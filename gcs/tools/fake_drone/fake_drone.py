#!/usr/bin/env python3
"""Fake Mini-Acrobat drone: a rosbridge v2 WebSocket server that behaves like
mini_acrobat_safety (see docs/safety.md), so MAgcs can be developed and tested
on the Mac without the Pi or ROS.

    pip install websockets
    python3 fake_drone.py --port 9090

It implements only what MAgcs uses: subscribe / advertise / publish /
call_service for the topics of docs/safety.md, plus a fake /link/status. It is NOT the real safety node:
always re-test on the drone (props off).

For the panels (Topics, Plot, Raw, Publish, Fly mode) it also fakes:
  - rosapi: /rosapi/topics and /rosapi/message_details (a few types);
  - /imu/data (sensor_msgs/Imu-like sines, 50 Hz) and /battery (1 Hz);
  - /control/cmd (geometry_msgs/Twist from Fly mode) integrated into
    /fake/pose (x, y, theta) while ARMED, 20 Hz;
  - any other topic a client publishes is relayed to its subscribers, like ROS.

Simulated flight (only while ARMED; the app's Fly mode takes priority):
    python3 fake_drone.py --scenario forward --level 0.4
    scenarios: forward, circle, square, sine, step   (--hover adds a base throttle)
  or change it at runtime from the app's Publish panel: topic /fake/scenario,
  type std_msgs/msg/String, message {"data": "circle 0.5"}  ("off" to stop).
  A fake mixer turns the command into /motor_cmd throttles (motors at
  0/120/240 deg, like the drawing) so the motor plots move. It is NOT the
  drone's real mixer.
"""
import math
import argparse
import asyncio
import json
import random
import sys
import time

try:  # websockets >= 13
    from websockets.asyncio.server import serve
except ImportError:  # older releases
    from websockets import serve

HEARTBEAT = "/gcs/heartbeat"
SETPOINT = "/control/motor_setpoint"
STATE = "/safety/state"
MOTOR_CMD = "/motor_cmd"
LINK_STATUS = "/link/status"
FLY_CMD = "/control/cmd"
IMU = "/imu/data"
BATTERY = "/battery"
POSE = "/fake/pose"
SCENARIO = "/fake/scenario"
SCENARIOS = ("forward", "circle", "square", "sine", "step")
MOTOR_ANGLES = (0.0, 2 * math.pi / 3, 4 * math.pi / 3)  # motor 1 at the front (+x)


def scenario_cmd(name, level, t):
    """(vx, vy, wz) of a scripted manoeuvre, t = seconds since it started."""
    if name == "forward":
        return level, 0.0, 0.0
    if name == "circle":
        return level, 0.0, 0.5 * level
    if name == "sine":
        return level * math.sin(2 * math.pi * t / 6.0), 0.0, 0.0
    if name == "step":
        return (level if (t % 4.0) < 2.0 else 0.0), 0.0, 0.0
    if name == "square":
        turn = (math.pi / 2) / (2.0 * max(level, 0.05))  # pose turns at 2*wz rad/s
        t = t % (3.0 + turn)
        return (level, 0.0, 0.0) if t < 3.0 else (0.0, 0.0, level)
    return 0.0, 0.0, 0.0


def fake_mix(vx, vy, wz, hover):
    """Command -> 3 throttles. Thrust towards (vx, vy) comes from the motors
    behind that direction; yaw adds the same amount to all three."""
    out = []
    for a in MOTOR_ANGLES:
        push = -(vx * math.cos(a) + vy * math.sin(a))
        out.append(max(-1.0, min(1.0, hover + 0.6 * push + 0.3 * wz)))
    return out

TOPIC_TYPES = {
    HEARTBEAT: "mini_acrobat_interfaces/msg/Heartbeat",
    SETPOINT: "mini_acrobat_interfaces/msg/MotorSetpoint",
    STATE: "mini_acrobat_interfaces/msg/SafetyState",
    MOTOR_CMD: "mini_acrobat_interfaces/msg/MotorCommand",
    LINK_STATUS: "mini_acrobat_interfaces/msg/LinkStatus",
    FLY_CMD: "geometry_msgs/msg/Twist",
    IMU: "sensor_msgs/msg/Imu",
    BATTERY: "sensor_msgs/msg/BatteryState",
    POSE: "geometry_msgs/msg/Pose2D",
    SCENARIO: "std_msgs/msg/String",
    "/rosout": "rcl_interfaces/msg/Log",
}

# Minimal rosapi/message_details answers (same shape as rosapi).
def _td(t, names, types, lens=None):
    return {"type": t, "fieldnames": names, "fieldtypes": types,
            "fieldarraylen": lens or [-1] * len(names), "examples": [], "constnames": [], "constvalues": []}

