#include "core/RosbridgeProtocol.hpp"

#include <QJsonDocument>

namespace magcs::rosbridge {

namespace {
QString toText(const QJsonObject& o)
{
  return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}
}  // namespace

QString advertise(const QString& topic, const QString& type)
{
  return toText({{"op", "advertise"}, {"topic", topic}, {"type", type}});
}

QString subscribe(const QString& topic, const QString& type, int throttleMs, int queueLength)
{
  QJsonObject o{{"op", "subscribe"}, {"topic", topic}, {"queue_length", queueLength}};
  if (!type.isEmpty()) {
    o.insert("type", type);  // empty: rosbridge looks the type up itself
  }
  if (throttleMs > 0) {
    o.insert("throttle_rate", throttleMs);
  }
  return toText(o);
}

QString unsubscribe(const QString& topic)
{
  return toText({{"op", "unsubscribe"}, {"topic", topic}});
}

QString publish(const QString& topic, const QJsonObject& msg)
{
  return toText({{"op", "publish"}, {"topic", topic}, {"msg", msg}});
}

QString callService(const QString& service, const QString& id, const QJsonObject& args)
{
  return toText({{"op", "call_service"}, {"service", service}, {"id", id}, {"args", args}});
}

Incoming parse(const QString& text)
{
  Incoming in;
  QJsonParseError err;
  const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &err);
  if (err.error != QJsonParseError::NoError || !doc.isObject()) {
    return in;
  }
  const QJsonObject o = doc.object();
  const QString op = o.value("op").toString();
  if (op == QLatin1String("publish")) {
    in.kind = Incoming::Kind::Publish;
    in.topic = o.value("topic").toString();
    in.msg = o.value("msg").toObject();
  } else if (op == QLatin1String("service_response")) {
    in.kind = Incoming::Kind::ServiceResponse;
    in.id = o.value("id").toString();
    in.result = o.value("result").toBool(false);
    in.values = o.value("values");
  } else if (op == QLatin1String("status")) {
    in.kind = Incoming::Kind::Status;
    in.level = o.value("level").toString();
    in.text = o.value("msg").toString();
  }
  return in;
}

}  // namespace magcs::rosbridge
