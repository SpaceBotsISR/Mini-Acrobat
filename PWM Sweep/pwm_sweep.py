"""
PWM sweep of the 3 ESCs (Raspberry Pi 5, hardware PWM, 50 Hz).

ESC1 -> GPIO12 | ESC2 -> GPIO13 | ESC3 -> GPIO18

Arm at 1000 us for 3 s. Each sweep ramps up from 1100 to MAX_US (1300 us)
in 10 us steps, then back down the same way, holding 0.5 s per step.
Returns to 1000 us at the end. Raise MAX_US gradually between runs.

Sequence (ENTER before each; motors not in the combo stay at 1000 us):
  1) single:  (1), (2), (3)
  2) pairs:   (1,2), (1,3), (2,3)
  3) all:     (1,2,3)

Ctrl+C resets to 1000 us and stops.
"""

import itertools
import signal
import subprocess
import sys
import time

from rpi_hardware_pwm import HardwarePWM

# Configuration
MOTORS = {
    1: {"channel": 0, "gpio": 12, "alt": "a0"},
    2: {"channel": 1, "gpio": 13, "alt": "a0"},
    3: {"channel": 2, "gpio": 18, "alt": "a3"},
}

# TODO: Check if this is correct for the Pi 5 (PWM_CHIP)
PWM_CHIP = 2
FREQ_HZ = 50
PERIOD_US = 1000000 / FREQ_HZ

ARM_US = 1000
START_US = 1100
MAX_US = 1300
STEP_US = 10
HOLD_S = 0.5
ARM_TIME = 3.0


# Helper Funcions
def us_to_duty_cycle(us):
    """Convert microseconds to duty cycle percentage"""
    return (us / PERIOD_US) * 100.0


def mux_pins():
    """Pin Muxing for PWM channels (persistent across reboots)"""
    for m in MOTORS.values():
        subprocess.run(["pinctrl", "set", str(m["gpio"]), m["alt"]], check=True)


pwms = {}


def set_pulse(motor_ids, us):
    """Set the pulse width for the specified motors"""
    duty = us_to_duty_cycle(us)
    for m in motor_ids:
        pwms[m].change_duty_cycle(duty)


def stop_all():
    """Stop all PWM signals"""
    for p in pwms.values():
        try:
            p.change_duty_cycle(us_to_duty_cycle(ARM_US))
        except Exception:
            pass
    time.sleep(0.5)
    for p in pwms.values():
        try:
            p.stop()
        except Exception:
            pass


def on_sigint(sig, frame):
    """Handle Ctrl+C gracefully"""
    print("\nStopping all motors...")
    stop_all()
    sys.exit(0)


signal.signal(signal.SIGINT, on_sigint)


# Sweep
def sweep(motor_ids):
    """Sweep the specified motors from START_US to MAX_US and back"""
    print(
        f"Starting sweep for motors: {motor_ids}"
        f"(GPIO {[MOTORS[m]['gpio'] for m in motor_ids]})"
    )
    up = range(START_US, MAX_US + 1, STEP_US)
    down = range(MAX_US, START_US - 1, -STEP_US)
    for us in list(up) + list(down):
        set_pulse(motor_ids, us)
        print(f" {us} us", end="\r", flush=True)
        time.sleep(HOLD_S)
    set_pulse(motor_ids, ARM_US)
    time.sleep(1.0)


def main():
    """Main function to initialize PWM and perform sweeps on motors"""
    mux_pins()

    for m, cfg in MOTORS.items():
        p = HardwarePWM(pwm_channel=cfg["channel"], hz=FREQ_HZ, chip=PWM_CHIP)
        p.start(us_to_duty_cycle(ARM_US))
        pwms[m] = p

    print(f"Arming motors for {ARM_TIME} seconds...")
    time.sleep(ARM_TIME)

    ids = list(MOTORS.keys())
    phases = [
        ("Individual Motors", list(itertools.combinations(ids, 1))),
        ("Pairs of Motors", list(itertools.combinations(ids, 2))),
        ("All Motors", list(itertools.combinations(ids, 3))),
    ]

    try:
        for name, combos in phases:
            print(f"\nPhase: {name}")
            for combo in combos:
                input(f"Press Enter to sweep motors {combo} (Ctrl+C to stop)...")
                sweep(combo)
    finally:
        stop_all()


if __name__ == "__main__":
    main()
