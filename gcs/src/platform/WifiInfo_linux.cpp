// Non-Apple implementation: /proc/net/wireless on Linux, nothing elsewhere.
#include <QFile>

#include "platform/WifiInfo.hpp"

namespace magcs {

WifiInfo readWifiInfo()
{
#ifdef Q_OS_LINUX
  QFile f(QStringLiteral("/proc/net/wireless"));
  if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
    WifiInfo w = parseProcNetWireless(QString::fromUtf8(f.readAll()));
    if (w.available && w.rssiDbm == 0) {
      w.note = QStringLiteral("not associated");
    }
    return w;
  }
#endif
  WifiInfo w;
  w.note = QStringLiteral("Wi-Fi info not available on this platform");
  return w;
}

void requestWifiNamePermission() {}

bool wifiNamePermissionSupported()
{
  return false;
}

}  // namespace magcs
