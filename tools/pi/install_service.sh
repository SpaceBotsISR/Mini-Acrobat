#!/usr/bin/env bash
# Run ON THE PI (Ubuntu 24.04 + ROS 2 Jazzy). Builds ros2_ws and installs the
# mini-acrobat systemd service, so the drone side (safety_node, link_monitor,
# rosbridge :9090, foxglove :8765) starts on every boot and is always ready
# for MAgcs. The safety node always boots DISARMED.
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
  rosdep install --from-paths "$WS/src" --ignore-src -y
  echo "==> colcon build"
  (cd "$WS" && colcon build --symlink-install)
fi

echo "==> installing $UNIT (user=$RUN_USER, ws=$WS)"
sed -e "s#@USER@#$RUN_USER#g" -e "s#@WS@#$WS#g" "$UNIT_IN" | sudo tee "$UNIT" >/dev/null
sudo systemctl daemon-reload
sudo systemctl enable mini-acrobat.service
sudo systemctl restart mini-acrobat.service

sleep 3
systemctl --no-pager --lines=15 status mini-acrobat.service || true
echo
echo "rosbridge should be listening on :9090 ->"
ss -ltn | grep -E ':(9090|8765)\b' || echo "  (not yet; check: journalctl -u mini-acrobat -f)"
