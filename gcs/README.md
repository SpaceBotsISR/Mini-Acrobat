# MAgcs

Ground control station for the Mini-Acrobat: Qt6 Widgets app that connects to the
drone through **rosbridge** (WebSocket), shows live data and sends motor inputs.
Design, topic contract and safety rules: [`docs/gcs.md`](../docs/gcs.md).

## Install as a Mac app (Apple Silicon)

Use a terminal **without** the `ros_jazzy` micromamba env active, so CMake picks
the Homebrew (arm64) Qt and not the conda one.

```bash
brew install qt cmake ninja
gcs/scripts/build_app.sh                 # -> /Applications/MAgcs.app
# or: gcs/scripts/build_app.sh ~/Applications
```

The script builds Release/arm64, installs the bundle, runs `macdeployqt` (Qt
frameworks and plugins copied into `MAgcs.app`, so it no longer depends on
Homebrew's Qt) and ad-hoc signs it. Open **MAgcs** from Launchpad/Spotlight like
any other app. Re-run the script after pulling changes.

The app is not notarised: if macOS ever blocks it (e.g. after copying it to
another Mac), right-click > Open once.

## Development build

```bash
cd gcs
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build build
open build/MAgcs.app                      # uses Homebrew's Qt, not standalone
```

Needs Qt >= 6.4 (modules: Widgets, Network, WebSockets, Charts, Test). Tested
with 6.4.2 on Linux; the CoreWLAN part (`src/platform/WifiInfo_mac.mm`) only
builds on macOS.

## Using it

On start MAgcs opens **Connect to Mini-Acrobat**:

- **Tailscale (VPN)**: state of Tailscale on this Mac, the tailnet's devices
  (online / offline, direct or relayed path), the drone's host and a path test
  (`tailscale ping`: direct vs DERP relay, round-trip time). Warns when the path
  is relayed or slower than 150 ms (heartbeat timeout is 300 ms).
- **Manual (IP / hostname)**: same network (phone hotspot), `raspberrypi.local` or an IP.
- **Simulator (fake drone)**: `localhost`, for `tools/fake_drone/fake_drone.py`.

Every mode shows its "how to connect" steps, this Mac's Wi-Fi signal (RSSI,
network, TX rate) and IPs, and the rosbridge port. *Connect* closes the window
once rosbridge answers; *Open without connecting* goes straight to the main window.

In the main window the **Connection** dock keeps showing the link (target,
which drone IP / Mac IP the socket uses, RTT, msg/s), the Tailscale path, this
Mac's Wi-Fi and the **drone's** Wi-Fi and IPs from `/link/status`.
*Connection > Connect to drone...* (Cmd+K) reopens the connect window.

macOS hides the Wi-Fi network name from apps without Location permission:
click *Show network name...* in the "This Mac" box to allow it (signal strength
works without it). The Tailscale CLI is found in `Tailscale.app` or Homebrew.

Command line (skips the connect window):
`MAgcs --mode tailscale|manual|simulator --host <host> --port <port> --connect`
(`open /Applications/MAgcs.app --args ...`).

## Test

```bash
python3 -m pip install websockets          # only for the integration test + fake drone
ctest --test-dir build --output-on-failure
```

- `tst_protocol`, `tst_safetystate`: rosbridge JSON and message parsing.
- `tst_network`: `tailscale status --json` / `tailscale ping` parsing, Wi-Fi
  RSSI, `/link/status`, connection modes.
- `tst_integration`: starts `tools/fake_drone/fake_drone.py` and runs the real
  `Drone` / `RosbridgeClient` / `HeartbeatSender` against it: arm refusal, arm,
  link loss -> disarm (latched), control-output rules, reconnect, `/link/status`.
  It is skipped if python or `websockets` are missing (set `MAGCS_PYTHON` to pick another python).

## Run without the drone

```bash
python3 tools/fake_drone/fake_drone.py --port 9090 -v      # terminal 1
open /Applications/MAgcs.app                               # pick "Simulator", Connect
```

In the app: **ARM**, tick **Control output enabled**, move the sliders. The
*Safety > Simulate link loss* menu pauses the heartbeat: the fake drone disarms
after 300 ms, like the real safety node should.

## Run with the drone

On the Pi (ROS 2 Jazzy): `ros2 launch mini_acrobat_bringup bringup.launch.py`
(safety node, link monitor, rosbridge on `:9090`, read-only Foxglove bridge on `:8765`).
In MAgcs pick the mode and the drone, then **Connect**.
