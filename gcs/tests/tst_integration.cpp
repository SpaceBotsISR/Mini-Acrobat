// End-to-end test of Drone + RosbridgeClient + HeartbeatSender against
// tools/fake_drone/fake_drone.py (a rosbridge server that mimics the safety node).
#include <QJsonArray>
#include <QProcess>
#include <QSignalSpy>
#include <QtTest>

#include "core/Drone.hpp"
#include "core/Topics.hpp"

using namespace magcs;

class TstIntegration : public QObject {
  Q_OBJECT
private:
  QString m_python;
  QProcess* m_proc = nullptr;
  int m_port = 0;
  Drone* m_drone = nullptr;

  void connectDrone()
  {
    m_drone->connectToHost("127.0.0.1", static_cast<quint16>(m_port));
    QTRY_VERIFY_WITH_TIMEOUT(m_drone->isConnected(), 3000);
    QTRY_VERIFY_WITH_TIMEOUT(m_drone->safety().valid, 3000);
  }

  // Calls arm() and returns the service's `success`.
  bool armNow()
  {
    QSignalSpy spy(m_drone, &Drone::armResult);
    m_drone->arm();
    if (!spy.wait(2000)) {
      return false;
    }
    return spy.at(0).at(0).toBool();
  }

  bool armOrDie()
  {
    QTest::qWait(500);  // >= 5 healthy heartbeats at 20 Hz
    return armNow();
  }

private slots:
  void initTestCase()
  {
    m_python = qEnvironmentVariable("MAGCS_PYTHON", "python3");
    QProcess check;
    check.start(m_python, {"-c", "import websockets"});
    if (!check.waitForFinished(10000) || check.exitCode() != 0) {
      QSKIP("python3 with the 'websockets' package is not available (pip install websockets)", "");
    }
  }

  void init()
  {
    m_proc = new QProcess;
    m_proc->start(m_python, {FAKE_DRONE_PY, "--port", "0"});
    QVERIFY(m_proc->waitForStarted(5000));
    QVERIFY(m_proc->waitForReadyRead(10000));
    const QString line = QString::fromUtf8(m_proc->readLine()).trimmed();
    QVERIFY2(line.startsWith("READY "), qPrintable(line));
    m_port = line.mid(6).toInt();
    QVERIFY(m_port > 0);
    m_drone = new Drone;
  }

  void cleanup()
  {
    delete m_drone;
    m_drone = nullptr;
    m_proc->terminate();
    m_proc->waitForFinished(3000);
    delete m_proc;
    m_proc = nullptr;
  }

  void receivesInitialState()
  {
    connectDrone();
    QVERIFY(!m_drone->safety().armed());
    QCOMPARE(m_drone->safety().reason, SafetyState::kReasonBoot);
    // heartbeats are flowing
    QTRY_VERIFY_WITH_TIMEOUT(m_drone->heartbeat()->sentCount() > 5, 2000);
    QTRY_VERIFY_WITH_TIMEOUT(m_drone->safety().linkOk, 2000);
  }

  void receivesLinkStatus()
  {
    connectDrone();
    QTRY_VERIFY_WITH_TIMEOUT(m_drone->linkStatus().valid, 3000);
    QCOMPARE(m_drone->linkStatus().hostname, QStringLiteral("fake-drone"));
    QVERIFY(m_drone->linkStatus().rssiDbm < 0);
    QVERIFY(m_drone->linkStatusAgeMs() >= 0);
    m_drone->disconnectFromHost();
    QTRY_VERIFY_WITH_TIMEOUT(!m_drone->isConnected(), 2000);
    QVERIFY(!m_drone->linkStatus().valid);  // cleared on disconnect
  }

  void armIsRefusedWithoutHealthyHeartbeats()
  {
    connectDrone();
    // Right after connecting there are not yet 5 consecutive heartbeats.
    QSignalSpy spy(m_drone, &Drone::armResult);
    m_drone->arm();
    QVERIFY(spy.wait(2000));
    QCOMPARE(spy.at(0).at(0).toBool(), false);
    QVERIFY(!m_drone->safety().armed());
  }

  void armAndDisarm()
  {
    connectDrone();
    QVERIFY(armOrDie());
    QTRY_VERIFY_WITH_TIMEOUT(m_drone->safety().armed(), 2000);

    QSignalSpy spy(m_drone, &Drone::disarmResult);
    m_drone->disarm();
    QVERIFY(spy.wait(2000));
    QTRY_VERIFY_WITH_TIMEOUT(!m_drone->safety().armed(), 2000);
    QCOMPARE(m_drone->safety().reason, SafetyState::kReasonUser);
  }

