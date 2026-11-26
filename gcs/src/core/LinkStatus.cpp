#include "core/LinkStatus.hpp"

#include <QJsonArray>

namespace magcs {

DroneLinkStatus DroneLinkStatus::fromJson(const QJsonObject& msg)
{
  DroneLinkStatus s;
  s.valid = msg.contains(QStringLiteral("wifi_connected"));
  s.hostname = msg.value(QStringLiteral("hostname")).toString();
  s.wifiInterface = msg.value(QStringLiteral("wifi_interface")).toString();
  s.wifiConnected = msg.value(QStringLiteral("wifi_connected")).toBool(false);
  s.ssid = msg.value(QStringLiteral("ssid")).toString();
  s.bssid = msg.value(QStringLiteral("bssid")).toString();
  s.rssiDbm = msg.value(QStringLiteral("rssi_dbm")).toInt(0);
  s.noiseDbm = msg.value(QStringLiteral("noise_dbm")).toInt(0);
  s.frequencyMhz = msg.value(QStringLiteral("frequency_mhz")).toInt(0);
  s.txBitrateMbps = msg.value(QStringLiteral("tx_bitrate_mbps")).toDouble(0.0);
  s.rxBitrateMbps = msg.value(QStringLiteral("rx_bitrate_mbps")).toDouble(0.0);
  for (const QJsonValue& v : msg.value(QStringLiteral("addresses")).toArray()) {
    s.addresses.append(v.toString());
  }
  s.tailscaleUp = msg.value(QStringLiteral("tailscale_up")).toBool(false);
  s.tailscaleIp = msg.value(QStringLiteral("tailscale_ip")).toString();
  return s;
}

}  // namespace magcs
