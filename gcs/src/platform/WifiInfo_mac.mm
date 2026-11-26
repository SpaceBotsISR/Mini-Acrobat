// macOS implementation (CoreWLAN). Compiled with ARC.
//
// RSSI, noise and TX rate are readable by any app. Since macOS 14 the SSID and
// BSSID come back nil unless the app has Location permission, hence
// requestWifiNamePermission() and the NSLocation* keys in Info.plist.
#import <CoreLocation/CoreLocation.h>
#import <CoreWLAN/CoreWLAN.h>
#import <Foundation/Foundation.h>

#include <QString>

#include "platform/WifiInfo.hpp"

namespace magcs {

namespace {
CLLocationManager* g_locationManager = nil;  // must outlive the prompt
}

WifiInfo readWifiInfo()
{
  WifiInfo w;
  @autoreleasepool {
    CWInterface* itf = [[CWWiFiClient sharedWiFiClient] interface];
    if (itf == nil) {
      w.note = QStringLiteral("no Wi-Fi interface");
      return w;
    }
    w.available = true;
    w.interfaceName = QString::fromNSString(itf.interfaceName);
    w.powerOn = itf.powerOn;
    if (!w.powerOn) {
      w.note = QStringLiteral("Wi-Fi is off");
      return w;
    }
    w.rssiDbm = static_cast<int>(itf.rssiValue);
    w.noiseDbm = static_cast<int>(itf.noiseMeasurement);
    w.txRateMbps = itf.transmitRate;
    if (NSString* ssid = itf.ssid) {
      w.ssid = QString::fromNSString(ssid);
    }
    if (NSString* bssid = itf.bssid) {
      w.bssid = QString::fromNSString(bssid);
    }
    if (CWChannel* ch = itf.wlanChannel) {
      w.channel = static_cast<int>(ch.channelNumber);
      switch (static_cast<int>(ch.channelBand)) {  // CWChannelBand: 1=2.4, 2=5, 3=6 GHz
        case 1: w.bandGHz = 2.4; break;
        case 2: w.bandGHz = 5.0; break;
        case 3: w.bandGHz = 6.0; break;
        default: break;
      }
    }
    if (w.rssiDbm == 0) {
      w.note = QStringLiteral("not connected to a Wi-Fi network");
    } else if (w.ssid.isEmpty()) {
      w.note = QStringLiteral("network name hidden by macOS (needs Location permission)");
    }
  }
  return w;
}

void requestWifiNamePermission()
{
  if (g_locationManager == nil) {
    g_locationManager = [[CLLocationManager alloc] init];
  }
  [g_locationManager requestWhenInUseAuthorization];
}

bool wifiNamePermissionSupported()
{
  return true;
}

}  // namespace magcs
