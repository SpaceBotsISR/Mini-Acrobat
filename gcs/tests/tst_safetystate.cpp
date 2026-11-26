#include <QJsonDocument>
#include <QtTest>

#include "core/SafetyState.hpp"

using namespace magcs;

class TstSafetyState : public QObject {
  Q_OBJECT
private slots:
  void parsesArmed()
  {
    const auto s = SafetyState::fromJson(QJsonDocument::fromJson(
      R"({"state":1,"reason":0,"link_ok":true,"heartbeat_age_ms":12.5,"heartbeat_max_gap_ms":60.0,"last_heartbeat_seq":42})").object());
    QVERIFY(s.valid);
    QVERIFY(s.armed());
    QCOMPARE(s.stateText(), QString("ARMED"));
    QVERIFY(s.linkOk);
    QCOMPARE(s.heartbeatAgeMs, 12.5);
    QCOMPARE(s.lastHeartbeatSeq, 42u);
  }

  void parsesLinkLost()
  {
    const auto s = SafetyState::fromJson(QJsonDocument::fromJson(
      R"({"state":0,"reason":3,"link_ok":false,"heartbeat_age_ms":-1,"heartbeat_max_gap_ms":0,"last_heartbeat_seq":0})").object());
    QVERIFY(!s.armed());
    QCOMPARE(s.stateText(), QString("DISARMED"));
    QCOMPARE(s.reasonText(), QString("LINK LOST"));
  }

  void defaultIsNoData()
  {
    const SafetyState s;
    QVERIFY(!s.valid);
    QVERIFY(!s.armed());
    QCOMPARE(s.stateText(), QString("NO DATA"));
  }

  void motorCmd()
  {
    const auto c = MotorCmd::fromJson(QJsonDocument::fromJson(
      R"({"armed":true,"throttle":[0.1,-0.2,0.3]})").object());
    QVERIFY(c.armed);
    QCOMPARE(c.throttle[0], 0.1);
    QCOMPARE(c.throttle[1], -0.2);
    QCOMPARE(c.throttle[2], 0.3);
  }
};

QTEST_GUILESS_MAIN(TstSafetyState)
#include "tst_safetystate.moc"
