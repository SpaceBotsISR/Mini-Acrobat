# Sent by the ground control station (GCS) at ~20 Hz on /gcs/heartbeat.
# The safety node measures arrival time on the drone's own monotonic clock,
# so no timestamp is needed (the GCS and the drone clocks are not synchronised).
uint32 seq   # increments by 1 on every heartbeat
