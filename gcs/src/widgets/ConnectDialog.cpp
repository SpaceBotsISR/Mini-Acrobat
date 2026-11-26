#include "widgets/ConnectDialog.hpp"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "core/Drone.hpp"
#include "core/NetworkMonitor.hpp"
#include "widgets/LinkText.hpp"
#include "widgets/MacNetworkBox.hpp"

namespace magcs {

using Mode = ConnectionTarget::Mode;

namespace {

const Mode kModes[] = {Mode::Tailscale, Mode::Manual, Mode::Simulator};

enum PeerColumn { ColName, ColIp, ColOs, ColStatus, ColPath, ColCount };

}  // namespace

ConnectDialog::ConnectDialog(Drone* drone, NetworkMonitor* monitor, Purpose purpose, QWidget* parent)
: QDialog(parent), m_drone(drone), m_monitor(monitor), m_purpose(purpose)
{
  setWindowTitle(tr("Connect to Mini-Acrobat"));
  resize(900, 640);

  auto* title = new QLabel(tr("<h2 style='margin:0'>Connect to Mini-Acrobat</h2>"
                              "<span style='color:gray'>Choose how this Mac reaches the drone's "
                              "rosbridge.</span>"));

  // ---- modes -----------------------------------------------------------------
  m_modes = new QListWidget;
  m_modes->setFixedWidth(210);
  m_modes->setSpacing(2);
  for (Mode m : kModes) {
    auto* item = new QListWidgetItem(ConnectionTarget::modeName(m));
    item->setData(Qt::UserRole, static_cast<int>(m));
    item->setSizeHint(QSize(0, 34));
    m_modes->addItem(item);
  }

  m_pages = new QStackedWidget;
  m_pages->addWidget(buildTailscalePage());
  m_pages->addWidget(buildManualPage());
  m_pages->addWidget(buildSimulatorPage());

  auto* top = new QHBoxLayout;
  top->addWidget(m_modes);
  top->addWidget(m_pages, 1);

  // ---- bottom: this Mac + port + status + buttons -----------------------------
  m_port = new QSpinBox;
  m_port->setRange(1, 65535);
  auto* portBox = new QFormLayout;
  portBox->addRow(tr("rosbridge port"), m_port);

  auto* bottom = new QHBoxLayout;
  bottom->addWidget(new MacNetworkBox(m_monitor), 1);
  bottom->addLayout(portBox);

  m_status = new QLabel;
  m_status->setWordWrap(true);
  m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);

  auto* buttons = new QDialogButtonBox;
  m_connect = buttons->addButton(tr("Connect"), QDialogButtonBox::AcceptRole);
  m_connect->setDefault(true);
  if (m_purpose == Purpose::Startup) {
    auto* offline = buttons->addButton(tr("Open without connecting"), QDialogButtonBox::ActionRole);
    connect(offline, &QPushButton::clicked, this, [this]() {
      cancelConnect();
      accept();
    });
    buttons->addButton(tr("Quit"), QDialogButtonBox::RejectRole);
  } else {
    buttons->addButton(tr("Close"), QDialogButtonBox::RejectRole);
  }
  connect(buttons, &QDialogButtonBox::rejected, this, &ConnectDialog::reject);
  connect(m_connect, &QPushButton::clicked, this, [this]() {
    m_connecting ? cancelConnect() : startConnect();
  });

  auto* root = new QVBoxLayout(this);
  root->addWidget(title);
  root->addLayout(top, 1);
  root->addLayout(bottom);
  root->addWidget(m_status);
  root->addWidget(buttons);

  // ---- initial state (current target, or the saved one) -----------------------
  const ConnectionTarget t = m_drone->target();
  m_port->setValue(t.port);
  m_tsHost->setText(ConnectionTarget::savedHost(Mode::Tailscale));
  m_manualHost->setText(ConnectionTarget::savedHost(Mode::Manual));
  if (t.mode != Mode::Simulator) {
    (t.mode == Mode::Tailscale ? m_tsHost : m_manualHost)->setText(t.host);
  }
  m_modes->setCurrentRow(static_cast<int>(t.mode));
  m_pages->setCurrentIndex(static_cast<int>(t.mode));

