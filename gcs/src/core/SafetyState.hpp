#pragma once
#include <QJsonObject>
#include <QMetaType>
#include <QString>

namespace magcs {

// Mirror of mini_acrobat_interfaces/msg/SafetyState.
struct SafetyState {
  static constexpr int kStateDisarmed = 0;
  static constexpr int kStateArmed = 1;
  static constexpr int kReasonNone = 0;
  static constexpr int kReasonBoot = 1;
  static constexpr int kReasonUser = 2;
  static constexpr int kReasonLinkLost = 3;

  bool valid = false;  // false until the first /safety/state arrives
  int state = kStateDisarmed;
  int reason = kReasonBoot;
  bool linkOk = false;
  double heartbeatAgeMs = -1.0;
  double heartbeatMaxGapMs = 0.0;
  quint32 lastHeartbeatSeq = 0;

  bool armed() const { return valid && state == kStateArmed; }
  QString stateText() const;
  QString reasonText() const;

  static SafetyState fromJson(const QJsonObject& msg);
};

// Mirror of mini_acrobat_interfaces/msg/MotorCommand.
struct MotorCmd {
  bool armed = false;
  double throttle[3] = {0.0, 0.0, 0.0};

  static MotorCmd fromJson(const QJsonObject& msg);
};

}  // namespace magcs

Q_DECLARE_METATYPE(magcs::SafetyState)
Q_DECLARE_METATYPE(magcs::MotorCmd)
