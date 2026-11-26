#include "widgets/LinkText.hpp"

#include <QStringList>

namespace magcs::linktext {

namespace {

QString rssiText(int dbm)
{
  return dbm < 0 ? QStringLiteral("%1 dBm (%2)").arg(dbm).arg(rssiQuality(dbm))
                 : QStringLiteral("no signal");
}

}  // namespace

QString macWifi(const WifiInfo& w)
{
  if (!w.available || !w.powerOn || w.rssiDbm == 0) {
    return w.note.isEmpty() ? QStringLiteral("not connected") : w.note;
  }
  QStringList parts{rssiText(w.rssiDbm)};
  parts << (w.ssid.isEmpty() ? QStringLiteral("(network name hidden)") : w.ssid);
  if (w.txRateMbps > 0) {
    parts << QStringLiteral("%1 Mbps").arg(w.txRateMbps, 0, 'f', 0);
  }
  if (w.channel > 0) {
    parts << (w.bandGHz > 0 ? QStringLiteral("ch %1, %2 GHz").arg(w.channel).arg(w.bandGHz)
                            : QStringLiteral("ch %1").arg(w.channel));
  }
  return parts.join(QStringLiteral("  ·  "));
}

QString droneWifi(const DroneLinkStatus& s)
{
  if (!s.valid) {
    return QStringLiteral("no /link/status (mini_acrobat_link not running?)");
  }
  if (!s.wifiConnected) {
    return QStringLiteral("%1 not connected").arg(s.wifiInterface.isEmpty() ? QStringLiteral("Wi-Fi")
                                                                            : s.wifiInterface);
  }
  QStringList parts{rssiText(s.rssiDbm)};
  if (!s.ssid.isEmpty()) {
    parts << s.ssid;
  }
  if (s.txBitrateMbps > 0) {
    parts << QStringLiteral("tx %1 Mbps").arg(s.txBitrateMbps, 0, 'f', 0);
  }
  if (s.rxBitrateMbps > 0) {
    parts << QStringLiteral("rx %1 Mbps").arg(s.rxBitrateMbps, 0, 'f', 0);
  }
  if (s.frequencyMhz > 0) {
    parts << QStringLiteral("%1 GHz").arg(s.frequencyMhz / 1000.0, 0, 'f', 1);
  }
  return parts.join(QStringLiteral("  ·  "));
}

QString tailscalePath(const tailscale::Ping& p, bool pinging)
{
  if (!p.ok && p.error.isEmpty()) {
    return pinging ? QStringLiteral("probing...") : QStringLiteral("-");
  }
  return p.text();
}

QString tailscalePathWarning(const tailscale::Ping& p)
{
  if (!p.ok) {
    return {};
  }
  if (p.relayed()) {
    return QStringLiteral(
      "<span style='color:#b08800'>Relayed through a Tailscale DERP server: higher, jittery "
      "latency. With the 300 ms heartbeat timeout expect link-loss disarms. Put the Mac and the "
      "drone on the same network for a direct path.</span>");
  }
  if (p.ms > kSlowPathMs) {
    return QStringLiteral(
             "<span style='color:#b08800'>%1 ms round trip: close to the 300 ms heartbeat "
             "timeout.</span>")
      .arg(p.ms, 0, 'f', 0);
  }
  return {};
}

QString tailscaleState(const tailscale::Status& s)
{
  if (!s.ok) {
    return QStringLiteral("<span style='color:#c0392b'>%1</span>").arg(s.error.toHtmlEscaped());
  }
  if (!s.running()) {
    return QStringLiteral(
             "<span style='color:#c0392b'>Tailscale is <b>%1</b> on this Mac: open the Tailscale "
             "app and connect.</span>")
      .arg(s.backendState.toHtmlEscaped());
  }
  QString line = QStringLiteral("<span style='color:#1a7f37'><b>Running</b></span>");
  if (!s.tailnet.isEmpty()) {
    line += QStringLiteral(" · tailnet <b>%1</b>").arg(s.tailnet.toHtmlEscaped());
  }
  line += QStringLiteral(" · this Mac: %1 (%2)")
            .arg(s.self.shortName().toHtmlEscaped(), s.self.ipv4().toHtmlEscaped());
  return line;
}

}  // namespace magcs::linktext