TYPEDEFS = {
    "geometry_msgs/msg/Twist": [
        _td("geometry_msgs/Twist", ["linear", "angular"], ["geometry_msgs/Vector3", "geometry_msgs/Vector3"]),
        _td("geometry_msgs/Vector3", ["x", "y", "z"], ["float64"] * 3)],
    "geometry_msgs/msg/Pose2D": [_td("geometry_msgs/Pose2D", ["x", "y", "theta"], ["float64"] * 3)],
    "std_msgs/msg/Float64": [_td("std_msgs/Float64", ["data"], ["float64"])],
    "std_msgs/msg/Bool": [_td("std_msgs/Bool", ["data"], ["bool"])],
    "std_msgs/msg/String": [_td("std_msgs/String", ["data"], ["string"])],
    "mini_acrobat_interfaces/msg/MotorSetpoint": [
        _td("mini_acrobat_interfaces/MotorSetpoint", ["throttle"], ["float32"], [3])],
}

DISARMED, ARMED = 0, 1
R_NONE, R_BOOT, R_USER, R_LINK_LOST = 0, 1, 2, 3


class Safety:
    """Python twin of SafetyLogic (default parameters of safety.yaml)."""

    def __init__(self, hb_timeout_ms=300, sp_timeout_ms=300, min_hb=5, zero_thr=0.02):
        self.hb_timeout = hb_timeout_ms / 1000.0
        self.sp_timeout = sp_timeout_ms / 1000.0
        self.min_hb = min_hb
        self.zero_thr = zero_thr
        self.state = DISARMED
        self.reason = R_BOOT
        self.last_hb = None
        self.consecutive = 0
        self.last_seq = 0
        self.max_gap_ms = 0.0
        self.window_max_gap_ms = 0.0
        self.setpoint = [0.0, 0.0, 0.0]
        self.last_sp = None

    def on_heartbeat(self, seq):
        now = time.monotonic()
        if self.last_hb is None or now - self.last_hb > self.hb_timeout:
            self.consecutive = 1
        else:
            self.consecutive += 1
        if self.last_hb is not None:
            self.max_gap_ms = max(self.max_gap_ms, (now - self.last_hb) * 1000.0)
        self.last_hb = now
        self.last_seq = seq

    def on_setpoint(self, throttle):
        self.setpoint = [float(x) for x in throttle][:3]
        self.last_sp = time.monotonic()

    def link_ok(self):
        return self.last_hb is not None and time.monotonic() - self.last_hb < self.hb_timeout

    def hb_age_ms(self):
        return -1.0 if self.last_hb is None else (time.monotonic() - self.last_hb) * 1000.0

    def sp_fresh(self):
        return self.last_sp is not None and time.monotonic() - self.last_sp < self.sp_timeout

    def tick(self):
        if self.state == ARMED and not self.link_ok():
            self.state, self.reason = DISARMED, R_LINK_LOST  # latched
        if self.state == ARMED:
            thr = self.setpoint if self.sp_fresh() else [0.0, 0.0, 0.0]
            return True, [max(-1.0, min(1.0, x)) for x in thr]
        return False, [0.0, 0.0, 0.0]

    def arm(self):
        if not self.link_ok():
            return False, "refused: no fresh heartbeat"
        if self.consecutive < self.min_hb:
            return False, f"refused: need {self.min_hb} healthy heartbeats ({self.consecutive} so far)"
        if self.sp_fresh() and any(abs(x) > self.zero_thr for x in self.setpoint):
            return False, "refused: motor setpoint is not zero"
        self.state, self.reason = ARMED, R_NONE
        return True, "armed"

    def disarm(self):
        self.state, self.reason = DISARMED, R_USER
        return True, "disarmed"

    def state_msg(self):
        return {
            "state": self.state,
            "reason": self.reason,
            "link_ok": self.link_ok(),
            "heartbeat_age_ms": self.hb_age_ms(),
            "heartbeat_max_gap_ms": self.window_max_gap_ms,
            "last_heartbeat_seq": self.last_seq,
        }


class FakeLink:
    """Pretend drone-side network status (what mini_acrobat_link publishes)."""

    def __init__(self):
        self.rssi = -58.0

    def msg(self):
        self.rssi = max(-82.0, min(-45.0, self.rssi + random.uniform(-2.0, 2.0)))
        return {
            "hostname": "fake-drone",
            "wifi_interface": "wlan0",
            "wifi_connected": True,
            "ssid": "FakeHotspot",
            "bssid": "02:00:00:00:00:01",
            "rssi_dbm": int(round(self.rssi)),
            "noise_dbm": 0,
            "frequency_mhz": 2437,
            "tx_bitrate_mbps": 72.2,
            "rx_bitrate_mbps": 65.0,
            "addresses": ["wlan0 172.20.10.3/28", "tailscale0 100.101.102.103/32"],
            "tailscale_up": True,
            "tailscale_ip": "100.101.102.103",
        }


