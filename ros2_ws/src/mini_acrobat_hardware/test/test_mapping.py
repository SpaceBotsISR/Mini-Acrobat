import os
import sys

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..'))
from mini_acrobat_hardware.mapping import EscBand, throttle_to_us, us_to_duty_ns  # noqa: E402

B = EscBand()


def test_zero_is_neutral():
    assert throttle_to_us(0.0, B) == 1500.0
    assert throttle_to_us(1e-4, B) == 1500.0
    assert throttle_to_us(float('nan'), B) == 1500.0


def test_full_scale_hits_band_ends():
    assert throttle_to_us(1.0, B) == pytest.approx(1832.0)
    assert throttle_to_us(-1.0, B) == pytest.approx(1168.0)


def test_deadzone_skipped_and_symmetric():
    up = throttle_to_us(0.01, B) - 1500.0
    down = 1500.0 - throttle_to_us(-0.01, B)
    assert up > 40.0 and down > 40.0
    assert up == pytest.approx(down)


def test_clamped_and_inverted():
    assert throttle_to_us(5.0, B) == pytest.approx(1832.0)
    assert throttle_to_us(0.5, B, invert=True) == pytest.approx(3000.0 - throttle_to_us(0.5, B))


def test_duty_ns():
    assert us_to_duty_ns(1500.0, 2_500_000) == 1_500_000
    assert us_to_duty_ns(5000.0, 2_500_000) == 2_500_000


def test_bad_band():
    with pytest.raises(ValueError):
        EscBand(dead_up_us=400.0).validate()


def test_sysfs_channel(tmp_path, monkeypatch):
    from mini_acrobat_hardware import pwm_sysfs
    from mini_acrobat_hardware.pwm_sysfs import PwmChannel
    ch = tmp_path / 'pwmchip0' / 'pwm2'
    ch.mkdir(parents=True)
    for f in ('period', 'duty_cycle', 'enable'):
        (ch / f).write_text('0')

    def kernel_write(path, value):
        # like the kernel: state with period 0 or duty > period -> EINVAL
        name = os.path.basename(path)
        period = int((ch / 'period').read_text())
        duty = int((ch / 'duty_cycle').read_text())
        if name == 'period':
            period = int(value)
        elif name == 'duty_cycle':
            duty = int(value)
        if name in ('period', 'duty_cycle') and (period == 0 or duty > period):
            raise OSError(22, 'Invalid argument')
        with open(path, 'w') as f:
            f.write(value)

    monkeypatch.setattr(PwmChannel, '_write', staticmethod(kernel_write))
    assert pwm_sysfs  # imported for monkeypatch target
    pwm = PwmChannel(0, 2, sysfs_root=str(tmp_path))
    pwm.configure(2_500_000)
    assert (ch / 'period').read_text() == '2500000'
    pwm.set_duty(1_500_000)
    pwm.enable()
    assert (ch / 'duty_cycle').read_text() == '1500000'
    assert (ch / 'enable').read_text() == '1'
    pwm.disable()
    assert (ch / 'duty_cycle').read_text() == '0'
    assert (ch / 'enable').read_text() == '0'
    # re-configure to a shorter period than a leftover duty
    pwm.set_duty(2_000_000)
    pwm.configure(1_000_000)
    assert (ch / 'period').read_text() == '1000000'
    assert (ch / 'duty_cycle').read_text() == '0'
