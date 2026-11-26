#include "platform/WifiInfo.hpp"

#include <QRegularExpression>
#include <QStringList>

namespace magcs {

int rssiToBars(int rssiDbm)
{
  if (rssiDbm >= 0) {
    return 0;  // unknown
  }
  if (rssiDbm >= -55) {
    return 4;
  }
  if (rssiDbm >= -65) {
    return 3;
  }
  if (rssiDbm >= -72) {
    return 2;
  }
  if (rssiDbm >= -80) {
    return 1;
  }
  return 0;
}

QString rssiQuality(int rssiDbm)
{
  switch (rssiDbm >= 0 ? -1 : rssiToBars(rssiDbm)) {
    case 4: return QStringLiteral("excellent");
    case 3: return QStringLiteral("good");
    case 2: return QStringLiteral("fair");
    case 1: return QStringLiteral("weak");
    case 0: return QStringLiteral("very weak");
    default: return QStringLiteral("unknown");
  }
}

WifiInfo parseProcNetWireless(const QString& text)
{
  // Inter-| sta-|   Quality        |   Discarded packets  ...
  //  face | tus | link level noise |  nwid  crypt   frag ...
  //  wlan0: 0000   54.  -56.  -256        0      0      0 ...
  WifiInfo w;
  static const QRegularExpression re(
    QStringLiteral(R"(^\s*(\S+):\s+\S+\s+(-?[0-9.]+)\s+(-?[0-9.]+)\s+(-?[0-9.]+))"));
  for (const QString& line : text.split(QLatin1Char('\n'))) {
    const QRegularExpressionMatch m = re.match(line);
    if (!m.hasMatch()) {
      continue;
    }
    w.available = true;
    w.powerOn = true;
    w.interfaceName = m.captured(1);
    const int level = static_cast<int>(m.captured(3).toDouble());
    const int noise = static_cast<int>(m.captured(4).toDouble());
    w.rssiDbm = (level < 0) ? level : 0;
    w.noiseDbm = (noise < 0 && noise > -200) ? noise : 0;
    return w;
  }
  w.note = QStringLiteral("no Wi-Fi interface");
  return w;
}

}  // namespace magcs
