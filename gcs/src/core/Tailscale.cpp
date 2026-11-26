#include "core/Tailscale.hpp"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>
#include <algorithm>

namespace magcs::tailscale {

namespace {

QString stripDot(QString s)
{
  if (s.endsWith(QLatin1Char('.'))) {
    s.chop(1);
  }
  return s;
}

Node parseNode(const QJsonObject& o)
{
  Node n;
  n.hostName = o.value(QStringLiteral("HostName")).toString();
  n.dnsName = stripDot(o.value(QStringLiteral("DNSName")).toString());
  n.os = o.value(QStringLiteral("OS")).toString();
  for (const QJsonValue& v : o.value(QStringLiteral("TailscaleIPs")).toArray()) {
    n.ips.append(v.toString());
  }
  n.online = o.value(QStringLiteral("Online")).toBool(false);
  n.active = o.value(QStringLiteral("Active")).toBool(false);
  n.curAddr = o.value(QStringLiteral("CurAddr")).toString();
  n.relay = o.value(QStringLiteral("Relay")).toString();
  return n;
}

}  // namespace

QString Node::ipv4() const
{
  for (const QString& ip : ips) {
    if (!ip.contains(QLatin1Char(':'))) {
      return ip;
    }
  }
  return ips.value(0);
}

QString Node::shortName() const
{
  if (!dnsName.isEmpty()) {
    return dnsName.section(QLatin1Char('.'), 0, 0);
  }
  return hostName;
}

QString Node::connectionText() const
{
  if (!online) {
    return QStringLiteral("offline");
  }
  if (isDirect()) {
    return QStringLiteral("direct (%1)").arg(curAddr);
  }
  if (active && !relay.isEmpty()) {
    return QStringLiteral("relay DERP(%1)").arg(relay);
  }
  return QStringLiteral("idle");
}

const Node* Status::findPeer(const QString& hostOrIp) const
{
  const QString key = stripDot(hostOrIp.trimmed());
  if (key.isEmpty()) {
    return nullptr;
  }
  for (const Node& n : peers) {
    if (n.hostName.compare(key, Qt::CaseInsensitive) == 0 ||
        n.dnsName.compare(key, Qt::CaseInsensitive) == 0 ||
        n.shortName().compare(key, Qt::CaseInsensitive) == 0 || n.ips.contains(key))
    {
      return &n;
    }
  }
  return nullptr;
}

Status parseStatus(const QByteArray& json)
{
  Status s;
  QJsonParseError err{};
  const QJsonDocument doc = QJsonDocument::fromJson(json, &err);
  if (err.error != QJsonParseError::NoError || !doc.isObject()) {
    s.error = QStringLiteral("cannot parse `tailscale status --json`: %1").arg(err.errorString());
    return s;
  }
  const QJsonObject root = doc.object();
  s.ok = true;
  s.backendState = root.value(QStringLiteral("BackendState")).toString();
  s.magicDnsSuffix = root.value(QStringLiteral("MagicDNSSuffix")).toString();
  s.tailnet = root.value(QStringLiteral("CurrentTailnet")).toObject()
                .value(QStringLiteral("Name")).toString();
  s.self = parseNode(root.value(QStringLiteral("Self")).toObject());

  const QJsonObject peers = root.value(QStringLiteral("Peer")).toObject();
  for (auto it = peers.begin(); it != peers.end(); ++it) {
    s.peers.append(parseNode(it.value().toObject()));
  }
  std::sort(s.peers.begin(), s.peers.end(), [](const Node& a, const Node& b) {
    if (a.online != b.online) {
      return a.online;
    }
    return a.shortName().compare(b.shortName(), Qt::CaseInsensitive) < 0;
  });
  return s;
}

QString Ping::text() const
{
  if (!ok) {
    return error.isEmpty() ? QStringLiteral("-") : error;
  }
  return QStringLiteral("%1 via %2, %3 ms")
    .arg(relayed() ? QStringLiteral("relayed") : QStringLiteral("direct"), via)
    .arg(ms, 0, 'f', ms < 10 ? 1 : 0);
}

Ping parsePing(const QString& output)
{
  // pong from mini-acrobat (100.101.102.103) via 192.168.1.20:41641 in 4ms
  // pong from mini-acrobat (100.101.102.103) via DERP(mad) in 48ms
  static const QRegularExpression re(
    QStringLiteral(R"(pong from (\S+) \(([^)]+)\) via (\S+) in ([0-9.]+)\s*ms)"));
  Ping p;
  const QStringList lines = output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
  for (const QString& line : lines) {
    const QRegularExpressionMatch m = re.match(line);
    if (m.hasMatch()) {
      p.ok = true;
      p.peer = m.captured(1);
      p.ip = m.captured(2);
      p.via = m.captured(3);
      p.ms = m.captured(4).toDouble();
      return p;
    }
  }
  p.error = lines.isEmpty() ? QStringLiteral("no reply") : lines.last().trimmed();
  return p;
}

QString findBinary(const QString& overridePath)
{
  QStringList candidates;
  if (!overridePath.isEmpty()) {
    candidates << overridePath;
  }
  candidates << QStringLiteral("/Applications/Tailscale.app/Contents/MacOS/Tailscale")
             << QStringLiteral("/opt/homebrew/bin/tailscale")
             << QStringLiteral("/usr/local/bin/tailscale")
             << QStringLiteral("/usr/bin/tailscale");
  for (const QString& c : candidates) {
    const QFileInfo fi(c);
    if (fi.isFile() && fi.isExecutable()) {
      return c;
    }
  }
  return QStandardPaths::findExecutable(QStringLiteral("tailscale"));
}

}  // namespace magcs::tailscale
