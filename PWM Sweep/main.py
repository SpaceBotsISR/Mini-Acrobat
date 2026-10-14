import itertools

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