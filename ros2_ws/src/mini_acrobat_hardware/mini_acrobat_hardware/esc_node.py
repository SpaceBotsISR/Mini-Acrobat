"""ESC node: the only node that touches the motor PWM pins.

    /motor_cmd (MotorCommand, from safety_node, 50 Hz) -> RP1 hardware PWM
    ESC1 -> GPIO12 (ch0) | ESC2 -> GPIO13 (ch1) | ESC3 -> GPIO18 (ch2)

Contract (docs/safety.md):
  * armed == false              -> NO PWM (duty 0, channel disabled, pin low)
  * armed == true               -> neutral first (ESCs arm and beep), then
                                   follow throttle (0.0 = neutral)
  * no /motor_cmd for cmd_timeout_ms -> NO PWM, even if the safety node died
  * shutdown / exception        -> NO PWM

After every OFF -> ON transition the output is held at neutral for
arm_neutral_ms, whatever the throttle, so the BLHeli_S ESCs always see a
clean neutral and arm before any thrust is asked.
"""
import time

import rclpy
from rclpy.node import Node
from rclpy.qos import QoSProfile, ReliabilityPolicy, HistoryPolicy

from mini_acrobat_interfaces.msg import MotorCommand

from .mapping import EscBand, throttle_to_us, us_to_duty_ns
from .pwm_sysfs import PwmChannel, find_rp1_pwm0_chip

N_MOTORS = 3


class EscNode(Node):
    def __init__(self):
        super().__init__('esc_node')
        p = self.declare_parameter
        self.freq_hz = float(p('pwm_freq_hz', 400.0).value)
        chip = int(p('pwm_chip', -1).value)
        channels = list(p('pwm_channels', [0, 1, 2]).value)
        invert = list(p('invert', [False, False, False]).value)
        self.cmd_timeout = float(p('cmd_timeout_ms', 100.0).value) / 1000.0
        self.arm_neutral = float(p('arm_neutral_ms', 1000.0).value) / 1000.0
        rate_hz = float(p('update_rate_hz', 100.0).value)
        self.band = EscBand(
            neutral_us=float(p('neutral_us', 1500.0).value),
            full_us=float(p('full_us', 332.0).value),
            dead_up_us=float(p('dead_up_us', 40.0).value),
            dead_down_us=float(p('dead_down_us', 40.0).value),
            min_us=float(p('min_us', 1168.0).value),
            max_us=float(p('max_us', 1832.0).value),
        )
        self.band.validate()
        if len(channels) != N_MOTORS or len(invert) != N_MOTORS:
            raise ValueError('pwm_channels and invert need 3 entries')
        if not 50.0 <= self.freq_hz <= 500.0:
            raise ValueError('pwm_freq_hz must be 50..500 (BLHeli_S)')
        self.invert = [bool(v) for v in invert]

        if chip < 0:
            chip = find_rp1_pwm0_chip()
        self.period_ns = int(round(1e9 / self.freq_hz))
        if self.band.max_us * 1000 >= self.period_ns:
            raise ValueError('max_us does not fit in the PWM period; lower pwm_freq_hz')
        self.pwms = [PwmChannel(chip, ch) for ch in channels]
        for pwm in self.pwms:
            pwm.configure(self.period_ns)   # starts OFF

        self.last_cmd = None
        self.last_cmd_t = None
        self.output_on = False
        self.off_applied = True     # configure() left every channel disabled
        self.on_since = 0.0
        self.last_us = [0.0] * N_MOTORS

        qos = QoSProfile(depth=1, reliability=ReliabilityPolicy.BEST_EFFORT,
                         history=HistoryPolicy.KEEP_LAST)
        # BEST_EFFORT subscription also matches the safety node's reliable publisher
        self.create_subscription(MotorCommand, 'motor_cmd', self.on_cmd, qos)
        self.create_timer(1.0 / rate_hz, self.tick)

        self.get_logger().info(
            f'esc_node up: pwmchip{chip} ch{channels}, {self.freq_hz:.0f} Hz, '
            f'neutral {self.band.neutral_us:.0f} us, band {self.band.min_us:.0f}..'
            f'{self.band.max_us:.0f} us, invert {self.invert}, '
            f'cmd timeout {self.cmd_timeout * 1000:.0f} ms. Output OFF.')

    def on_cmd(self, msg: MotorCommand) -> None:
        self.last_cmd = msg
        self.last_cmd_t = time.monotonic()

    def _off(self, why: str) -> None:
        if not self.output_on and self.off_applied:
            return  # already off: do not rewrite sysfs at 100 Hz
        for pwm in self.pwms:
            pwm.disable()
        if self.output_on:
            self.get_logger().warn(f'PWM OFF: {why}')
        self.output_on = False
        self.off_applied = True

    def tick(self) -> None:
        now = time.monotonic()
        try:
            if self.last_cmd is None:
                self._off('no /motor_cmd yet')
                return
            if now - self.last_cmd_t > self.cmd_timeout:
                self._off(f'/motor_cmd stale (> {self.cmd_timeout * 1000:.0f} ms)')
                return
            if not self.last_cmd.armed:
                self._off('disarmed')
                return

            if not self.output_on:
                self.output_on = True
                self.off_applied = False
                self.on_since = now
                self.get_logger().info(
                    f'PWM ON: neutral {self.band.neutral_us:.0f} us '
                    f'for {self.arm_neutral * 1000:.0f} ms (ESC arming)')

            holding = now - self.on_since < self.arm_neutral
            for i, pwm in enumerate(self.pwms):
                if holding:
                    us = self.band.neutral_us
                else:
                    us = throttle_to_us(self.last_cmd.throttle[i], self.band, self.invert[i])
                pwm.set_duty(us_to_duty_ns(us, self.period_ns))
                pwm.enable()
                self.last_us[i] = us
        except Exception as e:  # never leave a pin running on an error
            self.get_logger().error(f'PWM error: {e}')
            self.off_applied = False
            self._off('error')

    def shutdown(self) -> None:
        for pwm in self.pwms:
            pwm.disable()
        self.output_on = False


def main(args=None):
    rclpy.init(args=args)
    node = None
    try:
        node = EscNode()
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        if node is not None:
            node.shutdown()
            node.destroy_node()
        if rclpy.ok():
            rclpy.shutdown()


if __name__ == '__main__':
    main()
