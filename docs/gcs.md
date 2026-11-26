# MAgcs (ground control station)

Qt6 Widgets app in `gcs/`. It is the **GCS** of `docs/safety.md`: it sends the
heartbeat, arms/disarms, sends the manual motor inputs and shows the drone's data.

```
MAgcs (Mac) --ws:9090--> rosbridge_server --> ROS 2 graph (safety_node, control, hardware)
Foxglove    --ws:8765--> foxglove_bridge  --> (read-only view of the same graph)
```

MAgcs does **not** need ROS installed: it speaks the rosbridge JSON protocol over
a single WebSocket (DDS discovery over a phone hotspot is unreliable; one TCP
port is not). Foxglove is a second, read-only viewer for the same data.

## Topic contract

| Name | Type | Dir (from GCS) | Notes |
|---|---|---|---|
| `/gcs/heartbeat` | `mini_acrobat_interfaces/msg/Heartbeat` | publish | 20 Hz while connected |
| `/control/motor_setpoint` | `.../MotorSetpoint` | publish | 20 Hz **only while control output is enabled**. Temporary: once the control node exists, MAgcs should publish a higher-level command to it instead |
| `/safety/arm`, `/safety/disarm` | `std_srvs/srv/Trigger` | call | |
| `/safety/state` | `.../SafetyState` | subscribe | |
| `/motor_cmd` | `.../MotorCommand` | subscribe | what actually goes to the ESCs (plotted) |
| `/link/status` | `.../LinkStatus` | subscribe | drone Wi-Fi (RSSI, SSID, bitrate), IPv4 addresses, Tailscale IP; ~1 Hz from `mini_acrobat_link` |

The same names are whitelisted in `mini_acrobat_bringup/config/rosbridge.yaml`
(`topics_pub_glob`, `topics_sub_glob`, `services_glob`). Adding a topic to MAgcs
means adding it there too.

## GCS-side safety rules (implemented in `core/Drone.cpp`, covered by `tst_integration`)

- Heartbeat is sent from the **GUI thread** (as `safety.md` requires): if the GUI
  freezes, heartbeats stop and the drone disarms after `heartbeat_timeout_ms`.
  Keep the GUI thread light (the plot refreshes at 10 fps for this reason).
- Arm only by explicit click. The result (and a refusal reason) is shown.
- **DISARM** is always enabled while connected; `Space` is a global shortcut for it.
- Control output can only be enabled while the drone is **ARMED** and all
  throttles are **zero** (no jump on enable). It is published at 20 Hz only while enabled.
- Any transition to DISARMED (user, link lost, boot) or a lost connection
  zeroes the throttles and switches control output off.
- Throttle is clamped to a user limit (default 0.25) before it is sent.
- Closing the window sends a best-effort disarm; the heartbeat stopping disarms the drone anyway.

## Connecting (modes and diagnostics)

MAgcs opens a **Connect to Mini-Acrobat** window on start (and on Cmd+K). All
modes end in the same `ws://host:9090`; the mode only changes defaults, help
and diagnostics. The last mode/host is kept in `QSettings` (host per mode).

| Mode | Host | Diagnostics |
|---|---|---|
| Tailscale (VPN) | MagicDNS name (`mini-acrobat`) or 100.x IP | tailnet state and devices (`tailscale status --json`), path test every 5 s (`tailscale ping -c 1`): direct vs DERP relay + RTT |
| Manual | `raspberrypi.local` / IP | - |
| Simulator | `localhost` | - |

Always shown: this Mac's Wi-Fi (CoreWLAN: RSSI, noise, TX rate, channel; SSID
only with Location permission) and IPv4 addresses, the socket's actual drone IP
and local IP (tells which interface is used: Wi-Fi or Tailscale `utun`), RTT
and, once connected, the drone's own Wi-Fi and IPs from `/link/status`.

The `tailscale` CLI runs in `QProcess` (async, killed after 4 s), so the GUI
thread that sends the heartbeat never waits on it.

