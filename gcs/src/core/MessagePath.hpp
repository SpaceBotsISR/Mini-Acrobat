#pragma once
// Message paths in Foxglove style: "/topic.field.sub[2].x".
// Topic names never contain '.', so the topic is everything before the first
// '.', the field path the rest. Pure functions (unit-tested).
#include <QJsonValue>
#include <QList>
#include <QPair>
#include <QString>
#include <optional>

namespace magcs::msgpath {

struct Path {
  QString topic;  // "/motor_cmd"
  QString field;  // "throttle[0]" (empty: the whole message)
  bool valid() const { return topic.startsWith(QLatin1Char('/')) && topic.size() > 1; }
  QString full() const { return field.isEmpty() ? topic : topic + QLatin1Char('.') + field; }
};

Path parse(const QString& text);

// The value at `field` (e.g. "linear.x", "throttle[1]"), or an invalid
// QJsonValue (Undefined) if the path does not exist.
QJsonValue valueAt(const QJsonValue& root, const QString& field);

// As a number: doubles as-is, bools as 0/1. nullopt for anything else.
std::optional<double> numberAt(const QJsonValue& root, const QString& field);
std::optional<double> toNumber(const QJsonValue& v);

// All numeric (and bool) leaves: ("linear.x", 0.1), ("throttle[0]", 0.2), ...
// Arrays longer than maxArray are cut (only their first maxArray elements).
QList<QPair<QString, double>> numericLeaves(const QJsonValue& root, int maxArray = 32);

// Short human text for any JSON value (for tables).
QString shortText(const QJsonValue& v, int maxLen = 80);

}  // namespace magcs::msgpath
