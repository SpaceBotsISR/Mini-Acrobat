#include "core/MessagePath.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace magcs::msgpath {

Path parse(const QString& text)
{
  const QString t = text.trimmed();
  Path p;
  const int dot = t.indexOf(QLatin1Char('.'));
  if (dot < 0) {
    p.topic = t;
  } else {
    p.topic = t.left(dot);
    p.field = t.mid(dot + 1);
  }
  return p;
}

QJsonValue valueAt(const QJsonValue& root, const QString& field)
{
  QJsonValue cur = root;
  if (field.isEmpty()) {
    return cur;
  }
  // Tokens: names separated by '.', each optionally followed by [i][j]...
  const QStringList parts = field.split(QLatin1Char('.'));
  for (const QString& part : parts) {
    if (part.isEmpty()) {
      return QJsonValue(QJsonValue::Undefined);
    }
    int pos = part.indexOf(QLatin1Char('['));
    const QString name = pos < 0 ? part : part.left(pos);
    if (!name.isEmpty()) {
      if (!cur.isObject()) {
        return QJsonValue(QJsonValue::Undefined);
      }
      const QJsonObject o = cur.toObject();
      auto it = o.constFind(name);
      if (it == o.constEnd()) {
        return QJsonValue(QJsonValue::Undefined);
      }
      cur = it.value();
    }
    while (pos >= 0) {
      const int close = part.indexOf(QLatin1Char(']'), pos);
      if (close < 0) {
        return QJsonValue(QJsonValue::Undefined);
      }
      bool ok = false;
      const int idx = part.mid(pos + 1, close - pos - 1).toInt(&ok);
      if (!ok || !cur.isArray()) {
        return QJsonValue(QJsonValue::Undefined);
      }
      const QJsonArray a = cur.toArray();
      if (idx < 0 || idx >= a.size()) {
        return QJsonValue(QJsonValue::Undefined);
      }
      cur = a.at(idx);
      pos = part.indexOf(QLatin1Char('['), close);
    }
  }
  return cur;
}

std::optional<double> toNumber(const QJsonValue& v)
{
  if (v.isDouble()) {
    return v.toDouble();
  }
  if (v.isBool()) {
    return v.toBool() ? 1.0 : 0.0;
  }
  return std::nullopt;
}

std::optional<double> numberAt(const QJsonValue& root, const QString& field)
{
  return toNumber(valueAt(root, field));
}

namespace {

void collect(const QJsonValue& v, const QString& prefix, int maxArray, int depth,
             QList<QPair<QString, double>>& out)
{
  if (depth > 12) {
    return;
  }
  if (v.isObject()) {
    const QJsonObject o = v.toObject();
    for (auto it = o.constBegin(); it != o.constEnd(); ++it) {
      collect(it.value(), prefix.isEmpty() ? it.key() : prefix + QLatin1Char('.') + it.key(),
              maxArray, depth + 1, out);
    }
  } else if (v.isArray()) {
    const QJsonArray a = v.toArray();
    const int n = qMin(a.size(), maxArray);
    for (int i = 0; i < n; ++i) {
      collect(a.at(i), QStringLiteral("%1[%2]").arg(prefix).arg(i), maxArray, depth + 1, out);
    }
  } else if (const auto x = toNumber(v)) {
    out.append({prefix, *x});
  }
}

}  // namespace

QList<QPair<QString, double>> numericLeaves(const QJsonValue& root, int maxArray)
{
  QList<QPair<QString, double>> out;
  collect(root, QString(), maxArray, 0, out);
  return out;
}

QString shortText(const QJsonValue& v, int maxLen)
{
  QString s;
  switch (v.type()) {
    case QJsonValue::Null: s = QStringLiteral("null"); break;
    case QJsonValue::Bool: s = v.toBool() ? QStringLiteral("true") : QStringLiteral("false"); break;
    case QJsonValue::Double: {
      const double d = v.toDouble();
      s = (d == static_cast<qint64>(d) && qAbs(d) < 1e15) ? QString::number(static_cast<qint64>(d))
                                                          : QString::number(d, 'g', 6);
      break;
    }
    case QJsonValue::String: s = QLatin1Char('"') + v.toString() + QLatin1Char('"'); break;
    case QJsonValue::Array:
      s = QStringLiteral("[%1 items]").arg(v.toArray().size());
      break;
    case QJsonValue::Object:
      s = QStringLiteral("{%1 fields}").arg(v.toObject().size());
      break;
    case QJsonValue::Undefined: s = QStringLiteral("-"); break;
  }
  if (s.size() > maxLen) {
    s = s.left(maxLen - 1) + QChar(0x2026);
  }
  return s;
}

}  // namespace magcs::msgpath
