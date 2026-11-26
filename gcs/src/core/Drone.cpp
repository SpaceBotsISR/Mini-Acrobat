#include "core/Drone.hpp"

#include <QDateTime>
#include <QJsonArray>
#include <QUrl>
#include <algorithm>
#include <cmath>

#include "core/Topics.hpp"

namespace magcs {

Drone::Drone(QObject* parent)
: QObject(parent), m_heartbeat(&m_client)
{
  qRegisterMetaType<magcs::SafetyState>("magcs::SafetyState");
  qRegisterMetaType<magcs::MotorCmd>("magcs::MotorCmd");
  qRegisterMetaType<magcs::DroneLinkStatus>("magcs::DroneLinkStatus");

  m_client.advertise(topics::kMotorSetpoint, topics::kMotorSetpointType);
  m_client.subscribe(topics::kSafetyState, topics::kSafetyStateType);
  m_client.subscribe(topics::kMotorCmd, topics::kMotorCmdType);
  m_client.subscribe(topics::kLinkStatus, topics::kLinkStatusType);

  connect(&m_client, &RosbridgeClient::topicMessage, this, &Drone::onTopic);
  connect(&m_client, &RosbridgeClient::stateChanged, this, &Drone::onClientState);
  connect(&m_client, &RosbridgeClient::logMessage, this, &Drone::log);

  m_setpointTimer.setTimerType(Qt::PreciseTimer);
  m_setpointTimer.setInterval(50);  // 20 Hz
  connect(&m_setpointTimer, &QTimer::timeout, this, &Drone::publishSetpoint);
}

void Drone::connectTo(const ConnectionTarget& target)
{
  m_target = target;
  m_target.save();
  emit targetChanged();
  emit log(QStringLiteral("connecting to %1 (%2)")
             .arg(m_target.url(), ConnectionTarget::modeName(m_target.mode)));
  connectToHost(m_target.effectiveHost(), m_target.port);
}

void Drone::setTarget(const ConnectionTarget& target)
{
  m_target = target;
  emit targetChanged();
}

qint64 Drone::linkStatusAgeMs() const
{
  return m_linkStatusAt == 0 ? -1 : QDateTime::currentMSecsSinceEpoch() - m_linkStatusAt;
}

void Drone::connectToHost(const QString& host, quint16 port)
{
  QUrl url;
  url.setScheme(QStringLiteral("ws"));
  url.setHost(host);
  url.setPort(port);
  m_client.open(url);
}

void Drone::disconnectFromHost()
{
  setControlEnabled(false);
  m_client.close();
}

void Drone::arm()
{
  callSafety(topics::kArmService, true);
}

void Drone::disarm()
{
  // Stop sending throttle first, whatever the answer will be.
  zeroThrottle();
  setControlEnabled(false);
  callSafety(topics::kDisarmService, false);
}

void Drone::callSafety(const QString& service, bool isArm)
{
  m_client.callService(service, {}, [this, isArm](bool ok, const QJsonValue& values, const QString& error) {
    QString message = error;
    bool success = false;
    if (ok) {
      const QJsonObject v = values.toObject();
      success = v.value(QStringLiteral("success")).toBool(false);
      message = v.value(QStringLiteral("message")).toString();
    }
    if (isArm) {
      emit armResult(success, message);
    } else {
      emit disarmResult(success, message);
    }
    emit log(QStringLiteral("%1: %2 (%3)")
               .arg(isArm ? QStringLiteral("arm") : QStringLiteral("disarm"),
                    success ? QStringLiteral("ok") : QStringLiteral("REFUSED"), message));
  });
}

bool Drone::setControlEnabled(bool on)
{
  if (on == m_controlEnabled) {
    return true;
  }
  if (on) {
    if (!isConnected()) {
      emit log(QStringLiteral("control output refused: not connected"));
      return false;
    }
    if (!m_safety.armed()) {
      emit log(QStringLiteral("control output refused: drone is not ARMED"));
      return false;
    }
    const bool allZero = std::all_of(m_throttle.begin(), m_throttle.end(),
                                     [](double v) {return std::abs(v) < 1e-3;});
    if (!allZero) {
      emit log(QStringLiteral("control output refused: set all throttles to zero first"));
      return false;
    }
  }
  m_controlEnabled = on;
  if (on) {
    m_setpointTimer.start();
    publishSetpoint();
  } else {
    m_setpointTimer.stop();
    // One last explicit zero, so the drone does not wait for the stale timeout.
    m_client.publish(topics::kMotorSetpoint, {{"throttle", QJsonArray{0.0, 0.0, 0.0}}});
  }
  emit controlEnabledChanged(on);
  return true;
}

void Drone::setThrottle(int motor, double value)
{
  if (motor < 0 || motor > 2) {
    return;
  }
  value = std::clamp(value, -1.0, 1.0);
  if (m_throttle[motor] == value) {
    return;
  }
  m_throttle[motor] = value;
  emit throttleChanged();
}

void Drone::zeroThrottle()
{
  const bool changed = std::any_of(m_throttle.begin(), m_throttle.end(),
                                   [](double v) {return v != 0.0;});
  m_throttle = {0.0, 0.0, 0.0};
  if (changed) {
    emit throttleChanged();
  }
}

void Drone::setThrottleLimit(double limit)
{
  m_limit = std::clamp(limit, 0.0, 1.0);
}

void Drone::publishSetpoint()
{
  QJsonArray a;
  for (double v : m_throttle) {
    a.append(std::clamp(v, -m_limit, m_limit));
  }
  m_client.publish(topics::kMotorSetpoint, {{"throttle", a}});
}

void Drone::onTopic(const QString& topic, const QJsonObject& msg)
{
  if (topic == topics::kSafetyState) {
    const bool wasArmed = m_safety.armed();
    m_safety = SafetyState::fromJson(msg);
    if (wasArmed && !m_safety.armed()) {
      emit log(QStringLiteral("DISARMED (%1)").arg(m_safety.reasonText()));
    }
    if (!m_safety.armed()) {
      // Never keep a throttle around while disarmed.
      zeroThrottle();
      setControlEnabled(false);
    }
    emit safetyChanged(m_safety);
  } else if (topic == topics::kMotorCmd) {
    m_motorCmd = MotorCmd::fromJson(msg);
    emit motorCmdReceived(m_motorCmd);
  } else if (topic == topics::kLinkStatus) {
    m_linkStatus = DroneLinkStatus::fromJson(msg);
    m_linkStatusAt = QDateTime::currentMSecsSinceEpoch();
    emit linkStatusReceived(m_linkStatus);
  }
}

void Drone::onClientState(RosbridgeClient::State s)
{
  const bool connected = (s == RosbridgeClient::State::Connected);
  if (!connected) {
    m_setpointTimer.stop();
    if (m_controlEnabled) {
      m_controlEnabled = false;
      emit controlEnabledChanged(false);
    }
    zeroThrottle();
    m_safety = SafetyState{};
    m_motorCmd = MotorCmd{};
    m_linkStatus = DroneLinkStatus{};
    m_linkStatusAt = 0;
    emit safetyChanged(m_safety);
    emit linkStatusReceived(m_linkStatus);
    emit motorCmdReceived(m_motorCmd);
  }
  emit connectionChanged(connected);
}

}  // namespace magcs
