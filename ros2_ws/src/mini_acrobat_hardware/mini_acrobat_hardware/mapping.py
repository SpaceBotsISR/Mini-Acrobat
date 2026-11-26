"""Throttle -> ESC pulse width, for BLHeli_S in 3D (bidirectional) mode.

Same law as tools/pwm_sweep (percent mode): 0 is the neutral pulse; any
non-zero throttle skips the ESC deadzone on its side, so the same |throttle|
gives about the same thrust in both directions.

    pulse = neutral + s * (dead_s + |x| * (full - dead_s)),  s = sign(x)

Pure functions, no ROS: unit-tested in test/test_mapping.py.
"""
from dataclasses import dataclass


@dataclass(frozen=True)
class EscBand:
    neutral_us: float = 1500.0   # 3D centre (ESC config: 1168 / 1500 / 1832)
    full_us: float = 332.0       # useful range from neutral to either end
    dead_up_us: float = 40.0     # first us above neutral where the motor spins
    dead_down_us: float = 40.0   # first us below neutral where the motor spins
    min_us: float = 1168.0       # hard clamp
    max_us: float = 1832.0       # hard clamp
    zero_eps: float = 1e-3       # |throttle| below this is "stop"

    def validate(self) -> None:
        if not self.min_us < self.neutral_us < self.max_us:
            raise ValueError('need min_us < neutral_us < max_us')
        if self.full_us <= 0:
            raise ValueError('full_us must be > 0')
        if not (0 <= self.dead_up_us < self.full_us and 0 <= self.dead_down_us < self.full_us):
            raise ValueError('dead_up_us / dead_down_us must be in [0, full_us)')


def throttle_to_us(x: float, band: EscBand, invert: bool = False) -> float:
    """Pulse width in microseconds for throttle x in [-1, 1]."""
    if x != x:  # NaN -> stop
        return band.neutral_us
    x = max(-1.0, min(1.0, float(x)))
    if invert:
        x = -x
    if abs(x) < band.zero_eps:
        return band.neutral_us
    s = 1.0 if x > 0 else -1.0
    dead = band.dead_up_us if s > 0 else band.dead_down_us
    us = band.neutral_us + s * (dead + abs(x) * (band.full_us - dead))
    return max(band.min_us, min(band.max_us, us))


def us_to_duty_ns(us: float, period_ns: int) -> int:
    """Duty cycle in ns for a pulse of `us` microseconds, never above the period."""
    return max(0, min(period_ns, int(round(us * 1000.0))))