**Tailscale and the 300 ms watchdog.** A relayed (DERP) path adds tens to
hundreds of ms and jitter; MAgcs warns when the path is relayed or slower than
150 ms. For flying, keep the Mac and the drone on the same network so the path
is direct.

Drone setup (once, Ubuntu on the Pi):

```bash
curl -fsSL https://tailscale.com/install.sh | sh
sudo tailscale up --hostname=mini-acrobat
sudo apt install iw            # SSID / bitrate for /link/status
```

## Code layout (`gcs/src`)

| Path | Role |
|---|---|
| `core/RosbridgeProtocol` | JSON builders/parser (pure, unit-tested) |
| `core/RosbridgeClient` | QWebSocket client: reconnect, re-subscribe, service calls with timeout, RTT |
| `core/HeartbeatSender` | 20 Hz heartbeat |
| `core/Drone` | facade used by all widgets: state, arm/disarm, control input, connection target, `/link/status` |
| `core/ConnectionTarget` | mode + host + port, saved in `QSettings` |
| `core/Tailscale` | parsers for `tailscale status --json` / `tailscale ping` (unit-tested) |
| `core/NetworkMonitor` | polls this Mac's Wi-Fi, IPs and the Tailscale CLI |
| `platform/WifiInfo*` | Wi-Fi of this computer: CoreWLAN (macOS), `/proc/net/wireless` (Linux) |
| `widgets/ConnectDialog` | start-up "Connect to Mini-Acrobat" window |
| `widgets/*` | Connection (link + network dock), Safety, Motors, Plot (Qt Charts), Console |
| `MainWindow` | docks (`QDockWidget`), layout saved in `QSettings`, menus |

New feature = a new widget that only talks to `Drone`, plus a new topic in
`Topics.hpp` / `Drone` if it needs data.

## Packaging (macOS)

`gcs/scripts/build_app.sh` -> Release arm64 build, `cmake --install` into
`/Applications`, which runs Qt's deploy script (`macdeployqt`: Qt frameworks and
plugins inside the bundle) and `codesign --sign -` (ad-hoc; Apple Silicon needs a
valid signature after the binaries are rewritten). Bundle metadata:
`gcs/cmake/Info.plist.in` (id `pt.ulisboa.tecnico.isr.magcs`, icon
`resources/MAgcs.icns`, Location / local-network usage strings).

## Foxglove

Install the Foxglove app, *Open connection > Foxglove WebSocket*,
`ws://<drone>:8765`. The bridge is configured read-only (`foxglove.yaml`): no
publishing, no services, no parameters. Control stays in MAgcs.

## Known limits / to check on the real drone

- **rosbridge has no authentication**: anyone reaching port 9090 can arm the
  drone. Use a trusted network (hotspot with password, Tailscale) and/or set
  `address` in `rosbridge.yaml`. `rosapi` services stay reachable (rosbridge appends them).
- Not yet tested against a real rosbridge/Jazzy: the glob parameter format, the
  message type names (`pkg/msg/Type`) and the heartbeat jitter over the hotspot.
  Checklist (props off):
  - [ ] `ros2 param get /rosbridge_websocket topics_pub_glob` shows the configured list.
  - [ ] MAgcs connects and shows `/safety/state`; arm/disarm work.
  - [ ] *Safety > Simulate link loss* disarms in ~300 ms; re-arm needs a manual ARM.
  - [ ] Closing MAgcs or pulling the network disarms the drone.
  - [ ] Publishing to a topic outside the globs is refused.
  - [ ] Foxglove shows data and cannot publish or call services.
  - [ ] Connection dock shows the drone's Wi-Fi RSSI / SSID / IPs (`/link/status`, needs `iw`).
  - [ ] Tailscale mode: drone listed online, path test says *direct* with Mac and drone on the hotspot.
  - [ ] Heartbeat max-gap log on the hotspot (`safety_node` logs it every 5 s).
- TCP head-of-line blocking: after a stall, heartbeats arrive in a burst. The
  safety node latches the link loss and needs `min_heartbeats_to_arm` before a
  re-arm, so a burst cannot re-arm the drone by itself.
- Plain `QDockWidget` for now; Qt-Advanced-Docking-System can replace it later.
