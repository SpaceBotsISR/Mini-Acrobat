import subprocess, time
from rpi_hardware_pwm import HardwarePWM

subprocess.run(["pinctrl", "set", "12", "a0"], check=True)
p = HardwarePWM(pwm_channel=0, hz=400, chip=0)
d = lambda us: us / 2500 * 100

try:
    print("1000 us (armar)")
    p.start(d(1000)); time.sleep(4)
    print("1500 us")
    p.change_duty_cycle(d(1500)); time.sleep(5)
    print("de volta a 1000 us")
    p.change_duty_cycle(d(1000)); time.sleep(1)
finally:
    p.change_duty_cycle(d(1000)); time.sleep(0.5); p.stop()