  connect(m_modes, &QListWidget::currentRowChanged, this, &ConnectDialog::onModeChanged);
  connect(m_monitor, &NetworkMonitor::tailscaleChanged, this, &ConnectDialog::refreshTailscale);
  connect(m_monitor, &NetworkMonitor::pingChanged, this, &ConnectDialog::refreshPing);
  connect(m_drone, &Drone::connectionChanged, this, [this](bool connected) {
    if (connected && m_connecting) {
      m_connecting = false;
      m_slowTimer.stop();
      accept();
    }
  });
  connect(m_drone, &Drone::log, this, [this](const QString& text) {
    if (m_connecting && text.contains(QLatin1String("error"), Qt::CaseInsensitive)) {
      setStatus(QStringLiteral("Connecting to %1 ...<br><span style='color:gray'>%2</span>")
                  .arg(currentTarget().url().toHtmlEscaped(), text.toHtmlEscaped()));
    }
  });

  m_slowTimer.setSingleShot(true);
  m_slowTimer.setInterval(6000);
  connect(&m_slowTimer, &QTimer::timeout, this, [this]() {
    setStatus(tr("<span style='color:#b08800'>Still trying %1 (MAgcs keeps retrying).</span> "
                 "Check: drone powered and online, host right, and on the drone "
                 "<code>ros2 launch mini_acrobat_bringup bringup.launch.py</code> running.")
                .arg(currentTarget().url().toHtmlEscaped()));
  });

  if (m_drone->isConnected()) {
    setStatus(tr("Currently connected to <b>%1</b>. Connect switches to the selection.")
                .arg(m_drone->target().url().toHtmlEscaped()));
  }

  m_monitor->refreshNow();
  onModeChanged();
  refreshTailscale();
  refreshPing();
}

QLabel* ConnectDialog::howTo(const QString& html)
{
  auto* l = new QLabel(html);
  l->setWordWrap(true);
  l->setTextFormat(Qt::RichText);
  l->setTextInteractionFlags(Qt::TextBrowserInteraction);
  l->setOpenExternalLinks(true);
  l->setAlignment(Qt::AlignTop | Qt::AlignLeft);
  return l;
}

QWidget* ConnectDialog::buildTailscalePage()
{
  auto* page = new QWidget;

  m_tsState = new QLabel;
  m_tsState->setWordWrap(true);
  auto* refresh = new QPushButton(tr("Refresh"));
  connect(refresh, &QPushButton::clicked, m_monitor, &NetworkMonitor::refreshNow);
  auto* stateRow = new QHBoxLayout;
  stateRow->addWidget(m_tsState, 1);
  stateRow->addWidget(refresh);

  m_peers = new QTreeWidget;
  m_peers->setColumnCount(ColCount);
  m_peers->setHeaderLabels({tr("Device"), tr("Tailscale IP"), tr("OS"), tr("Status"), tr("Path")});
  m_peers->setRootIsDecorated(false);
  m_peers->setUniformRowHeights(true);
  m_peers->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
  m_peers->header()->setStretchLastSection(true);
  m_peers->setMinimumHeight(140);
  connect(m_peers, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* item) {
    m_tsHost->setText(item->data(ColName, Qt::UserRole).toString());
    updatePingTarget();
  });
  connect(m_peers, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item) {
    m_tsHost->setText(item->data(ColName, Qt::UserRole).toString());
    startConnect();
  });

  m_tsHost = new QLineEdit;
  m_tsHost->setPlaceholderText(tr("MagicDNS name or 100.x IP, e.g. mini-acrobat"));
  connect(m_tsHost, &QLineEdit::editingFinished, this, &ConnectDialog::updatePingTarget);
  connect(m_tsHost, &QLineEdit::returnPressed, this, &ConnectDialog::startConnect);

  m_tsPath = new QLabel;
  m_tsPath->setTextInteractionFlags(Qt::TextSelectableByMouse);
  m_tsTest = new QPushButton(tr("Test path"));
  m_tsTest->setToolTip(tr("tailscale ping: direct or relayed, and round-trip time"));
  connect(m_tsTest, &QPushButton::clicked, this, [this]() {
    updatePingTarget();
    m_monitor->pingNow();
  });
  auto* pathRow = new QHBoxLayout;
  pathRow->addWidget(m_tsPath, 1);
  pathRow->addWidget(m_tsTest);

  m_tsWarn = new QLabel;
  m_tsWarn->setWordWrap(true);

  auto* form = new QFormLayout;
  form->addRow(tr("Drone"), m_tsHost);
  form->addRow(tr("Path"), pathRow);
  form->addRow(QString(), m_tsWarn);

  auto* help = howTo(tr(
    "<b>How to connect</b><ol style='margin-left:-20px'>"
    "<li>On this Mac: Tailscale app running and logged in to the team tailnet.</li>"
    "<li>On the drone (once): <code>curl -fsSL https://tailscale.com/install.sh | sh</code> "
    "then <code>sudo tailscale up --hostname=mini-acrobat</code>.</li>"
    "<li>The drone needs internet (phone hotspot or any Wi-Fi) for Tailscale to come up. "
    "Mac and drone on the <i>same</i> network give a <b>direct</b> path; otherwise traffic is "
    "relayed (DERP) and too slow for the 300 ms heartbeat.</li>"
    "<li>Pick the drone in the list (or type its name / 100.x IP) and press <b>Connect</b>. "
    "On the drone <code>ros2 launch mini_acrobat_bringup bringup.launch.py</code> must be "
    "running.</li></ol>"));

  auto* v = new QVBoxLayout(page);
  v->setContentsMargins(0, 0, 0, 0);
  v->addLayout(stateRow);
  v->addWidget(m_peers, 1);
  v->addLayout(form);
  v->addWidget(help);
  return page;
}

