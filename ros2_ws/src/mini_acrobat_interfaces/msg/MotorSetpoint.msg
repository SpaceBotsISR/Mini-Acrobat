# Output of the control node, input of the safety node (/control/motor_setpoint).
# Normalised per-motor throttle, bidirectional (the 3 ESCs run in 3D mode).
#   -1.0 = full reverse, 0.0 = stop, +1.0 = full forward
float32[3] throttle   # ESC1, ESC2, ESC3
