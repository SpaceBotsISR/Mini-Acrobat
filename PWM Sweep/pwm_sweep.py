"""
Smooth PWM sweep of the 3 ESCs (Raspberry Pi 5, RP1 hardware PWM).
tests ALL direction combinations; --auto runs unattended.
BIDIRECTIONAL (3D) is now the DEFAULT, because the ESCs are configured in
3D mode (min 1168 / center 1500 / max 1832). The old unidirectional behaviour
is still available with --unidir, but in 3D mode it arms at 1000 us, which is
FULL REVERSE, so it asks for confirmation first.

ESC1 -> GPIO12 (PWM0_CHAN0) | ESC2 -> GPIO13 (PWM0_CHAN1) | ESC3 -> GPIO18 (PWM0_CHAN2)

BIDIRECTIONAL (default). ESCs MUST already be in 3D mode (BlueJay/BLHeli_S).
  Rest/arm pulse is NEUTRAL (default 1500 us).
  For each combo, for each direction (forward, then reverse):
  1) KICK: neutral +/- --kick-off us for --kick-time s (skipped if --kick-off 0)
  2) continuous ramp towards neutral +/- --max-off at --rate us/s
  3) hold --hold s, ramp back to neutral+/- --dead, return to neutral and wait
     --dir-wait s before the opposite direction (ESC needs time to switch)

  Deadband: pulses within neutral +/- --dead are treated as stop by the ESC.
  To find where the motor actually starts: --kick-off 0 --rate 20 and watch
  the printed us value when the motor begins to spin.

UNIDIRECTIONAL (--unidir, only for ESCs NOT in 3D mode). For each motor combo:
  1) KICK: jump to --kick us for --kick-time s to break static friction
  2) continuous ramp  kick -> --max  at --rate us/s
  3) hold at --max for --hold s, ramp back down to idle, return to 1000 us

Examples:
  python3 pwm_sweep.py --single                        # 3D test, one motor at a time
  python3 pwm_sweep.py                                 # 3D test, singles + pairs + all
  python3 pwm_sweep.py --single --kick-off 0 --rate 20 # find start points
  python3 pwm_sweep.py --max-off 300 --kick-off 150
  python3 pwm_sweep.py --by-dir --kick-off 0 --rate 15 --max-off 66   # direction check, ~20 %

Ctrl+C resets all channels to rest (neutral in 3D, 1000 us in --unidir) and stops.
"""

import argparse
import glob
import itertools
import os
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
REST_US = ARM_US  # 1000 us unidirectional, NEUTRAL in bidirectional mode
pwms = {}


def parse_args():
    ap = argparse.ArgumentParser(description="Smooth ESC sweep with start kick")
    ap.add_argument(
        "--freq",
        type=float,
        default=400.0,
        help="PWM frame rate in Hz (BLHeli_S accepts 50-500)",
    )
    ap.add_argument(
        "--kick",
        type=float,
        default=1300.0,
        help="kick pulse width in us (unidirectional)",
    )
    ap.add_argument("--kick-time", type=float, default=0.5, help="kick duration in s")
    ap.add_argument(
        "--max",
        type=float,
        default=1600.0,
        help="top pulse width in us (<= 2000) (unidirectional)",
    )
    ap.add_argument("--rate", type=float, default=100.0, help="ramp rate in us/s")
    ap.add_argument("--hold", type=float, default=2.0, help="hold time at max in s")
    ap.add_argument(
        "--chip",
        type=int,
        default=None,
        help="force pwmchip number (default: auto-detect RP1 PWM0)",
    )
    # Mode
    ap.add_argument(
        "--unidir",
        action="store_true",
        help="old unidirectional mode (ESCs NOT in 3D mode; arms at 1000 us)",
    )
    # Bidirectional options
    ap.add_argument(
        "--neutral", type=float, default=1500.0, help="3D neutral pulse in us"
    )
    ap.add_argument(
        "--dead",
        type=float,
        default=30.0,
        help="3D deadband half-width in us (ESC ignores neutral +/- this)",
    )
    ap.add_argument(
        "--kick-off",
        type=float,
        default=150.0,
        help="3D kick offset from neutral in us (0 = no kick)",
    )
    ap.add_argument(
        "--max-off",
        type=float,
        default=300.0,
        help="3D top offset from neutral in us (max 500)",
    )
    ap.add_argument(
        "--dir-wait",
        type=float,
        default=1.5,
        help="3D wait at neutral between directions in s",
    )
    ap.add_argument(
        "--auto",
        action="store_true",
        help="run all combinations without waiting for Enter between them",
    )
    ap.add_argument(
        "--by-dir",
        action="store_true",
        help="direction check: M1,M2,M3 all FORWARD first, then M1,M2,M3 all REVERSE",
    )
    ap.add_argument(
        "--single",
        action="store_true",
        help="only test individual motors (skip pairs and all)",
    )
    return ap.parse_args()


