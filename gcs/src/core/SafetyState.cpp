#include "core/SafetyState.hpp"

#include <QJsonArray>

namespace magcs {

QString SafetyState::stateText() const
{
  if (!valid) {
    return QStringLiteral("NO DATA");
  }
  return state == kStateArmed ? QStringLiteral("ARMED") : QStringLiteral("DISARMED");
}

QString SafetyState::reasonText() const
{
  if (!valid) {
    return QStringLiteral("-");
  }
  switch (reason) {
    case kReasonNone: return QStringLiteral("-");
    case kReasonBoot: return QStringLiteral("boot");
    case kReasonUser: return QStringLiteral("user request");
    case kReasonLinkLost: return QStringLiteral("LINK LOST");
    default: return QStringLiteral("unknown (%1)").arg(reason);
  }
}

SafetyState SafetyState::fromJson(const QJsonObject& msg)
{
  SafetyState s;
  s.valid = msg.contains(QStringLiteral("state"));
  s.state = msg.value(QStringLiteral("state")).toInt(kStateDisarmed);
  s.reason = msg.value(QStringLiteral("reason")).toInt(kReasonNone);
  s.linkOk = msg.value(QStringLiteral("link_ok")).toBool(false);
  s.heartbeatAgeMs = msg.value(QStringLiteral("heartbeat_age_ms")).toDouble(-1.0);
  s.heartbeatMaxGapMs = msg.value(QStringLiteral("heartbeat_max_gap_ms")).toDouble(0.0);
  s.lastHeartbeatSeq = static_cast<quint32>(
    msg.value(QStringLiteral("last_heartbeat_seq")).toDouble(0.0));
  return s;
}

MotorCmd MotorCmd::fromJson(const QJsonObject& msg)
{
  MotorCmd c;
  c.armed = msg.value(QStringLiteral("armed")).toBool(false);
  const QJsonArray a = msg.value(QStringLiteral("throttle")).toArray();
  for (int i = 0; i < 3 && i < a.size(); ++i) {
    c.throttle[i] = a.at(i).toDouble(0.0);
  }
  return c;
}

}  // namespace magcs
