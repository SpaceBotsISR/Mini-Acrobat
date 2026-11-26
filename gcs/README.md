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

The script builds Release/arm64, copies the Qt frameworks and the Cocoa plugin
into `MAgcs.app` (so it no longer depends on Homebrew's Qt), checks nothing
still links into Homebrew, ad-hoc signs it and replaces the installed app
(details in `docs/gcs.md`, *Packaging*). Open **MAgcs** from Launchpad/Spotlight like
any other app.

Iterating: re-run `gcs/scripts/build_app.sh` after every change. It recompiles
only what changed (`gcs/build-release`), quits the running MAgcs, replaces the
installed bundle and opens the new one. `--no-open` skips opening, `--clean`
rebuilds from scratch.

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

- **Tailscale (VPN)**: state of Tailscale on this Mac and of the drone's device
  only (online / offline, Tailscale IP; the rest of the tailnet is never shown),
  and a path test
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

### Main window: panels and layouts

Every section is a dock: drag a title bar to move it, drop it on another dock to
make tabs, on an edge to split, double-click to float it, ✕ to close it. All of
it is dark-themed (`src/widgets/Theme.cpp`).

- **Fixed sections** (*View* menu to show again): Connection, Safety, Motors, Console.
- **Panels** (*+ Panel* in the toolbar or the *Panels* menu, as many as needed):
  - **Plot**: several signals at once, as message paths `/topic.field[i]`
    (e.g. `/imu/data.angular_velocity.x`). Type a path (Tab completes fields),
    drop a field from Topics, or double-click a field in Topics (goes to the
    last plot you clicked). Legend: click the colour swatch to change it, click
    the name to hide it, right-click for more. Window 5 s–10 min (mouse wheel
    zooms), Auto Y or fixed min/max, pause, clear.
  - **Topics**: every topic on the drone with type and rate; expand a topic to
    see its numeric fields live. Needs **rosapi** on the drone (started by
    `rosbridge_websocket_launch.xml`).
  - **Raw messages**: the last message of a topic as a tree.
  - **Gauge**: one value, big, with a min–max bar.
  - **Publish**: publish JSON to any topic once or at N Hz (*Template* fills in
    the fields of the type). The safety topics (`/gcs/heartbeat`,
    `/control/motor_setpoint`, `/control/cmd`) are refused here.
  - **Fly mode**: see below.
- **Layouts** (toolbar): the arrangement + every panel's settings (paths,
  colours, ranges), saved by name, e.g. one per test run. *Layout ▾*: Save
  (Cmd+S), Save as new, Rename, Delete, Reset to the default arrangement,
  Export / Import (`.json`, to share). The current layout is saved when
  switching and on quit.

### Safety keys

- **Space**: DISARM, from anywhere in the app, also with the cursor in a path,
  topic or number field (none of them needs spaces). Only the Publish JSON
  editor types spaces. **Esc** leaves a text field.
- **Shift+Space**: ARM (only if connected, state valid and disarmed).
- The ARM button lights red when arming is possible, DISARM green when armed.
- Connection > Heartbeat shows the measured heartbeat rate (target 20 Hz).

### Fly mode (test)

*Enable fly mode* (only while ARMED) publishes `geometry_msgs/msg/Twist` on
`/control/cmd` at 20 Hz: `linear.x` (W/S, forward is +x), `linear.y` (A/D, left
is +y), `angular.z` (Q/E, +yaw counter-clockwise), normalised to the
"max" values set in the panel. Keys are ignored while typing in a text field;
all are released when MAgcs loses focus. Fly mode and Motors *control output*
exclude each other, and both stop on disarm or disconnect. **No node on the
drone uses `/control/cmd` yet**; the motor positions in the drawing are
placeholders (`DroneTopView.cpp`, `kMotorAngleDeg`).

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
- `tst_messagepath`: plot paths (`/topic.a.b[2]`) and numeric-field listing.
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

In the app: **ARM**, turn on **Enable control output**, move the sliders. The fake
drone also answers rosapi and publishes `/imu/data`, `/battery` and `/fake/pose`
(which moves with Fly mode while armed), so Topics, Plot and Fly mode can be tried.

Simulated flight, without touching the keyboard (only while ARMED; Fly mode wins):

```bash
python3 tools/fake_drone/fake_drone.py --scenario forward --level 0.4 --hover 0.2
```

Scenarios: `forward`, `circle`, `square`, `sine`, `step`. `--level` is the
command (-1..1), `--hover` a base throttle on every motor. A fake mixer turns
the command into `/motor_cmd` throttles (not the drone's real mixer) and
`/fake/pose` integrates it. To change it while running, use a **Publish**
panel: topic `/fake/scenario`, type `std_msgs/msg/String`,
`{"data": "circle 0.5"}` (`"off"` stops). The
*Safety > Simulate link loss* menu pauses the heartbeat: the fake drone disarms
after 300 ms, like the real safety node should.

## Run with the drone

On the Pi (ROS 2 Jazzy): `ros2 launch mini_acrobat_bringup bringup.launch.py`
(safety node, link monitor, rosbridge on `:9090`, read-only Foxglove bridge on `:8765`).
In MAgcs pick the mode and the drone, then **Connect**.

## Icon

`resources/magcs.png` (window icon) and `resources/MAgcs.icns` (bundle icon) are
generated from the square artwork `resources/magcs_icon_source.png`, masked to
the macOS rounded-square shape. To change the icon, replace that file, then:

```bash
python3 gcs/tools/icon/make_icon.py      # needs Pillow
gcs/scripts/build_app.sh
```