  void linkLossDisarmsAndIsLatched()
  {
    connectDrone();
    QVERIFY(armOrDie());
    QTRY_VERIFY_WITH_TIMEOUT(m_drone->safety().armed(), 2000);

    m_drone->heartbeat()->setPaused(true);  // GCS stops talking, socket stays open
    QTRY_VERIFY_WITH_TIMEOUT(!m_drone->safety().armed(), 1500);
    QCOMPARE(m_drone->safety().reason, SafetyState::kReasonLinkLost);

    m_drone->heartbeat()->setPaused(false);  // link is back...
    QTest::qWait(600);
    QVERIFY(!m_drone->safety().armed());  // ...but stays disarmed until re-armed by the user
    QCOMPARE(m_drone->safety().reason, SafetyState::kReasonLinkLost);
    QVERIFY(armNow());
    QTRY_VERIFY_WITH_TIMEOUT(m_drone->safety().armed(), 2000);
  }

  void controlOutputRules()
  {
    connectDrone();
    m_drone->setThrottleLimit(1.0);

    // Not armed: control output refused.
    QVERIFY(!m_drone->setControlEnabled(true));

    QVERIFY(armOrDie());
    QTRY_VERIFY_WITH_TIMEOUT(m_drone->safety().armed(), 2000);

    // Armed but throttle != 0: refused (no jump on enable).
    m_drone->setThrottle(0, 0.5);
    QVERIFY(!m_drone->setControlEnabled(true));
    m_drone->zeroThrottle();
    QVERIFY(m_drone->setControlEnabled(true));

    m_drone->setThrottle(0, 0.5);
    m_drone->setThrottle(2, -0.3);
    QTRY_VERIFY_WITH_TIMEOUT(
      qFuzzyCompare(m_drone->motorCmd().throttle[0] + 1.0, 1.5) &&
      qFuzzyCompare(m_drone->motorCmd().throttle[2] + 1.0, 0.7), 2000);
    QVERIFY(m_drone->motorCmd().armed);

    // The limit clamps what is sent.
    m_drone->setThrottleLimit(0.25);
    QTRY_VERIFY_WITH_TIMEOUT(
      qFuzzyCompare(m_drone->motorCmd().throttle[0] + 1.0, 1.25) &&
      qFuzzyCompare(m_drone->motorCmd().throttle[2] + 1.0, 0.75), 2000);

    // Disarm: throttles zeroed, control off, motors stop.
    m_drone->disarm();
    QVERIFY(!m_drone->controlEnabled());
    QCOMPARE(m_drone->throttle()[0], 0.0);
    QTRY_VERIFY_WITH_TIMEOUT(!m_drone->motorCmd().armed, 2000);
    QCOMPARE(m_drone->motorCmd().throttle[0], 0.0);
  }

  void armRefusedWhileSetpointNotZero()
  {
    connectDrone();
    QTest::qWait(500);
    // Someone else (e.g. the future control node) publishes a non-zero setpoint.
    m_drone->client()->publish(topics::kMotorSetpoint, {{"throttle", QJsonArray{0.5, 0.0, 0.0}}});
    QTest::qWait(50);
    QVERIFY(!armNow());
    QVERIFY(!m_drone->safety().armed());
  }

  void disconnectResetsEverything()
  {
    connectDrone();
    QVERIFY(armOrDie());
    QTRY_VERIFY_WITH_TIMEOUT(m_drone->safety().armed(), 2000);
    m_drone->setThrottleLimit(1.0);
    QVERIFY(m_drone->setControlEnabled(true));
    m_drone->setThrottle(1, 0.4);

    m_drone->disconnectFromHost();
    QTRY_VERIFY_WITH_TIMEOUT(!m_drone->isConnected(), 2000);
    QVERIFY(!m_drone->safety().valid);
    QVERIFY(!m_drone->controlEnabled());
    QCOMPARE(m_drone->throttle()[1], 0.0);
  }

  void reconnectsAfterServerRestart()
  {
    m_drone->client()->setAutoReconnect(true);
    connectDrone();
    const int port = m_port;
    // kill the server, start another one on the same port
    m_proc->terminate();
    m_proc->waitForFinished(3000);
    QTRY_VERIFY_WITH_TIMEOUT(!m_drone->isConnected(), 3000);

    m_proc->start(m_python, {FAKE_DRONE_PY, "--port", QString::number(port)});
    QVERIFY(m_proc->waitForStarted(5000));
    QVERIFY(m_proc->waitForReadyRead(10000));
    QTRY_VERIFY_WITH_TIMEOUT(m_drone->isConnected(), 6000);
    QTRY_VERIFY_WITH_TIMEOUT(m_drone->safety().valid, 3000);  // re-subscribed automatically
  }
};

QTEST_GUILESS_MAIN(TstIntegration)
#include "tst_integration.moc"
