# Safety node (`mini_acrobat_safety`)

The motors may only be armed while the link to the ground control station (GCS)
is alive. If the GCS heartbeat stops, the node disarms and the motors stop.

```
GCS (Qt) --rosbridge--> /gcs/heartbeat ----\
control node --> /control/motor_setpoint ---> safety_node --> /motor_cmd --> hardware node --> ESCs
GCS (Qt) --rosbridge--> /safety/arm|disarm -/        \--> /safety/state --> GCS / Foxglove
```

## Interfaces

| Name | Type | Dir | Notes |
|---|---|---|---|
| `/gcs/heartbeat` | `mini_acrobat_interfaces/msg/Heartbeat` | in | GCS, ~20 Hz |
| `/control/motor_setpoint` | `.../MotorSetpoint` | in | normalised -1..1 per motor |
| `/safety/arm`, `/safety/disarm` | `std_srvs/srv/Trigger` | srv | arm can be refused (see below) |
| `/motor_cmd` | `.../MotorCommand` | out | fixed rate (50 Hz); `armed` + clamped `throttle` |
| `/safety/state` | `.../SafetyState` | out | ~10 Hz and on every transition |

## State machine

- Boots `DISARMED` (reason `BOOT`).
- `DISARMED -> ARMED` on `/safety/arm`, only if: heartbeat fresh (< timeout), at
  least `min_heartbeats_to_arm` consecutive healthy heartbeats, and the latest
  setpoint (if fresh) is ~0.
- `ARMED -> DISARMED (LINK_LOST)` when no heartbeat for `heartbeat_timeout_ms`.
  Latched: when the link returns the node stays disarmed; re-arming is manual.
- `ARMED -> DISARMED (USER)` on `/safety/disarm`.
- While armed, a stale setpoint (> `setpoint_timeout_ms`) becomes zero throttle;
  the node stays armed (only the heartbeat disarms).
- Time is measured on the drone's monotonic clock at message arrival, never from
  message contents (GCS and drone clocks are not synchronised).

Parameters: `config/safety.yaml` (`heartbeat_timeout_ms` = 300 by default).
The node logs the largest heartbeat gap every 5 s, to tune the timeout on the
real link.

## Contract for the other nodes

**Hardware node** (only node that touches the PWM pins):
- `armed == false` -> emit **no PWM** (drive the pins low / keep a pull-down).
- `armed == true` -> start with neutral (1500 us) and then follow `throttle`
  (0.0 = neutral).
- Independent watchdog: if no `/motor_cmd` arrives for a short time, stop
  emitting PWM, even if the safety node is dead.

**GCS app**:
- Publish `/gcs/heartbeat` at ~20 Hz from a thread that is only alive while the
  control/UI loop is alive.
- Arm only on an explicit user action; show `/safety/state` and the arm result.
- rosbridge exposes everything by default: restrict it with `topics_glob` /
  `services_glob`; keep `foxglove_bridge` read-only (no `clientPublish`).

## Run / test (on the Pi, after sourcing ROS 2 Jazzy)

```bash
cd ros2_ws
colcon build --packages-select mini_acrobat_interfaces mini_acrobat_safety
colcon test  --packages-select mini_acrobat_safety && colcon test-result --verbose
source install/setup.bash
ros2 launch mini_acrobat_safety safety.launch.py
```

In other terminals:

```bash
ros2 topic pub --rate 20 /gcs/heartbeat mini_acrobat_interfaces/msg/Heartbeat "{seq: 0}"
ros2 service call /safety/arm std_srvs/srv/Trigger
ros2 topic echo /motor_cmd
ros2 topic echo /safety/state
# Ctrl+C the heartbeat publisher -> after ~300 ms: "LINK LOST ... DISARMED"
```

## Bench checklist (props off)

- [ ] Time from "PWM stopped" to motor stopped, per ESC (BLHeli_S, 3D mode).
- [ ] ESCs re-arm correctly after the signal returns at 1500 us.
- [ ] PWM pins are low (not floating) when disarmed and during Pi boot.
- [ ] Heartbeat max-gap log over the phone hotspot (5 GHz, Wi-Fi power save off).
