#include <QJsonArray>
#include <QJsonDocument>
#include <QtTest>

#include "core/RosbridgeProtocol.hpp"

using namespace magcs;

static QJsonObject obj(const QString& s)
{
  return QJsonDocument::fromJson(s.toUtf8()).object();
}

class TstProtocol : public QObject {
  Q_OBJECT
private slots:
  void advertise()
  {
    const auto o = obj(rosbridge::advertise("/gcs/heartbeat", "mini_acrobat_interfaces/msg/Heartbeat"));
    QCOMPARE(o["op"].toString(), QString("advertise"));
    QCOMPARE(o["topic"].toString(), QString("/gcs/heartbeat"));
    QCOMPARE(o["type"].toString(), QString("mini_acrobat_interfaces/msg/Heartbeat"));
  }

  void subscribeThrottle()
  {
    auto o = obj(rosbridge::subscribe("/a", "t/msg/T"));
    QCOMPARE(o["op"].toString(), QString("subscribe"));
    QVERIFY(!o.contains("throttle_rate"));
    o = obj(rosbridge::subscribe("/a", "t/msg/T", 100));
    QCOMPARE(o["throttle_rate"].toInt(), 100);
  }

  void publish()
  {
    const auto o = obj(rosbridge::publish("/x", {{"throttle", QJsonArray{0.5, -0.5, 0.0}}}));
    QCOMPARE(o["op"].toString(), QString("publish"));
    const auto a = o["msg"].toObject()["throttle"].toArray();
    QCOMPARE(a.size(), 3);
    QCOMPARE(a[1].toDouble(), -0.5);
  }

  void callService()
  {
    const auto o = obj(rosbridge::callService("/safety/arm", "id-1", {}));
    QCOMPARE(o["op"].toString(), QString("call_service"));
    QCOMPARE(o["service"].toString(), QString("/safety/arm"));
    QCOMPARE(o["id"].toString(), QString("id-1"));
    QVERIFY(o["args"].isObject());
  }

  void parsePublish()
  {
    const auto in = rosbridge::parse(R"({"op":"publish","topic":"/safety/state","msg":{"state":1}})");
    QCOMPARE(in.kind, rosbridge::Incoming::Kind::Publish);
    QCOMPARE(in.topic, QString("/safety/state"));
    QCOMPARE(in.msg["state"].toInt(), 1);
  }

  void parseServiceResponseOk()
  {
    const auto in = rosbridge::parse(
      R"({"op":"service_response","service":"/safety/arm","id":"i","result":true,"values":{"success":false,"message":"refused"}})");
    QCOMPARE(in.kind, rosbridge::Incoming::Kind::ServiceResponse);
    QCOMPARE(in.id, QString("i"));
    QVERIFY(in.result);
    QCOMPARE(in.values.toObject()["message"].toString(), QString("refused"));
  }

  void parseServiceResponseFailureString()
  {
    // rosbridge puts an error string in `values` when result is false.
    const auto in = rosbridge::parse(
      R"({"op":"service_response","service":"/x","id":"i","result":false,"values":"service /x does not exist"})");
    QCOMPARE(in.kind, rosbridge::Incoming::Kind::ServiceResponse);
    QVERIFY(!in.result);
    QCOMPARE(in.values.toString(), QString("service /x does not exist"));
  }

  void parseStatus()
  {
    const auto in = rosbridge::parse(R"({"op":"status","level":"error","msg":"nope"})");
    QCOMPARE(in.kind, rosbridge::Incoming::Kind::Status);
    QCOMPARE(in.level, QString("error"));
    QCOMPARE(in.text, QString("nope"));
  }

  void parseGarbage()
  {
    QCOMPARE(rosbridge::parse("not json").kind, rosbridge::Incoming::Kind::Unknown);
    QCOMPARE(rosbridge::parse("[1,2]").kind, rosbridge::Incoming::Kind::Unknown);
    QCOMPARE(rosbridge::parse(R"({"op":"weird"})").kind, rosbridge::Incoming::Kind::Unknown);
  }
};

QTEST_GUILESS_MAIN(TstProtocol)
#include "tst_protocol.moc"
