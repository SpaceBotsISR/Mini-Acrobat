#pragma once
// rosbridge v2 protocol: JSON builders and a parser. Pure functions, no I/O,
// so they are unit-tested without a socket.
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

namespace magcs::rosbridge {

QString advertise(const QString& topic, const QString& type);
QString subscribe(const QString& topic, const QString& type, int throttleMs = 0,
                  int queueLength = 1);
QString publish(const QString& topic, const QJsonObject& msg);
QString callService(const QString& service, const QString& id, const QJsonObject& args);

struct Incoming {
  enum class Kind { Unknown, Publish, ServiceResponse, Status };
  Kind kind = Kind::Unknown;

  // Publish
  QString topic;
  QJsonObject msg;

  // ServiceResponse. On failure rosbridge puts an error string in `values`.
  QString id;
  bool result = false;
  QJsonValue values;

  // Status
  QString level;
  QString text;
};

Incoming parse(const QString& text);

}  // namespace magcs::rosbridge
