#include "widgets/ConnectionWidget.hpp"

#include <QDateTime>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>

#include "core/Drone.hpp"
#include "core/NetworkMonitor.hpp"
#include "widgets/LinkText.hpp"
#include "widgets/MacNetworkBox.hpp"
#include "widgets/SignalBars.hpp"

namespace magcs {

using Mode = ConnectionTarget::Mode;

namespace {

QLabel* selectable(QLabel* l)
{
  l->setTextInteractionFlags(Qt::TextSelectableByMouse);
  l->setWordWrap(true);
  return l;
}

}  // namespace

ConnectionWidget::ConnectionWidget(Drone* drone, NetworkMonitor* monitor, QWidget* parent)
: QWidget(parent), m_drone(drone), m_monitor(monitor)
{
  // ---- link -------------------------------------------------------------------
  m_mode = selectable(new QLabel);
  m_target = selectable(new QLabel);
  m_button = new QPushButton(tr("Connect"));
  m_change = new QPushButton(tr("Change..."));
  m_change->setToolTip(tr("Choose how to connect (Cmd+K)"));
  m_status = new QLabel;
  m_addresses = selectable(new QLabel);
  m_stats = selectable(new QLabel);
  m_stats->setStyleSheet("color: gray;");
  m_heartbeat = new QLabel;
  m_heartbeat->setToolTip(tr("Heartbeats MAgcs actually sent in the last second (target 20 Hz). "
                             "The drone disarms if none arrives for 300 ms."));

  auto* buttons = new QHBoxLayout;
  buttons->addWidget(m_button);
  buttons->addWidget(m_change);

  auto* linkBox = new QGroupBox(tr("Link"));
  auto* linkForm = new QFormLayout(linkBox);
  linkForm->addRow(tr("Mode"), m_mode);
  linkForm->addRow(tr("Target"), m_target);
  linkForm->addRow(buttons);
  linkForm->addRow(tr("Status"), m_status);
  linkForm->addRow(tr("Socket"), m_addresses);
  linkForm->addRow(tr("Heartbeat"), m_heartbeat);
  linkForm->addRow(m_stats);

  // ---- tailscale ----------------------------------------------------------------
  m_tsPeer = selectable(new QLabel);
  m_tsPath = selectable(new QLabel);
  m_tsWarn = selectable(new QLabel);
  auto* tsBox = new QGroupBox(tr("Tailscale"));
  auto* tsForm = new QFormLayout(tsBox);
  tsForm->addRow(tr("Drone"), m_tsPeer);
  tsForm->addRow(tr("Path"), m_tsPath);
  tsForm->addRow(m_tsWarn);
  m_tsBox = tsBox;

  // ---- drone side ---------------------------------------------------------------
  m_droneBars = new SignalBars;
  m_droneWifi = selectable(new QLabel);
  m_droneIps = selectable(new QLabel);
  m_droneAge = new QLabel;
  m_droneAge->setStyleSheet("color: gray;");
  auto* droneWifiRow = new QHBoxLayout;
  droneWifiRow->addWidget(m_droneBars, 0, Qt::AlignVCenter);
  droneWifiRow->addWidget(m_droneWifi, 1);
  auto* droneBox = new QGroupBox(tr("Drone"));
  auto* droneForm = new QFormLayout(droneBox);
  droneForm->addRow(tr("Wi-Fi"), droneWifiRow);
  droneForm->addRow(tr("IPs"), m_droneIps);
  droneForm->addRow(m_droneAge);

  auto* content = new QWidget;
  auto* v = new QVBoxLayout(content);
  v->addWidget(linkBox);
  v->addWidget(tsBox);
  v->addWidget(new MacNetworkBox(m_monitor));
  v->addWidget(droneBox);
  v->addStretch();

  auto* scroll = new QScrollArea;
  scroll->setWidget(content);
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  content->setMinimumWidth(300);
  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);
  root->addWidget(scroll);

  connect(m_button, &QPushButton::clicked, this, &ConnectionWidget::toggle);
  connect(m_change, &QPushButton::clicked, this, &ConnectionWidget::changeRequested);
  connect(m_drone, &Drone::connectionChanged, this, &ConnectionWidget::refreshLink);
  connect(m_drone, &Drone::targetChanged, this, [this]() {
    refreshLink();
    refreshTailscale();
  });
  connect(m_drone->client(), &RosbridgeClient::stateChanged, this, &ConnectionWidget::refreshLink);
  connect(m_drone->client(), &RosbridgeClient::rttMeasured, this, [this](int ms) {m_rttMs = ms;});
  connect(m_drone, &Drone::linkStatusReceived, this, &ConnectionWidget::refreshDrone);
  connect(m_monitor, &NetworkMonitor::tailscaleChanged, this, &ConnectionWidget::refreshTailscale);
  connect(m_monitor, &NetworkMonitor::pingChanged, this, &ConnectionWidget::refreshTailscale);

  auto* timer = new QTimer(this);
  connect(timer, &QTimer::timeout, this, [this]() {
    refreshStats();
    refreshDrone();  // ages
  });
  timer->start(1000);

  refreshLink();
  refreshStats();
  refreshTailscale();
  refreshDrone();
}

void ConnectionWidget::connectNow()
{
  m_drone->connectTo(m_drone->target());
}

void ConnectionWidget::toggle()
{
  if (m_drone->client()->state() == RosbridgeClient::State::Disconnected) {
    connectNow();
  } else {
    m_drone->disconnectFromHost();
  }
}