# Helpers
def find_pwm_chip():
    """Find the pwmchip of RP1 PWM0 (the block wired to GPIO12/13/18).
    PWM0 sits at 1f00098000, PWM1 at 1f0009c000; pwmchip numbering can vary."""
    for d in sorted(glob.glob("/sys/class/pwm/pwmchip*")):
        if "1f00098000" in os.path.realpath(d):
            return int(d.split("pwmchip")[-1])
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
            p.change_duty_cycle(us_to_duty(REST_US))
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
    """Continuous linear ramp (up or down), updated every UPDATE_S"""
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


def set_signs(signs, off):
    """Set each motor to neutral + sign*off (signs = {motor: +1/-1})."""
    n = ARGS.neutral
    for m, s in signs.items():
        pwms[m].change_duty_cycle(us_to_duty(n + s * off))


def ramp_off(signs, start_off, end_off, rate):
    """Ramp the offset from neutral; every motor moves in its own direction."""
    if start_off == end_off:
        return
    duration = abs(end_off - start_off) / rate
    t0 = time.monotonic()
    while True:
        t = time.monotonic() - t0
        if t >= duration:
            break
        off = start_off + (end_off - start_off) * t / duration
        set_signs(signs, off)
        n = ARGS.neutral
        abs_us = " ".join(f"M{m}:{n + sg * off:6.0f}" for m, sg in signs.items())
        print(f"  offset {off:6.1f} us -> {abs_us} us   ", end="\r", flush=True)
        time.sleep(UPDATE_S)
    set_signs(signs, end_off)


def sweep_dirs(motor_ids, pattern):
    """One sweep with a given direction per motor (+1 forward / -1 reverse),
    always starting from and returning to neutral, then waiting there."""
    n = ARGS.neutral
    signs = dict(zip(motor_ids, pattern))
    label = ", ".join(f"M{m} {'FWD' if s > 0 else 'REV'}" for m, s in signs.items())
    print(f"\n  --- {label} ---")

    set_pulse(motor_ids, n)
    time.sleep(0.5)

    ramp_from = 0.0
    if ARGS.kick_off > 0:
        print(f"  kick {ARGS.kick_off:.0f} us off neutral for {ARGS.kick_time}s")
        set_signs(signs, ARGS.kick_off)
        time.sleep(ARGS.kick_time)
        ramp_from = ARGS.kick_off

    print(f"  ramp {ramp_from:.0f} -> {ARGS.max_off:.0f} us off neutral at {ARGS.rate:.0f} us/s")
    ramp_off(signs, ramp_from, ARGS.max_off, ARGS.rate)
    time.sleep(ARGS.hold)

    print("\n  ramp down")
    ramp_off(signs, ARGS.max_off, ARGS.dead, ARGS.rate * 2)
    set_pulse(motor_ids, n)
    print(f"  neutral {n:.0f} us, waiting {ARGS.dir_wait}s")
    time.sleep(ARGS.dir_wait)


