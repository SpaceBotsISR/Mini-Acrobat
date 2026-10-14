import itertools
import subprocess
from time import time


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
    duty = us_to_duty_cycle(us)
    for m in motor_ids:
        pwms[m].change_duty_cycle(duty)

def stop_all():
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