class Server:
    def __init__(self, safety, verbose):
        self.safety = safety
        self.verbose = verbose
        self.clients = {}  # ws -> set(subscribed topics)
        self.link = FakeLink()
        self.extra_types = {}  # topics advertised by clients
        self.fly = (0.0, 0.0, 0.0)
        self.fly_t = None
        self.pose = [0.0, 0.0, 0.0]
        self.scenario = None  # (name, level)
        self.scenario_t0 = None
        self.hover = 0.0

    def set_scenario(self, text):
        parts = (text or "").split()
        if not parts or parts[0] in ("off", "stop", "none"):
            self.scenario = None
        elif parts[0] in SCENARIOS:
            level = float(parts[1]) if len(parts) > 1 else 0.3
            self.scenario = (parts[0], max(-1.0, min(1.0, level)))
        else:
            print(f"unknown scenario {text!r} (use {', '.join(SCENARIOS)} or off)", file=sys.stderr, flush=True)
            return
        self.scenario_t0 = None
        print(f"scenario: {self.scenario or 'off'}", file=sys.stderr, flush=True)

    async def send(self, ws, obj):
        try:
            await ws.send(json.dumps(obj))
        except Exception:
            pass

    async def broadcast(self, topic, msg):
        for ws, topics in list(self.clients.items()):
            if topic in topics:
                await self.send(ws, {"op": "publish", "topic": topic, "msg": msg})

    async def handler(self, ws):
        self.clients[ws] = set()
        if self.verbose:
            print("client connected", file=sys.stderr, flush=True)
        try:
            async for raw in ws:
                try:
                    m = json.loads(raw)
                except ValueError:
                    continue
                await self.handle(ws, m)
        except Exception:
            pass
        finally:
            self.clients.pop(ws, None)
            if self.verbose:
                print("client disconnected", file=sys.stderr, flush=True)

    async def handle(self, ws, m):
        op = m.get("op")
        if op == "subscribe":
            self.clients[ws].add(m.get("topic"))
        elif op == "unsubscribe":
            self.clients[ws].discard(m.get("topic"))
        elif op == "publish":
            topic, msg = m.get("topic"), m.get("msg", {})
            if topic == HEARTBEAT:
                self.safety.on_heartbeat(int(msg.get("seq", 0)))
            elif topic == SETPOINT:
                self.safety.on_setpoint(msg.get("throttle", [0, 0, 0]))
            elif topic == SCENARIO:
                self.set_scenario(str(msg.get("data", "")))
            elif topic == FLY_CMD:
                lin, ang = msg.get("linear", {}), msg.get("angular", {})
                self.fly = (float(lin.get("x", 0)), float(lin.get("y", 0)), float(ang.get("z", 0)))
                self.fly_t = time.monotonic()
            if topic not in (HEARTBEAT,):
                await self.broadcast(topic, msg)  # relay, like ROS
        elif op == "advertise":
            if m.get("topic") and m.get("type"):
                self.extra_types[m["topic"]] = m["type"]
        elif op == "call_service":
            svc = m.get("service")
            if svc == "/rosapi/topics":
                types = dict(TOPIC_TYPES)
                types.update(self.extra_types)
                names = sorted(types)
                await self.send(ws, {"op": "service_response", "service": svc, "id": m.get("id"), "result": True,
                                     "values": {"topics": names, "types": [types[n] for n in names]}})
                return
            if svc == "/rosapi/message_details":
                t = (m.get("args") or {}).get("type", "")
                defs = TYPEDEFS.get(t)
                if defs is None:
                    await self.send(ws, {"op": "service_response", "service": svc, "id": m.get("id"),
                                         "result": False, "values": f"unknown type {t}"})
                else:
                    await self.send(ws, {"op": "service_response", "service": svc, "id": m.get("id"),
                                         "result": True, "values": {"typedefs": defs}})
                return
            if svc == "/safety/arm":
                ok, text = self.safety.arm()
            elif svc == "/safety/disarm":
                ok, text = self.safety.disarm()
            else:
                await self.send(ws, {"op": "service_response", "service": svc, "id": m.get("id"),
                                     "result": False, "values": "unknown service"})
                return
            await self.send(ws, {"op": "service_response", "service": svc, "id": m.get("id"),
                                 "result": True, "values": {"success": ok, "message": text}})
            if self.verbose:
                print(f"{svc}: {ok} ({text})", file=sys.stderr, flush=True)
            await self.broadcast(STATE, self.safety.state_msg())
        # unadvertise: nothing to do

    async def ticker(self):
        """50 Hz /motor_cmd, ~10 Hz /safety/state and on every transition, 1 Hz /link/status."""
        last_key = None
        last_state_t = 0.0
        last_link_t = 0.0
        last_window_t = time.monotonic()
        last_pose_t = 0.0
        t0 = time.monotonic()
        while True:
            await asyncio.sleep(0.02)
            now = time.monotonic()
            armed, thr = self.safety.tick()
            # Effective planar command: app Fly mode first, then the scenario.
            fly_fresh = self.fly_t is not None and now - self.fly_t < 0.3
            cmd = None
            if armed and fly_fresh:
                cmd = self.fly
            elif armed and self.scenario:
                if self.scenario_t0 is None:
                    self.scenario_t0 = now
                cmd = scenario_cmd(self.scenario[0], self.scenario[1], now - self.scenario_t0)
            elif not armed:
                self.scenario_t0 = None  # restart the manoeuvre on the next arm
            if cmd is not None and not self.safety.sp_fresh():
                thr = fake_mix(*cmd, self.hover)  # no direct motor setpoints: the mixer drives
            await self.broadcast(MOTOR_CMD, {"armed": armed, "throttle": thr})
            if now - last_window_t >= 1.0:
                self.safety.window_max_gap_ms = self.safety.max_gap_ms
                self.safety.max_gap_ms = 0.0
                last_window_t = now
            key = (self.safety.state, self.safety.reason, self.safety.link_ok())
            if key != last_key or now - last_state_t >= 0.1:
                last_key, last_state_t = key, now
                await self.broadcast(STATE, self.safety.state_msg())
            if now - last_link_t >= 1.0:
                last_link_t = now
                await self.broadcast(LINK_STATUS, self.link.msg())
                await self.broadcast(BATTERY, {"voltage": 11.8 - 0.0005 * (now - t0) + random.uniform(-0.02, 0.02),
                                               "current": -2.0 + random.uniform(-0.1, 0.1),
                                               "percentage": max(0.0, 0.92 - 0.0001 * (now - t0)),
                                               "present": True})
            # IMU: slow wobble + noise, plus the motors' effect when armed
            tt = now - t0
            await self.broadcast(IMU, {
                "header": {"stamp": {"sec": int(tt), "nanosec": int((tt % 1) * 1e9)}, "frame_id": "imu"},
                "orientation": {"x": 0.0, "y": 0.0, "z": math.sin(self.pose[2] / 2), "w": math.cos(self.pose[2] / 2)},
                "angular_velocity": {"x": 0.05 * math.sin(tt * 1.3) + random.gauss(0, 0.01),
                                     "y": 0.04 * math.cos(tt * 0.9) + random.gauss(0, 0.01),
                                     "z": (2.0 * cmd[2] if cmd else 0.0) + random.gauss(0, 0.01)},
                "linear_acceleration": {"x": (1.5 * cmd[0] if cmd else 0.0) + random.gauss(0, 0.05),
                                        "y": 0.1 * math.sin(tt * 0.7) + random.gauss(0, 0.05),
                                        "z": 9.81 + random.gauss(0, 0.05)},
            })
            # Fly command -> fake planar pose (only while armed and fresh)
            if now - last_pose_t >= 0.05:
                dt = now - last_pose_t if last_pose_t else 0.05
                last_pose_t = now
                if cmd is not None:
                    vx, vy, wz = cmd
                    th = self.pose[2]
                    self.pose[0] += (vx * math.cos(th) - vy * math.sin(th)) * dt
                    self.pose[1] += (vx * math.sin(th) + vy * math.cos(th)) * dt
                    self.pose[2] = (th + wz * 2.0 * dt + math.pi) % (2 * math.pi) - math.pi
                await self.broadcast(POSE, {"x": self.pose[0], "y": self.pose[1], "theta": self.pose[2]})


async def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=9090, help="0 = pick a free port")
    ap.add_argument("--heartbeat-timeout-ms", type=int, default=300)
    ap.add_argument("--scenario", choices=SCENARIOS, help="simulated manoeuvre while ARMED")
    ap.add_argument("--level", type=float, default=0.3, help="scenario command level, -1..1 (default 0.3)")
    ap.add_argument("--hover", type=float, default=0.0, help="base throttle added to every motor by the fake mixer")
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()

    server = Server(Safety(hb_timeout_ms=args.heartbeat_timeout_ms), args.verbose)
    server.hover = max(-1.0, min(1.0, args.hover))
    if args.scenario:
        server.set_scenario(f"{args.scenario} {args.level}")
    async with serve(server.handler, args.host, args.port) as srv:
        port = list(srv.sockets)[0].getsockname()[1]
        print(f"READY {port}", flush=True)
        await server.ticker()


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass
