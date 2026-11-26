#pragma once
// Wi-Fi link of THIS computer. readWifiInfo() is per platform:
//   macOS: CoreWLAN (WifiInfo_mac.mm)
//   Linux: /proc/net/wireless (WifiInfo_linux.cpp), no SSID
#include <QString>

namespace magcs {

struct WifiInfo {
  bool available = false;  // a Wi-Fi interface exists
  bool powerOn = false;
  QString interfaceName;
  QString ssid;            // empty if not associated or hidden by the OS (macOS: Location permission)
  QString bssid;
  int rssiDbm = 0;         // 0 = not associated / unknown
  int noiseDbm = 0;
  double txRateMbps = 0.0;
  int channel = 0;
  double bandGHz = 0.0;
  QString note;            // why something is missing

  bool associated() const { return available && rssiDbm != 0; }
};

WifiInfo readWifiInfo();

// macOS hides the SSID/BSSID unless the app has Location permission; this
// asks for it (shows the system prompt once). No-op elsewhere.
void requestWifiNamePermission();
bool wifiNamePermissionSupported();

// Shared helpers.
int rssiToBars(int rssiDbm);           // 0..4, 0 also for unknown
QString rssiQuality(int rssiDbm);      // "excellent" .. "very weak"
WifiInfo parseProcNetWireless(const QString& text);  // first interface found

}  // namespace magcs