def sweep_bidir(motor_ids):
    """All direction combinations for these motors: 2 for one motor, 4 for a
    pair, 8 for three. Each combination is a full neutral-to-neutral sweep."""
    print(
        f"Bidirectional sweep motors {motor_ids} (GPIO {[MOTORS[m]['gpio'] for m in motor_ids]})"
    )
    for m in motor_ids:
        print(f"  motor {m} sysfs: {read_state(MOTORS[m]['channel'])}")
    for pattern in itertools.product((+1, -1), repeat=len(motor_ids)):
        sweep_dirs(motor_ids, pattern)


def main():
    global PERIOD_US, CHIP, ARGS, REST_US
    ARGS = parse_args()
    ARGS.bidir = not ARGS.unidir
    ARGS.max = min(ARGS.max, 2000.0)
    ARGS.max_off = min(ARGS.max_off, 500.0)
    ARGS.kick_off = min(ARGS.kick_off, ARGS.max_off)
    if ARGS.bidir:
        REST_US = ARGS.neutral
        if ARGS.dead >= ARGS.max_off:
            sys.exit("--dead must be smaller than --max-off")
    PERIOD_US = 1e6 / ARGS.freq
    CHIP = ARGS.chip if ARGS.chip is not None else find_pwm_chip()
    print(f"pwmchip{CHIP} -> {os.path.realpath(f'/sys/class/pwm/pwmchip{CHIP}')}")
    print(f"{ARGS.freq:.0f} Hz (period {PERIOD_US:.0f} us)")

    if ARGS.bidir:
        print("\n*** BIDIRECTIONAL (3D) MODE ***")
        print(
            f"Arming at NEUTRAL {ARGS.neutral:.0f} us. If an ESC is NOT in 3D mode, this is ~50 % throttle."
        )
        input(
            "Propellers OFF, motors free to spin, ESCs set to 3D? Press Enter to continue (Ctrl+C to abort)..."
        )
    else:
        print("\n*** UNIDIRECTIONAL MODE ***")
        print(
            f"Arming at {ARM_US:.0f} us. If the ESCs are in 3D mode, this is FULL REVERSE."
        )
        input(
            "Propellers OFF and ESCs NOT in 3D mode? Press Enter to continue (Ctrl+C to abort)..."
        )

    mux_pins()
    for m, cfg in MOTORS.items():
        p = HardwarePWM(pwm_channel=cfg["channel"], hz=ARGS.freq, chip=CHIP)
        p.start(us_to_duty(REST_US))
        pwms[m] = p

    print(f"Arming motors for {ARM_TIME} seconds at {REST_US:.0f} us...")
    time.sleep(ARM_TIME)

    if ARGS.bidir:
        ans = input("Did ALL motors stay still at neutral? [y/N] ").strip().lower()
        if ans != "y":
            print(
                "A motor spun at neutral -> that ESC is not in 3D mode (or neutral differs). Aborting."
            )
            stop_all()
            return

    ids = list(MOTORS.keys())
    phases = [("Individual Motors", list(itertools.combinations(ids, 1)))]
    if not ARGS.single:
        phases += [
            ("Pairs of Motors", list(itertools.combinations(ids, 2))),
            ("All Motors", list(itertools.combinations(ids, 3))),
        ]
    run = sweep_bidir if ARGS.bidir else sweep

    if ARGS.by_dir and ARGS.bidir:
        try:
            for sign, name in ((+1, "FORWARD"), (-1, "REVERSE")):
                print(f"\n=== ALL MOTORS {name}, one at a time (M1, M2, M3) ===")
                for m in ids:
                    if not ARGS.auto:
                        input(f"Press Enter for motor {m} {name} (Ctrl+C to stop)...")
                    sweep_dirs((m,), (sign,))
        finally:
            stop_all()
        return

    try:
        for name, combos in phases:
            print(f"\nPhase: {name}")
            for combo in combos:
                if ARGS.auto:
                    print(f"\nMotors {combo} (Ctrl+C to stop)")
                else:
                    input(f"Press Enter to sweep motors {combo} (Ctrl+C to stop)...")
                run(combo)
    finally:
        stop_all()


if __name__ == "__main__":
    main()