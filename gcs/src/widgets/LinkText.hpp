#pragma once
// Text formatting shared by the connect dialog and the connection dock.
#include <QString>

#include "core/LinkStatus.hpp"
#include "core/Tailscale.hpp"
#include "platform/WifiInfo.hpp"

namespace magcs::linktext {

// Latency above which the 300 ms heartbeat watchdog is at risk.
constexpr double kSlowPathMs = 150.0;

QString macWifi(const WifiInfo& w);                 // "-54 dBm (good) · MEO-1234 · 866 Mbps · ch 36, 5 GHz"
QString droneWifi(const DroneLinkStatus& s);        // same, for /link/status
QString tailscalePath(const tailscale::Ping& p, bool pinging);
// Non-empty (HTML) when the path is relayed or slow.
QString tailscalePathWarning(const tailscale::Ping& p);
QString tailscaleState(const tailscale::Status& s); // HTML, one line

}  // namespace magcs::linktext
