#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

#include "core/MessagePath.hpp"

using namespace magcs;

class TstMessagePath : public QObject {
  Q_OBJECT
private slots:
  void parse()
  {
    auto p = msgpath::parse(" /motor_cmd.throttle[1] ");
    QCOMPARE(p.topic, QStringLiteral("/motor_cmd"));
    QCOMPARE(p.field, QStringLiteral("throttle[1]"));
    QVERIFY(p.valid());
    QCOMPARE(p.full(), QStringLiteral("/motor_cmd.throttle[1]"));
    p = msgpath::parse("/a/b/c");
    QCOMPARE(p.topic, QStringLiteral("/a/b/c"));
    QVERIFY(p.field.isEmpty());
    QVERIFY(!msgpath::parse("motor_cmd.x").valid());
  }

  void valueAt()
  {
    const QJsonObject msg = QJsonDocument::fromJson(
      R"({"linear":{"x":0.5,"y":-1},"throttle":[0.1,0.2,0.3],"armed":true,
          "m":[[1,2],[3,4]],"pts":[{"v":7}],"name":"x"})").object();
    QCOMPARE(*msgpath::numberAt(msg, "linear.x"), 0.5);
    QCOMPARE(*msgpath::numberAt(msg, "linear.y"), -1.0);
    QCOMPARE(*msgpath::numberAt(msg, "throttle[2]"), 0.3);
    QCOMPARE(*msgpath::numberAt(msg, "armed"), 1.0);
    QCOMPARE(*msgpath::numberAt(msg, "m[1][0]"), 3.0);
    QCOMPARE(*msgpath::numberAt(msg, "pts[0].v"), 7.0);
    QVERIFY(!msgpath::numberAt(msg, "name"));
    QVERIFY(!msgpath::numberAt(msg, "throttle[3]"));
    QVERIFY(!msgpath::numberAt(msg, "linear.q"));
    QVERIFY(!msgpath::numberAt(msg, "linear..x"));
    QVERIFY(!msgpath::numberAt(msg, "throttle[x]"));
  }

  void leaves()
  {
    const QJsonObject msg = QJsonDocument::fromJson(
      R"({"a":{"b":1},"arr":[1,2,3,4,5],"s":"text","ok":false})").object();
    const auto l = msgpath::numericLeaves(msg, 3);
    QStringList names;
    for (const auto& p : l) {
      names << p.first;
    }
    QCOMPARE(names, (QStringList{"a.b", "arr[0]", "arr[1]", "arr[2]", "ok"}));
  }
};

QTEST_GUILESS_MAIN(TstMessagePath)
#include "tst_messagepath.moc"