QWidget* ConnectDialog::buildManualPage()
{
  auto* page = new QWidget;
  m_manualHost = new QLineEdit;
  m_manualHost->setPlaceholderText(tr("raspberrypi.local or 192.168.x.y"));
  connect(m_manualHost, &QLineEdit::returnPressed, this, &ConnectDialog::startConnect);

  auto* form = new QFormLayout;
  form->addRow(tr("Drone host"), m_manualHost);

  auto* help = howTo(tr(
    "<b>How to connect</b><ol style='margin-left:-20px'>"
    "<li>Put the Mac and the drone on the same network (e.g. the phone hotspot).</li>"
    "<li>Host: the drone's mDNS name (<code>raspberrypi.local</code>) or its IP "
    "(<code>hostname -I</code> on the drone, or the hotspot's device list).</li>"
    "<li>On the drone run <code>ros2 launch mini_acrobat_bringup bringup.launch.py</code> "
    "and press <b>Connect</b>.</li></ol>"
    "<span style='color:gray'>rosbridge has no authentication: only use trusted networks.</span>"));

  auto* v = new QVBoxLayout(page);
  v->setContentsMargins(0, 0, 0, 0);
  v->addLayout(form);
  v->addWidget(help);
  v->addStretch();
  return page;
}

QWidget* ConnectDialog::buildSimulatorPage()
{
  auto* page = new QWidget;
  auto* help = howTo(tr(
    "<b>Fake drone on this Mac</b> (no Pi, no ROS). It mimics the safety node, including the "
    "300 ms link-loss disarm, and publishes a fake <code>/link/status</code>."
    "<ol style='margin-left:-20px'>"
    "<li><code>python3 -m pip install websockets</code> (once)</li>"
    "<li>From the repo: <code>python3 gcs/tools/fake_drone/fake_drone.py --port 9090 -v</code></li>"
    "<li>Press <b>Connect</b> (connects to <code>localhost</code>).</li></ol>"));
  auto* v = new QVBoxLayout(page);
  v->setContentsMargins(0, 0, 0, 0);
  v->addWidget(help);
  v->addStretch();
  return page;
}

Mode ConnectDialog::currentMode() const
{
  const QListWidgetItem* item = m_modes->currentItem();
  return item ? static_cast<Mode>(item->data(Qt::UserRole).toInt()) : Mode::Tailscale;
}

ConnectionTarget ConnectDialog::currentTarget() const
{
  ConnectionTarget t;
  t.mode = currentMode();
  t.port = static_cast<quint16>(m_port->value());
  switch (t.mode) {
    case Mode::Tailscale: t.host = m_tsHost->text().trimmed(); break;
    case Mode::Manual: t.host = m_manualHost->text().trimmed(); break;
    case Mode::Simulator: t.host = QStringLiteral("localhost"); break;
  }
  return t;
}

