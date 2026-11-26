"""Minimal Linux sysfs PWM driver for the RP1 PWM0 block of the Raspberry Pi 5.

RP1 PWM0 lives at 1f00098000 and drives GPIO12 (ch0), GPIO13 (ch1) and
GPIO18 (ch2). The pwmchip number varies between kernels, so it is found by
address. Exporting the channels, muxing the pins and giving this user write
access is done as root by tools/pi/pwm_setup.sh (systemd ExecStartPre), so
this module only writes period / duty_cycle / enable.
"""
import glob
import os
import time

RP1_PWM0_ADDR = '1f00098000'


def find_rp1_pwm0_chip(sysfs_root: str = '/sys/class/pwm') -> int:
    for d in sorted(glob.glob(os.path.join(sysfs_root, 'pwmchip*'))):
        if RP1_PWM0_ADDR in os.path.realpath(d):
            return int(d.rsplit('pwmchip', 1)[-1])
    raise RuntimeError(
        f'RP1 PWM0 ({RP1_PWM0_ADDR}) not found in {sysfs_root}. '
        'Enable it in /boot/firmware/config.txt (see tools/pi/pwm_setup.sh) and reboot.')


class PwmChannel:
    def __init__(self, chip: int, channel: int, sysfs_root: str = '/sys/class/pwm'):
        self.chip_dir = os.path.join(sysfs_root, f'pwmchip{chip}')
        self.dir = os.path.join(self.chip_dir, f'pwm{channel}')
        self.channel = channel
        if not os.path.isdir(self.dir):
            # normally already exported by pwm_setup.sh (root); try anyway
            try:
                self._write(os.path.join(self.chip_dir, 'export'), str(channel))
            except OSError as e:
                raise RuntimeError(
                    f'{self.dir} missing and cannot export it ({e}). '
                    'Run tools/pi/pwm_setup.sh as root.') from e
            for _ in range(50):
                if os.path.exists(os.path.join(self.dir, 'enable')):
                    break
                time.sleep(0.02)
        self.enabled = False
        self.period_ns = 0
        self.duty_ns = 0

    @staticmethod
    def _write(path: str, value: str) -> None:
        with open(path, 'w') as f:
            f.write(value)

    def _set(self, name: str, value: int) -> None:
        self._write(os.path.join(self.dir, name), str(int(value)))

    def configure(self, period_ns: int) -> None:
        """Disable, zero the duty, then set the period (duty must stay <= period)."""
        self.disable()
        self._set('duty_cycle', 0)
        self.duty_ns = 0
        self._set('period', period_ns)
        self.period_ns = period_ns

    def set_duty(self, duty_ns: int) -> None:
        duty_ns = max(0, min(self.period_ns, int(duty_ns)))
        if duty_ns != self.duty_ns:
            self._set('duty_cycle', duty_ns)
            self.duty_ns = duty_ns

    def enable(self) -> None:
        if not self.enabled:
            self._set('enable', 1)
            self.enabled = True

    def disable(self) -> None:
        """No pulses at all: duty 0 first (pin low), then switch the channel off."""
        try:
            self._set('duty_cycle', 0)
            self.duty_ns = 0
        except OSError:
            pass
        try:
            self._set('enable', 0)
        except OSError:
            pass
        self.enabled = False
