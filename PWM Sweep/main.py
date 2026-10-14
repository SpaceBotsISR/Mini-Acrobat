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

#TODO: Check if this is correct for the Pi 5 (PWM_CHIP)
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
    print(f"Starting sweep for motors: {motor_ids}"
        f"(GPIO {[MOTORS[m]['gpio'] for m in motor_ids]})")
    up = range(START_US, MAX_US + 1, STEP_US)
    down = range(MAX_US, START_US - 1, -STEP_US)
    for us in list(up) + list(down):
        set_pulse(motor_ids, us)
        print(f" {us} us", end="\r", flush=True)
        time.sleep(HOLD_S)
    set_pulse(motor_ids, ARM_US)
    time.sleep(1.0)
    
    