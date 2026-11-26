#pragma once
// Tailscale, seen from this computer through its CLI (`tailscale status --json`,
// `tailscale ping`). Pure parsers here (unit-tested); the polling lives in
// NetworkMonitor.
#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

namespace magcs::tailscale {

struct Node {
  QString hostName;
  QString dnsName;   // full MagicDNS name, without the trailing dot
  QString os;
  QStringList ips;   // Tailscale IPs (100.x first, then fd7a:...)
  bool online = false;
  bool active = false;  // traffic in the last few minutes
  QString curAddr;   // "ip:port" when the path is direct, empty when relayed
  QString relay;     // home DERP region, e.g. "mad"

  QString ipv4() const;
  QString shortName() const;  // first label of the MagicDNS name (what to type)
  bool isDirect() const { return !curAddr.isEmpty(); }
  QString connectionText() const;
};

struct Status {
  bool ok = false;         // CLI ran and its output was parsed
  QString error;           // why not ok
  QString backendState;    // Running, Stopped, NeedsLogin, ...
  QString tailnet;
  QString magicDnsSuffix;
  Node self;
  QList<Node> peers;       // online first, then by name

  bool running() const { return ok && backendState == QLatin1String("Running"); }
  // Match by host name, MagicDNS name (short or full) or Tailscale IP.
  const Node* findPeer(const QString& hostOrIp) const;
};

Status parseStatus(const QByteArray& json);

// Result of `tailscale ping -c 1 <host>`.
struct Ping {
  bool ok = false;
  QString peer;
  QString ip;
  QString via;      // "DERP(mad)" or "192.168.1.20:41641"
  double ms = -1.0;
  QString error;

  bool relayed() const { return via.startsWith(QLatin1String("DERP")); }
  QString text() const;
};

Ping parsePing(const QString& output);

// Locates the CLI: `overridePath` if set, then the macOS app bundle, Homebrew
// and the usual bin folders (a .app does not inherit the shell PATH), then PATH.
QString findBinary(const QString& overridePath = {});

}  // namespace magcs::tailscale
