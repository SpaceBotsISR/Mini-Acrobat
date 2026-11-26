#!/usr/bin/env python3
"""Fake Mini-Acrobat drone: a rosbridge v2 WebSocket server that behaves like
mini_acrobat_safety (see docs/safety.md), so MAgcs can be developed and tested
on the Mac without the Pi or ROS.

    pip install websockets
    python3 fake_drone.py --port 9090

It implements only what MAgcs uses: subscribe / advertise / publish /
call_service for the topics of docs/safety.md, plus a fake /link/status. It is NOT the real safety node:
always re-test on the drone (props off).
"""
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
        elif op == "call_service":
            svc = m.get("service")
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
        # advertise / unadvertise: nothing to do

    async def ticker(self):
        """50 Hz /motor_cmd, ~10 Hz /safety/state and on every transition, 1 Hz /link/status."""
        last_key = None
        last_state_t = 0.0
        last_link_t = 0.0
        last_window_t = time.monotonic()
        while True:
            await asyncio.sleep(0.02)
            now = time.monotonic()
            armed, thr = self.safety.tick()
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


async def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=9090, help="0 = pick a free port")
    ap.add_argument("--heartbeat-timeout-ms", type=int, default=300)
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()

    server = Server(Safety(hb_timeout_ms=args.heartbeat_timeout_ms), args.verbose)
    async with serve(server.handler, args.host, args.port) as srv:
        port = list(srv.sockets)[0].getsockname()[1]
        print(f"READY {port}", flush=True)
        await server.ticker()


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass
