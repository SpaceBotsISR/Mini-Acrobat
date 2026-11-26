#pragma once
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QStringList>
#include <QTimer>

#include "core/Tailscale.hpp"
#include "platform/WifiInfo.hpp"

namespace magcs {

// Network diagnostics of THIS computer, polled in the background:
//   - Wi-Fi (1 Hz) and local IPv4 addresses
//   - `tailscale status --json` (every 3 s)
//   - `tailscale ping -c 1 <target>` (every 5 s, only when a target is set)
// The CLI runs in QProcess (async), so the GUI thread (which also sends the
// safety heartbeat) never blocks on it.
class NetworkMonitor : public QObject {
  Q_OBJECT
public:
  explicit NetworkMonitor(QObject* parent = nullptr);
  ~NetworkMonitor() override;

  const WifiInfo& wifi() const { return m_wifi; }
  // "<iface>  <addr>/<prefix>", IPv4, up, not loopback. Tailscale ones end in "(tailscale)".
  const QStringList& localAddresses() const { return m_addresses; }

  const tailscale::Status& tailscale() const { return m_ts; }
  QString tailscaleBinary() const { return m_tsBinary; }

  // Host to probe with `tailscale ping`. Empty: no probing.
  void setPingTarget(const QString& host);
  QString pingTarget() const { return m_pingTarget; }
  const tailscale::Ping& ping() const { return m_ping; }
  bool pinging() const { return !m_pingProc.isNull(); }

  void refreshNow();   // Wi-Fi, addresses and Tailscale status
  void pingNow();

signals:
  void localChanged();       // wifi() / localAddresses()
  void tailscaleChanged();
  void pingChanged();

private:
  void pollLocal();
  void pollTailscale();
  QProcess* runCli(const QStringList& args, int timeoutMs);

  QTimer m_localTimer;
  QTimer m_tsTimer;
  QTimer m_pingTimer;
  WifiInfo m_wifi;
  QStringList m_addresses;
  QString m_tsBinary;
  tailscale::Status m_ts;
  QString m_pingTarget;
  tailscale::Ping m_ping;
  QPointer<QProcess> m_statusProc;
  QPointer<QProcess> m_pingProc;
};

}  // namespace magcs
