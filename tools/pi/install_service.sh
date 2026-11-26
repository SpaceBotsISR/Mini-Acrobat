#!/usr/bin/env bash
# Run ON THE PI (Ubuntu 24.04 + ROS 2 Jazzy). Builds ros2_ws and installs the
# mini-acrobat systemd service, so the drone side (safety_node, esc_node,
# link_monitor, rosbridge :9090, foxglove :8765) starts on every boot and is
# always ready for MAgcs. The safety node always boots DISARMED, and esc_node
# emits no PWM until the drone is armed.
#
#   tools/pi/install_service.sh            # deps + build + install + start
#   tools/pi/install_service.sh --no-build # only (re)install the unit
#
# Afterwards:
#   systemctl status mini-acrobat          journalctl -u mini-acrobat -f
#   sudo systemctl restart mini-acrobat    (after every colcon build)
#   sudo systemctl disable --now mini-acrobat   (run by hand again)
set -euo pipefail

BUILD=1
[[ "${1:-}" == "--no-build" ]] && BUILD=0

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
WS="$REPO/ros2_ws"
UNIT_IN="$REPO/tools/pi/mini-acrobat.service.in"
UNIT=/etc/systemd/system/mini-acrobat.service
RUN_USER="${SUDO_USER:-$USER}"

[[ -d "$WS/src" ]] || { echo "error: $WS/src not found" >&2; exit 1; }
[[ -f /opt/ros/jazzy/setup.bash ]] || { echo "error: ROS 2 Jazzy not installed" >&2; exit 1; }
[[ $EUID -ne 0 ]] || { echo "error: run as your user, not root (sudo is called when needed)" >&2; exit 1; }

# shellcheck disable=SC1091
set +u; source /opt/ros/jazzy/setup.bash; set -u

if [[ $BUILD -eq 1 ]]; then
  echo "==> dependencies"
  sudo apt-get install -y ros-jazzy-rosbridge-suite ros-jazzy-foxglove-bridge iw
  sudo apt-get install -y device-tree-compiler
  rosdep install --from-paths "$WS/src" --ignore-src -y
  echo "==> colcon build"
  (cd "$WS" && colcon build --symlink-install)
fi

# RP1 hardware PWM0 (GPIO12/13/18) is enabled by our own overlay
# (tools/pi/mini-acrobat-pwm.dts). The stock pwm-2chan overlay does NOT work on
# the Pi 5: it enables the SoC PWM (107d517a80), not RP1 PWM0 (1f00098000).
CONFIG=/boot/firmware/config.txt
DTS="$REPO/tools/pi/mini-acrobat-pwm.dts"
DTBO=/boot/firmware/overlays/mini-acrobat-pwm.dtbo
NEED_REBOOT=0
for sym in rp1_pwm0 rp1_gpio; do
  [[ -e /proc/device-tree/__symbols__/$sym ]] || { echo "error: device-tree symbol '$sym' not found (not a Pi 5?)" >&2; exit 1; }
done
command -v dtc >/dev/null || sudo apt-get install -y device-tree-compiler
TMP_DTBO="$(mktemp --suffix=.dtbo)"
dtc -q -@ -I dts -O dtb -o "$TMP_DTBO" "$DTS"
if ! sudo cmp -s "$TMP_DTBO" "$DTBO" 2>/dev/null; then
  echo "==> installing $DTBO"
  sudo install -m 644 "$TMP_DTBO" "$DTBO"
  NEED_REBOOT=1
fi
rm -f "$TMP_DTBO"
if grep -qE '^dtoverlay=pwm-2chan' "$CONFIG"; then
  echo "==> removing the old pwm-2chan overlay from $CONFIG"
  sudo sed -i -E 's/^(dtoverlay=pwm-2chan.*)$/# \1   (replaced by mini-acrobat-pwm)/' "$CONFIG"
  NEED_REBOOT=1
fi
if ! grep -qE '^dtoverlay=mini-acrobat-pwm' "$CONFIG"; then
  echo "==> enabling RP1 PWM0 in $CONFIG"
  printf '\n# Mini-Acrobat ESCs: RP1 PWM0 on GPIO12/13/18 (tools/pi/mini-acrobat-pwm.dts)\n[all]\ndtoverlay=mini-acrobat-pwm\n' \
    | sudo tee -a "$CONFIG" >/dev/null
  NEED_REBOOT=1
fi
chmod +x "$REPO/tools/pi/pwm_setup.sh"

echo "==> installing $UNIT (user=$RUN_USER, ws=$WS)"
sed -e "s#@USER@#$RUN_USER#g" -e "s#@WS@#$WS#g" -e "s#@REPO@#$REPO#g" "$UNIT_IN" | sudo tee "$UNIT" >/dev/null
sudo systemctl daemon-reload
sudo systemctl enable mini-acrobat.service
if [[ $NEED_REBOOT -eq 1 ]]; then
  echo
  echo "PWM overlay installed/changed: run 'sudo reboot'. The service starts by itself after the reboot."
  exit 0
fi
sudo systemctl restart mini-acrobat.service

sleep 3
systemctl --no-pager --lines=15 status mini-acrobat.service || true
echo
echo "rosbridge should be listening on :9090 ->"
ss -ltn | grep -E ':(9090|8765)\b' || echo "  (not yet; check: journalctl -u mini-acrobat -f)"
