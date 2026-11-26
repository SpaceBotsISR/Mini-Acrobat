#include "core/NetworkMonitor.hpp"

#include <QNetworkInterface>

namespace magcs {

namespace {

bool isTailscaleIp(const QHostAddress& a)
{
  // Tailscale hands out addresses from the CGNAT range 100.64.0.0/10.
  return a.isInSubnet(QHostAddress(QStringLiteral("100.64.0.0")), 10);
}

}  // namespace

NetworkMonitor::NetworkMonitor(QObject* parent)
: QObject(parent)
{
  m_tsBinary = tailscale::findBinary();

  m_localTimer.setInterval(1000);
  connect(&m_localTimer, &QTimer::timeout, this, &NetworkMonitor::pollLocal);
  m_localTimer.start();

  m_tsTimer.setInterval(3000);
  connect(&m_tsTimer, &QTimer::timeout, this, &NetworkMonitor::pollTailscale);
  m_tsTimer.start();

  m_pingTimer.setInterval(5000);
  connect(&m_pingTimer, &QTimer::timeout, this, &NetworkMonitor::pingNow);

  pollLocal();
  pollTailscale();
}

NetworkMonitor::~NetworkMonitor()
{
  for (QProcess* p : {m_statusProc.data(), m_pingProc.data()}) {
    if (p) {
      p->disconnect();
      p->kill();
      p->waitForFinished(500);
    }
  }
}

void NetworkMonitor::refreshNow()
{
  m_tsBinary = tailscale::findBinary();
  pollLocal();
  pollTailscale();
}

void NetworkMonitor::pollLocal()
{
  m_wifi = readWifiInfo();

  QStringList out;
  for (const QNetworkInterface& itf : QNetworkInterface::allInterfaces()) {
    const auto flags = itf.flags();
    if (!(flags & QNetworkInterface::IsUp) || !(flags & QNetworkInterface::IsRunning) ||
        (flags & QNetworkInterface::IsLoopBack))
    {
      continue;
    }
    for (const QNetworkAddressEntry& e : itf.addressEntries()) {
      const QHostAddress ip = e.ip();
      if (ip.protocol() != QAbstractSocket::IPv4Protocol) {
        continue;
      }
      QString line = QStringLiteral("%1  %2/%3").arg(itf.humanReadableName(), ip.toString())
                       .arg(e.prefixLength());
      if (isTailscaleIp(ip)) {
        line += QStringLiteral("  (tailscale)");
      }
      out << line;
    }
  }
  m_addresses = out;
  emit localChanged();
}

QProcess* NetworkMonitor::runCli(const QStringList& args, int timeoutMs)
{
  auto* p = new QProcess(this);
  p->setProcessChannelMode(QProcess::MergedChannels);
  // Kill a hung CLI (e.g. tailscaled not answering).
  QPointer<QProcess> guard(p);
  QTimer::singleShot(timeoutMs, this, [guard]() {
    if (guard && guard->state() != QProcess::NotRunning) {
      guard->kill();
    }
  });
  p->start(m_tsBinary, args);
  return p;
}

void NetworkMonitor::pollTailscale()
{
  if (m_statusProc) {
    return;  // previous call still running
  }
  if (m_tsBinary.isEmpty()) {
    m_ts = tailscale::Status{};
    m_ts.error = QStringLiteral("Tailscale is not installed (CLI not found)");
    emit tailscaleChanged();
    return;
  }
  QProcess* p = runCli({QStringLiteral("status"), QStringLiteral("--json")}, 4000);
  m_statusProc = p;
  connect(p, &QProcess::finished, this, [this, p](int code, QProcess::ExitStatus st) {
    const QByteArray out = p->readAll();
    p->deleteLater();
    m_statusProc = nullptr;
    // `status --json` prints the JSON even when logged out / stopped.
    tailscale::Status s = tailscale::parseStatus(out);
    if (!s.ok) {
      s.error = (st == QProcess::CrashExit) ? QStringLiteral("tailscale CLI timed out")
                : out.trimmed().isEmpty()   ? QStringLiteral("tailscale CLI failed (exit %1)").arg(code)
                                            : QString::fromUtf8(out.trimmed()).section('\n', 0, 0);
    }
    m_ts = s;
    emit tailscaleChanged();
  });
  connect(p, &QProcess::errorOccurred, this, [this, p](QProcess::ProcessError e) {
    if (e != QProcess::FailedToStart) {
      return;  // finished() follows
    }
    p->deleteLater();
    m_statusProc = nullptr;
    m_ts = tailscale::Status{};
    m_ts.error = QStringLiteral("cannot run %1").arg(m_tsBinary);
    emit tailscaleChanged();
  });
}

void NetworkMonitor::setPingTarget(const QString& host)
{
  const QString h = host.trimmed();
  if (h == m_pingTarget) {
    return;
  }
  m_pingTarget = h;
  m_ping = tailscale::Ping{};
  emit pingChanged();
  if (h.isEmpty()) {
    m_pingTimer.stop();
  } else {
    m_pingTimer.start();
    pingNow();
  }
}

void NetworkMonitor::pingNow()
{
  if (m_pingProc || m_pingTarget.isEmpty() || m_tsBinary.isEmpty()) {
    return;
  }
  const QString target = m_pingTarget;
  QProcess* p = runCli({QStringLiteral("ping"), QStringLiteral("-c"), QStringLiteral("1"),
                        QStringLiteral("--timeout"), QStringLiteral("2s"), target},
                       4000);
  m_pingProc = p;
  emit pingChanged();  // "pinging..."
  connect(p, &QProcess::finished, this, [this, p, target](int, QProcess::ExitStatus) {
    const QString out = QString::fromUtf8(p->readAll());
    p->deleteLater();
    m_pingProc = nullptr;
    if (target != m_pingTarget) {
      return;  // target changed meanwhile
    }
    m_ping = tailscale::parsePing(out);
    emit pingChanged();
  });
  connect(p, &QProcess::errorOccurred, this, [this, p](QProcess::ProcessError e) {
    if (e != QProcess::FailedToStart) {
      return;
    }
    p->deleteLater();
    m_pingProc = nullptr;
    m_ping = tailscale::Ping{};
    m_ping.error = QStringLiteral("cannot run %1").arg(m_tsBinary);
    emit pingChanged();
  });
}

}  // namespace magcs
