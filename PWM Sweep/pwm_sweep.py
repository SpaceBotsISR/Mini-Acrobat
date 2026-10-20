"""
Smooth PWM sweep of the 3 ESCs (Raspberry Pi 5, RP1 hardware PWM).

ESC1 -> GPIO12 (PWM0_CHAN0) | ESC2 -> GPIO13 (PWM0_CHAN1) | ESC3 -> GPIO18 (PWM0_CHAN2)

For each motor combo (ENTER before each; the others stay at 1000 us):
  1) KICK: jump to --kick us for --kick-time s to break static friction
  2) continuous ramp  kick -> --max  at --rate us/s
  3) hold at --max for --hold s, ramp back down to idle, return to 1000 us

BLHeli_S factory defaults: throttle min ~1148 us, max ~1832 us.
Below 1148 us = zero throttle; above 1832 us = still 100 %.
Raise --max gradually between runs. The drone MUST be restrained.

Examples:
  python3 pwm_sweep_v2.py                      # max 1600 us, kick 1300 us
  python3 pwm_sweep_v2.py --max 1832 --kick 1350 --rate 80
  python3 pwm_sweep_v2.py --freq 50            # back to 50 Hz frame rate

Ctrl+C resets all channels to 1000 us and stops.
"""

import argparse
import glob
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

PWM_CHIP_FALLBACK = 0
ARM_US = 1000
IDLE_US = 1150  # just above the BLHeli_S default min throttle (1148 us)
ARM_TIME = 3.0
UPDATE_S = 0.02  # ramp update period (50 Hz)

PERIOD_US = 20000.0
CHIP = PWM_CHIP_FALLBACK
ARGS = None
pwms = {}


def parse_args():
    ap = argparse.ArgumentParser(description="Smooth ESC sweep with start kick")
    ap.add_argument("--freq", type=float, default=400.0, help="PWM frame rate in Hz (BLHeli_S accepts 50-500)")
    ap.add_argument("--kick", type=float, default=1300.0, help="kick pulse width in us")
    ap.add_argument("--kick-time", type=float, default=0.5, help="kick duration in s")
    ap.add_argument("--max", type=float, default=1600.0, help="top pulse width in us (<= 2000)")
    ap.add_argument("--rate", type=float, default=100.0, help="ramp rate in us/s")
    ap.add_argument("--hold", type=float, default=2.0, help="hold time at max in s")
    return ap.parse_args()


# Helpers
def find_pwm_chip():
    """Find the pwmchip that exposes 4 channels (RP1 PWM0)"""
    for d in sorted(glob.glob("/sys/class/pwm/pwmchip*")):
        try:
            with open(f"{d}/npwm") as f:
                if int(f.read().strip()) >= 4:
                    return int(d.split("pwmchip")[-1])
        except (OSError, ValueError):
            continue
    return PWM_CHIP_FALLBACK


def us_to_duty(us):
    """Convert microseconds to duty cycle percentage"""
    us = max(ARM_US, min(2000.0, us))
    return (us / PERIOD_US) * 100.0


def mux_pins():
    """Pin muxing for the PWM channels (not persistent across reboots)"""
    for m in MOTORS.values():
        subprocess.run(["pinctrl", "set", str(m["gpio"]), m["alt"]], check=True)


def read_state(ch):
    base = f"/sys/class/pwm/pwmchip{CHIP}/pwm{ch}"
    out = {}
    for k in ("period", "duty_cycle", "enable"):
        try:
            with open(f"{base}/{k}") as f:
                out[k] = f.read().strip()
        except OSError:
            out[k] = "?"
    return out


def set_pulse(motor_ids, us):
    duty = us_to_duty(us)
    for m in motor_ids:
        pwms[m].change_duty_cycle(duty)


def stop_all():
    for p in pwms.values():
        try:
            p.change_duty_cycle(us_to_duty(ARM_US))
        except Exception:
            pass
    time.sleep(0.5)
    for p in pwms.values():
        try:
            p.stop()
        except Exception:
            pass


def on_sigint(sig, frame):
    print("\nStopping all motors...")
    stop_all()
    sys.exit(0)


signal.signal(signal.SIGINT, on_sigint)


# Sweep
def ramp(motor_ids, start_us, end_us, rate):
    """Continuous linear ramp, updated every UPDATE_S"""
    if start_us == end_us:
        return
    duration = abs(end_us - start_us) / rate
    t0 = time.monotonic()
    while True:
        t = time.monotonic() - t0
        if t >= duration:
            break
        us = start_us + (end_us - start_us) * t / duration
        set_pulse(motor_ids, us)
        print(f"  {us:7.1f} us", end="\r", flush=True)
        time.sleep(UPDATE_S)
    set_pulse(motor_ids, end_us)


def sweep(motor_ids):
    print(f"Sweep motors {motor_ids} (GPIO {[MOTORS[m]['gpio'] for m in motor_ids]})")
    for m in motor_ids:
        print(f"  motor {m} sysfs: {read_state(MOTORS[m]['channel'])}")

    set_pulse(motor_ids, ARM_US)
    time.sleep(0.5)

    print(f"  kick {ARGS.kick:.0f} us for {ARGS.kick_time}s")
    set_pulse(motor_ids, ARGS.kick)
    time.sleep(ARGS.kick_time)

    print(f"  ramp up to {ARGS.max:.0f} us at {ARGS.rate:.0f} us/s")
    ramp(motor_ids, ARGS.kick, ARGS.max, ARGS.rate)
    time.sleep(ARGS.hold)

    print("\n  ramp down")
    ramp(motor_ids, ARGS.max, IDLE_US, ARGS.rate * 2)
    set_pulse(motor_ids, ARM_US)
    time.sleep(1.0)


def main():
    global PERIOD_US, CHIP, ARGS
    ARGS = parse_args()
    ARGS.max = min(ARGS.max, 2000.0)
    PERIOD_US = 1e6 / ARGS.freq
    CHIP = find_pwm_chip()
    print(f"pwmchip{CHIP}, {ARGS.freq:.0f} Hz (period {PERIOD_US:.0f} us)")

    mux_pins()
    for m, cfg in MOTORS.items():
        p = HardwarePWM(pwm_channel=cfg["channel"], hz=ARGS.freq, chip=CHIP)
        p.start(us_to_duty(ARM_US))
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