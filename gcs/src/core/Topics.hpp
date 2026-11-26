#pragma once
// Topic / service names and message types shared with the drone.
// Keep in sync with docs/safety.md and mini_acrobat_interfaces.
#include <QString>

namespace magcs::topics {

// GCS -> drone
inline const QString kHeartbeat = QStringLiteral("/gcs/heartbeat");
inline const QString kHeartbeatType = QStringLiteral("mini_acrobat_interfaces/msg/Heartbeat");

// Manual control input. Until the control node exists the GCS publishes the
// setpoint directly; the safety node still clamps it and zeroes it when stale.
inline const QString kMotorSetpoint = QStringLiteral("/control/motor_setpoint");
inline const QString kMotorSetpointType = QStringLiteral("mini_acrobat_interfaces/msg/MotorSetpoint");

// Fly / test mode: desired planar motion (x forward, y left, yaw CCW), normalised
// to -1..1. No node consumes it yet; the drone-side mixer comes later.
inline const QString kFlyCmd = QStringLiteral("/control/cmd");
inline const QString kFlyCmdType = QStringLiteral("geometry_msgs/msg/Twist");

// rosapi (started by rosbridge_websocket_launch): topic list and message layouts.
inline const QString kRosapiTopics = QStringLiteral("/rosapi/topics");
inline const QString kRosapiMessageDetails = QStringLiteral("/rosapi/message_details");

inline const QString kArmService = QStringLiteral("/safety/arm");
inline const QString kDisarmService = QStringLiteral("/safety/disarm");

// drone -> GCS
inline const QString kSafetyState = QStringLiteral("/safety/state");
inline const QString kSafetyStateType = QStringLiteral("mini_acrobat_interfaces/msg/SafetyState");
inline const QString kMotorCmd = QStringLiteral("/motor_cmd");
inline const QString kMotorCmdType = QStringLiteral("mini_acrobat_interfaces/msg/MotorCommand");
// Drone-side network status (mini_acrobat_link, ~1 Hz).
inline const QString kLinkStatus = QStringLiteral("/link/status");
inline const QString kLinkStatusType = QStringLiteral("mini_acrobat_interfaces/msg/LinkStatus");

}  // namespace magcs::topics
