# Published by the safety node on /safety/state (~10 Hz and on every transition).
uint8 STATE_DISARMED=0
uint8 STATE_ARMED=1

uint8 REASON_NONE=0
uint8 REASON_BOOT=1        # disarmed since power-up
uint8 REASON_USER=2        # disarm requested through /safety/disarm
uint8 REASON_LINK_LOST=3   # GCS heartbeat timed out

uint8 state
uint8 reason               # why the node is DISARMED (REASON_NONE while ARMED)
bool link_ok               # heartbeat currently fresh
float32 heartbeat_age_ms   # time since last heartbeat; -1 if none received yet
float32 heartbeat_max_gap_ms  # largest heartbeat gap seen in the last second
uint32 last_heartbeat_seq