void ConnectDialog::onModeChanged()
{
  m_pages->setCurrentIndex(static_cast<int>(currentMode()));
  updatePingTarget();
}

void ConnectDialog::updatePingTarget()
{
  m_monitor->setPingTarget(currentMode() == Mode::Tailscale ? m_tsHost->text() : QString());
}

void ConnectDialog::refreshTailscale()
{
  const tailscale::Status& s = m_monitor->tailscale();
  m_tsState->setText(linktext::tailscaleState(s));

  const QString host = m_tsHost->text().trimmed();
  const tailscale::Node* selected = s.findPeer(host);
  m_peers->clear();
  for (const tailscale::Node& n : s.peers) {
    auto* item = new QTreeWidgetItem;
    item->setText(ColName, n.shortName());
    // What to connect to: the MagicDNS short name if there is one, else the IP.
    item->setData(ColName, Qt::UserRole, n.dnsName.isEmpty() ? n.ipv4() : n.shortName());
    item->setText(ColIp, n.ipv4());
    item->setText(ColOs, n.os);
    item->setText(ColStatus, n.online ? tr("online") : tr("offline"));
    item->setForeground(ColStatus, QColor(n.online ? "#1a7f37" : "#888888"));
    item->setText(ColPath, n.connectionText());
    if (!n.online) {
      for (int c : {ColName, ColIp, ColOs, ColPath}) {
        item->setForeground(c, QColor("#888888"));
      }
    }
    m_peers->addTopLevelItem(item);
    if (selected && n.hostName == selected->hostName && n.ips == selected->ips) {
      item->setSelected(true);
      m_peers->setCurrentItem(item);
    }
  }
  if (s.running() && s.peers.isEmpty()) {
    m_tsState->setText(m_tsState->text() + tr("<br>No other devices in this tailnet yet."));
  } else if (s.running() && !host.isEmpty() && !selected) {
    m_tsState->setText(m_tsState->text() +
                       tr("<br><span style='color:#b08800'>'%1' is not in this tailnet.</span>")
                         .arg(host.toHtmlEscaped()));
  } else if (selected && !selected->online) {
    m_tsState->setText(m_tsState->text() +
                       tr("<br><span style='color:#c0392b'>%1 is offline (drone off, no internet, "
                          "or tailscaled not running).</span>")
                         .arg(selected->shortName().toHtmlEscaped()));
  }
}

void ConnectDialog::refreshPing()
{
  const tailscale::Ping& p = m_monitor->ping();
  m_tsPath->setText(m_monitor->pingTarget().isEmpty()
                      ? QStringLiteral("-")
                      : linktext::tailscalePath(p, m_monitor->pinging()));
  m_tsWarn->setText(linktext::tailscalePathWarning(p));
  m_tsWarn->setVisible(!m_tsWarn->text().isEmpty());
  m_tsTest->setEnabled(!m_monitor->pinging() && !m_monitor->tailscaleBinary().isEmpty());
}

void ConnectDialog::startConnect()
{
  const ConnectionTarget t = currentTarget();
  if (t.effectiveHost().isEmpty()) {
    setStatus(tr("<span style='color:#c0392b'>Enter the drone's host first.</span>"));
    return;
  }
  if (m_drone->client()->state() != RosbridgeClient::State::Disconnected) {
    m_drone->disconnectFromHost();
  }
  m_connecting = true;
  m_connect->setText(tr("Cancel"));
  m_modes->setEnabled(false);
  m_pages->setEnabled(false);
  m_port->setEnabled(false);
  setStatus(tr("Connecting to <b>%1</b> ...").arg(t.url().toHtmlEscaped()));
  m_slowTimer.start();
  m_drone->connectTo(t);
}

void ConnectDialog::cancelConnect()
{
  if (!m_connecting) {
    return;
  }
  m_connecting = false;
  m_slowTimer.stop();
  m_drone->disconnectFromHost();
  m_connect->setText(tr("Connect"));
  m_modes->setEnabled(true);
  m_pages->setEnabled(true);
  m_port->setEnabled(true);
  setStatus(tr("Cancelled."));
}

void ConnectDialog::reject()
{
  cancelConnect();
  QDialog::reject();
}

void ConnectDialog::setStatus(const QString& html)
{
  m_status->setText(html);
}

}  // namespace magcs
