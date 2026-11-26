#include "core/ConnectionTarget.hpp"

#include <QSettings>

namespace magcs {

QString ConnectionTarget::effectiveHost() const
{
  return mode == Mode::Simulator ? QStringLiteral("localhost") : host.trimmed();
}

QString ConnectionTarget::url() const
{
  return QStringLiteral("ws://%1:%2").arg(effectiveHost()).arg(port);
}

QString ConnectionTarget::modeName(Mode m)
{
  switch (m) {
    case Mode::Tailscale: return QStringLiteral("Tailscale (VPN)");
    case Mode::Manual: return QStringLiteral("Manual (IP / hostname)");
    case Mode::Simulator: return QStringLiteral("Simulator (fake drone)");
  }
  return {};
}

QString ConnectionTarget::modeKey(Mode m)
{
  switch (m) {
    case Mode::Tailscale: return QStringLiteral("tailscale");
    case Mode::Manual: return QStringLiteral("manual");
    case Mode::Simulator: return QStringLiteral("simulator");
  }
  return {};
}

bool ConnectionTarget::modeFromKey(const QString& key, Mode* out)
{
  for (Mode m : {Mode::Tailscale, Mode::Manual, Mode::Simulator}) {
    if (modeKey(m).compare(key.trimmed(), Qt::CaseInsensitive) == 0) {
      *out = m;
      return true;
    }
  }
  return false;
}

QString ConnectionTarget::defaultHost(Mode m)
{
  switch (m) {
    case Mode::Tailscale: return QStringLiteral("mini-acrobat");
    case Mode::Manual: return QStringLiteral("raspberrypi.local");
    case Mode::Simulator: return QStringLiteral("localhost");
  }
  return {};
}

QString ConnectionTarget::savedHost(Mode m)
{
  QSettings s;
  QString fallback = defaultHost(m);
  if (m == Mode::Manual) {
    // host saved by MAgcs <= 0.1.0
    fallback = s.value(QStringLiteral("connection/host"), fallback).toString();
  }
  return s.value(QStringLiteral("connection/host_%1").arg(modeKey(m)), fallback).toString();
}

ConnectionTarget ConnectionTarget::load()
{
  QSettings s;
  ConnectionTarget t;
  modeFromKey(s.value(QStringLiteral("connection/mode"), modeKey(Mode::Tailscale)).toString(),
              &t.mode);
  t.host = savedHost(t.mode);
  t.port = static_cast<quint16>(s.value(QStringLiteral("connection/port"), 9090).toUInt());
  return t;
}

void ConnectionTarget::save() const
{
  QSettings s;
  s.setValue(QStringLiteral("connection/mode"), modeKey(mode));
  if (mode != Mode::Simulator) {
    s.setValue(QStringLiteral("connection/host_%1").arg(modeKey(mode)), host.trimmed());
  }
  s.setValue(QStringLiteral("connection/port"), port);
}

}  // namespace magcs
