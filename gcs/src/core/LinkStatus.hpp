#pragma once
#include <QJsonObject>
#include <QMetaType>
#include <QString>
#include <QStringList>

namespace magcs {

// Mirror of mini_acrobat_interfaces/msg/LinkStatus (/link/status, ~1 Hz),
// published on the drone by mini_acrobat_link. Fields are 0 / empty when unknown.
struct DroneLinkStatus {
  bool valid = false;  // false until the first /link/status arrives
  QString hostname;
  QString wifiInterface;
  bool wifiConnected = false;
  QString ssid;
  QString bssid;
  int rssiDbm = 0;
  int noiseDbm = 0;
  int frequencyMhz = 0;
  double txBitrateMbps = 0.0;
  double rxBitrateMbps = 0.0;
  QStringList addresses;  // "<iface> <addr>/<prefix>"
  bool tailscaleUp = false;
  QString tailscaleIp;

  static DroneLinkStatus fromJson(const QJsonObject& msg);
};

}  // namespace magcs

Q_DECLARE_METATYPE(magcs::DroneLinkStatus)
