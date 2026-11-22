# Output of the safety node, input of the hardware node (/motor_cmd).
# Published at a fixed rate; the hardware node treats a missing stream as a
# failure and stops emitting PWM by itself.
#   armed == false -> the hardware node must NOT emit any PWM signal
#   armed == true  -> emit PWM from `throttle` (0.0 maps to the ESC neutral)
bool armed
float32[3] throttle   # -1.0 .. 1.0, already clamped by the safety node
