#!/usr/bin/env bash
# Prepares the RP1 hardware PWM for the ESC node (Raspberry Pi 5). Runs as ROOT
# from the mini-acrobat systemd service (ExecStartPre / ExecStopPost), so the
# ESC node itself runs as a normal user.
#
#   pwm_setup.sh <user>     export ch0-2, mux GPIO12/13/18, give <user> write access
#   pwm_setup.sh --off      disable all 3 channels (duty 0, enable 0)
#
# ESC1 GPIO12 = PWM0 ch0 (alt a0) | ESC2 GPIO13 = ch1 (a0) | ESC3 GPIO18 = ch2 (a3)
set -uo pipefail

ADDR=1f00098000
CHANNELS=(0 1 2)
PINS=("12 a0" "13 a0" "18 a3")

chip_dir=""
for d in /sys/class/pwm/pwmchip*; do
  [[ -e "$d" ]] || continue
  if [[ "$(readlink -f "$d")" == *"$ADDR"* ]]; then chip_dir="$d"; break; fi
done

if [[ -z "$chip_dir" ]]; then
  echo "pwm_setup: RP1 PWM0 ($ADDR) not found. Install the overlay with" >&2
  echo "  tools/pi/install_service.sh   (adds dtoverlay=mini-acrobat-pwm) and reboot." >&2
  exit 1
fi

if [[ "${1:-}" == "--off" ]]; then
  for ch in "${CHANNELS[@]}"; do
    [[ -d "$chip_dir/pwm$ch" ]] || continue
    echo 0 > "$chip_dir/pwm$ch/duty_cycle" 2>/dev/null
    echo 0 > "$chip_dir/pwm$ch/enable" 2>/dev/null
  done
  echo "pwm_setup: all channels off"
  exit 0
fi

RUN_USER="${1:?usage: pwm_setup.sh <user> | --off}"

for ch in "${CHANNELS[@]}"; do
  if [[ ! -d "$chip_dir/pwm$ch" ]]; then
    echo "$ch" > "$chip_dir/export" || { echo "pwm_setup: export pwm$ch failed" >&2; exit 1; }
  fi
  for _ in $(seq 50); do [[ -e "$chip_dir/pwm$ch/enable" ]] && break; sleep 0.02; done
  echo 0 > "$chip_dir/pwm$ch/duty_cycle" 2>/dev/null
  echo 0 > "$chip_dir/pwm$ch/enable" 2>/dev/null
  for f in period duty_cycle enable polarity; do
    chown "$RUN_USER" "$(readlink -f "$chip_dir/pwm$ch/$f")"
  done
done

# Pin mux + pull-downs are done by the mini-acrobat-pwm overlay. If `pinctrl`
# happens to be installed, re-assert them (harmless otherwise).
if command -v pinctrl >/dev/null; then
  for p in "${PINS[@]}"; do
    read -r gpio alt <<< "$p"
    pinctrl set "$gpio" "$alt" pd
  done
fi

echo "pwm_setup: $(basename "$chip_dir") ch${CHANNELS[*]} ready for $RUN_USER, output OFF"
