#pragma once
#include <QObject>
#include <QString>
#include <QTimer>
#include <array>

#include "core/ConnectionTarget.hpp"
#include "core/HeartbeatSender.hpp"
#include "core/LinkStatus.hpp"
#include "core/RosbridgeClient.hpp"
#include "core/SafetyState.hpp"

namespace magcs {

// Everything the GUI knows about the drone: connection, safety state, motor
// output and the manual control input. Widgets only talk to this class.
//
// Control-input rules (the GCS side of the safety contract):
//  - the setpoint is only published while "control output" is enabled (20 Hz);
//  - it can only be enabled while the drone is ARMED and all throttles are zero
//    (no jump on enable);
//  - any transition to DISARMED, or a lost connection, zeroes the throttles and
//    switches control output off;
//  - all outputs are clamped to +-throttleLimit.
class Drone : public QObject {
  Q_OBJECT
public:
  explicit Drone(QObject* parent = nullptr);

  RosbridgeClient* client() { return &m_client; }
  HeartbeatSender* heartbeat() { return &m_heartbeat; }

  // Saves the target (QSettings) and connects to it.
  void connectTo(const ConnectionTarget& target);
  const ConnectionTarget& target() const { return m_target; }
  void setTarget(const ConnectionTarget& target);  // no connect, no save
  void connectToHost(const QString& host, quint16 port = 9090);
  void disconnectFromHost();
  bool isConnected() const { return m_client.isConnected(); }

  const SafetyState& safety() const { return m_safety; }
  const MotorCmd& motorCmd() const { return m_motorCmd; }
  // Last /link/status and its age (-1 if none since connecting).
  const DroneLinkStatus& linkStatus() const { return m_linkStatus; }
  qint64 linkStatusAgeMs() const;

  // Safety services. The result is reported through armResult / disarmResult.
  void arm();
  void disarm();

  // Manual control input.
  bool controlEnabled() const { return m_controlEnabled; }
  bool setControlEnabled(bool on);  // false if refused (not armed / throttles not zero)
  void setThrottle(int motor, double value);  // -1..1
  void zeroThrottle();
  std::array<double, 3> throttle() const { return m_throttle; }
  void setThrottleLimit(double limit);
  double throttleLimit() const { return m_limit; }

signals:
  void connectionChanged(bool connected);
  void safetyChanged(const magcs::SafetyState& state);
  void motorCmdReceived(const magcs::MotorCmd& cmd);
  void linkStatusReceived(const magcs::DroneLinkStatus& status);
  void targetChanged();
  void armResult(bool ok, const QString& message);
  void disarmResult(bool ok, const QString& message);
  void controlEnabledChanged(bool on);
  void throttleChanged();
  void log(const QString& text);

private:
  void onTopic(const QString& topic, const QJsonObject& msg);
  void onClientState(RosbridgeClient::State s);
  void publishSetpoint();
  void callSafety(const QString& service, bool isArm);

  RosbridgeClient m_client;
  HeartbeatSender m_heartbeat;
  QTimer m_setpointTimer;
  SafetyState m_safety;
  MotorCmd m_motorCmd;
  DroneLinkStatus m_linkStatus;
  qint64 m_linkStatusAt = 0;  // QDateTime ms, 0 = never
  ConnectionTarget m_target;
  bool m_controlEnabled = false;
  std::array<double, 3> m_throttle{0.0, 0.0, 0.0};
  double m_limit = 0.25;
};

}  // namespace magcs