void ConnectionWidget::refreshLink()
{
  const ConnectionTarget& t = m_drone->target();
  m_mode->setText(ConnectionTarget::modeName(t.mode));
  m_target->setText(t.url());

  using S = RosbridgeClient::State;
  switch (m_drone->client()->state()) {
    case S::Connected:
      m_status->setText(tr("connected"));
      m_status->setStyleSheet("color: #3dd68c; font-weight: bold;");
      m_button->setText(tr("Disconnect"));
      m_addresses->setText(tr("drone %1\nfrom Mac %2")
                             .arg(m_drone->client()->peerAddress().toString(),
                                  m_drone->client()->localAddress().toString()));
      break;
    case S::Connecting:
      m_status->setText(tr("connecting..."));
      m_status->setStyleSheet("color: #b08800; font-weight: bold;");
      m_button->setText(tr("Cancel"));
      m_addresses->setText(QStringLiteral("-"));
      break;
    case S::Disconnected:
      m_status->setText(tr("disconnected"));
      m_status->setStyleSheet("color: #e85d5d; font-weight: bold;");
      m_button->setText(tr("Connect"));
      m_addresses->setText(QStringLiteral("-"));
      break;
  }
}

void ConnectionWidget::refreshStats()
{
  const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
  const double dt = m_lastStatsMs > 0 ? qMax<qint64>(1, nowMs - m_lastStatsMs) / 1000.0 : 1.0;
  m_lastStatsMs = nowMs;

  const quint64 rx = m_drone->client()->messagesReceived();
  m_stats->setText(tr("RTT %1 ms   rx %2 msg/s   heartbeats sent %3")
                     .arg(m_rttMs >= 0 && m_drone->isConnected() ? QString::number(m_rttMs)
                                                                 : QStringLiteral("-"))
                     .arg(static_cast<double>(rx - m_lastRx) / dt, 0, 'f', 0)
                     .arg(m_drone->heartbeat()->sentCount()));
  m_lastRx = rx;

  // Heartbeat rate actually achieved (sent count per second).
  const quint32 hb = m_drone->heartbeat()->sentCount();
  const double hz = hb >= m_lastHb ? (hb - m_lastHb) / dt : 0.0;
  m_lastHb = hb;
  if (!m_drone->isConnected()) {
    m_heartbeat->setText(QStringLiteral("-"));
    m_heartbeat->setStyleSheet(QString());
  } else if (m_drone->heartbeat()->paused()) {
    m_heartbeat->setText(tr("PAUSED (simulated link loss)"));
    m_heartbeat->setStyleSheet("color: #e0a800; font-weight: bold;");
  } else {
    m_heartbeat->setText(tr("%1 Hz  (target 20)").arg(hz, 0, 'f', 1));
    m_heartbeat->setStyleSheet(hz >= 18.0 ? "color: #3dd68c; font-weight: bold;"
                               : hz >= 10.0 ? "color: #e0a800; font-weight: bold;"
                                            : "color: #e85d5d; font-weight: bold;");
  }
}

void ConnectionWidget::refreshTailscale()
{
  const bool ts = m_drone->target().mode == Mode::Tailscale;
  m_tsBox->setVisible(ts);
  if (!ts) {
    return;
  }
  const tailscale::Status& s = m_monitor->tailscale();
  if (!s.running()) {
    m_tsPeer->setText(linktext::tailscaleState(s));
  } else if (const tailscale::Node* n = s.findPeer(m_drone->target().host)) {
    m_tsPeer->setText(QStringLiteral("%1  %2  ·  %3")
                        .arg(n->shortName(), n->ipv4(),
                             n->online ? n->connectionText() : tr("OFFLINE")));
  } else {
    m_tsPeer->setText(tr("'%1' not found in the tailnet").arg(m_drone->target().host));
  }
  m_tsPath->setText(linktext::tailscalePath(m_monitor->ping(), m_monitor->pinging()));
  m_tsWarn->setText(linktext::tailscalePathWarning(m_monitor->ping()));
  m_tsWarn->setVisible(!m_tsWarn->text().isEmpty());
}

void ConnectionWidget::refreshDrone()
{
  const DroneLinkStatus& s = m_drone->linkStatus();
  m_droneBars->setRssi(s.valid && s.wifiConnected ? s.rssiDbm : 0);
  if (!m_drone->isConnected()) {
    m_droneWifi->setText(tr("not connected"));
    m_droneIps->setText(QStringLiteral("-"));
    m_droneAge->clear();
    return;
  }
  m_droneWifi->setText(linktext::droneWifi(s));
  QStringList ips = s.addresses;
  if (s.tailscaleUp && !s.tailscaleIp.isEmpty() && ips.filter(s.tailscaleIp).isEmpty()) {
    ips << QStringLiteral("tailscale  %1").arg(s.tailscaleIp);
  }
  m_droneIps->setText(ips.isEmpty() ? QStringLiteral("-") : ips.join('\n'));
  const qint64 age = m_drone->linkStatusAgeMs();
  if (age < 0) {
    m_droneAge->clear();
  } else {
    m_droneAge->setText(age > 3000 ? tr("<span style='color:#c0392b'>stale: %1 s old</span>")
                                       .arg(age / 1000)
                                   : tr("%1  ·  updated %2 s ago")
                                       .arg(s.hostname)
                                       .arg(age / 1000));
  }
}

}  // namespace magcs
