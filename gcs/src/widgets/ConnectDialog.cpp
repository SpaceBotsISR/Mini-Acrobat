#include "widgets/ConnectDialog.hpp"

#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkProxy>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStyle>
#include <QTabBar>
#include <QTcpSocket>
#include <QToolButton>
#include <QVBoxLayout>

#include "core/Drone.hpp"
#include "core/NetworkMonitor.hpp"
#include "widgets/LinkText.hpp"
#include "widgets/MacNetworkBox.hpp"
#include "widgets/SignalBars.hpp"

namespace magcs {

using Mode = ConnectionTarget::Mode;

namespace {

const Mode kModes[] = {Mode::Tailscale, Mode::Manual, Mode::Simulator};

constexpr int kProbeIntervalMs = 2000;
constexpr int kProbeTimeoutMs = 1500;

QString tabName(Mode m)
{
  switch (m) {
    case Mode::Tailscale: return QObject::tr("Tailscale");
    case Mode::Manual: return QObject::tr("Manual IP");
    case Mode::Simulator: return QObject::tr("Simulator");
  }
  return {};
}

QString placeholder(Mode m)
{
  switch (m) {
    case Mode::Tailscale: return QObject::tr("Tailscale name or 100.x IP, e.g. mini-acrobat");
    case Mode::Manual: return QObject::tr("raspberrypi.local or 192.168.x.y");
    case Mode::Simulator: return QStringLiteral("localhost");
  }
  return {};
}

// This Mac's Tailscale address from the interface list, e.g. "100.90.89.80".
// Works even when the tailscale CLI can't be queried.
QString macTailscaleIp(const QStringList& addresses)
{
  for (const QString& line : addresses) {
    if (line.endsWith(QLatin1String("(tailscale)"))) {
      // "utun9  100.90.89.80/32  (tailscale)"
      const QStringList parts = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
      if (parts.size() >= 2) {
        return parts.at(1).section(QLatin1Char('/'), 0, 0);
      }
    }
  }
  return {};
}

QString gray(const QString& s)
{
  return QStringLiteral("<span style='color:gray'>%1</span>").arg(s);
}

const char* kConnectReady =
  "QPushButton { background-color: #2f9e44; color: white; border: 1px solid #2f9e44;"
  " border-radius: 6px; padding: 5px 24px; font-weight: 600; }"
  "QPushButton:hover { background-color: #37b24d; }"
  "QPushButton:pressed { background-color: #237a34; }";
const char* kConnectIdle =
  "QPushButton { background-color: palette(button); color: palette(button-text);"
  " border: 1px solid palette(mid); border-radius: 6px; padding: 5px 24px; }"
  "QPushButton:pressed { background-color: palette(mid); }";

}  // namespace

ConnectDialog::ConnectDialog(Drone* drone, NetworkMonitor* monitor, Purpose purpose, QWidget* parent)
: QDialog(parent), m_drone(drone), m_monitor(monitor), m_purpose(purpose)
{
  setWindowTitle(tr("Connect to Mini-Acrobat"));
  setMinimumWidth(600);

  auto* title = new QLabel(tr("<span style='font-size:17pt; font-weight:600'>Connect to "
                              "Mini-Acrobat</span>"));

  // ---- mode tabs ---------------------------------------------------------------
  m_modes = new QTabBar;
  m_modes->setExpanding(false);
  m_modes->setDrawBase(false);
  m_modes->setDocumentMode(true);
  for (Mode m : kModes) {
    m_modes->addTab(tabName(m));
  }
  m_modes->setTabToolTip(0, tr("Over the team tailnet (default)"));
  m_modes->setTabToolTip(1, tr("Mac and drone on the same local network"));
  m_modes->setTabToolTip(2, tr("Fake drone running on this Mac"));
  auto* modeRow = new QHBoxLayout;
  modeRow->addStretch();
  modeRow->addWidget(m_modes);
  modeRow->addStretch();

  // ---- host : port ⟳ -------------------------------------------------------------
  m_host = new QLineEdit;
  m_host->setClearButtonEnabled(true);
  m_port = new QSpinBox;
  m_port->setRange(1, 65535);
  m_port->setButtonSymbols(QAbstractSpinBox::NoButtons);
  m_port->setAlignment(Qt::AlignCenter);
  m_port->setFixedWidth(64);
  m_port->setToolTip(tr("rosbridge port"));
  m_refresh = new QToolButton;
  m_refresh->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
  m_refresh->setAutoRaise(true);
  m_refresh->setToolTip(tr("Refresh Tailscale state and re-check the drone"));

  auto* hostRow = new QHBoxLayout;
  hostRow->addWidget(new QLabel(tr("Drone")));
  hostRow->addWidget(m_host, 1);
  hostRow->addWidget(new QLabel(QStringLiteral(":")));
  hostRow->addWidget(m_port);
  hostRow->addWidget(m_refresh);

  // ---- readiness summary ----------------------------------------------------------
  m_summary = new QLabel;
  m_summary->setWordWrap(true);
  m_summary->setTextInteractionFlags(Qt::TextSelectableByMouse);
  m_summary->setContentsMargins(2, 2, 0, 2);

  // ---- per-mode dropdowns -----------------------------------------------------------
  m_pages = new QStackedWidget;
  m_pages->addWidget(buildTailscalePage());
  m_pages->addWidget(buildManualPage());
  m_pages->addWidget(buildSimulatorPage());

  // ---- status (connect progress / errors) ------------------------------------------
  m_status = new QLabel;
  m_status->setWordWrap(true);
  m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
  m_status->hide();

  // ---- buttons: exits on the left, Connect on the right ----------------------------
  auto* line = new QFrame;
  line->setFrameShape(QFrame::HLine);
  line->setFrameShadow(QFrame::Sunken);

  auto* buttons = new QHBoxLayout;
  if (m_purpose == Purpose::Startup) {
    auto* quit = new QPushButton(tr("Quit"));
    auto* offline = new QPushButton(tr("Open without connecting"));
    offline->setToolTip(tr("Open MAgcs offline; connect later from Connection > Connect to drone..."));
    for (QPushButton* b : {quit, offline}) {
      b->setAutoDefault(false);
      buttons->addWidget(b);
    }
    connect(quit, &QPushButton::clicked, this, &ConnectDialog::reject);
    connect(offline, &QPushButton::clicked, this, [this]() {
      cancelConnect();
      accept();
    });
  } else {
    auto* close = new QPushButton(tr("Close"));
    close->setAutoDefault(false);
    buttons->addWidget(close);
    connect(close, &QPushButton::clicked, this, &ConnectDialog::reject);
  }
  buttons->addStretch();
  m_connect = new QPushButton(tr("Connect"));
  m_connect->setAutoDefault(false);
  m_connect->setMinimumWidth(130);
  buttons->addWidget(m_connect);
  connect(m_connect, &QPushButton::clicked, this, [this]() {
    m_connecting ? cancelConnect() : startConnect();
  });

  auto* root = new QVBoxLayout(this);
  root->setSpacing(8);
  root->setContentsMargins(20, 16, 20, 14);
  root->addWidget(title);
  root->addLayout(modeRow);
  root->addSpacing(4);
  root->addLayout(hostRow);
  root->addWidget(m_summary);
  root->addWidget(m_pages);
  root->addWidget(buildMacSection());
  root->addWidget(m_status);
  root->addStretch(1);
  root->addWidget(line);
  root->addLayout(buttons);

  // ---- initial state (current target, or the saved ones) ---------------------------
  const ConnectionTarget t = m_drone->target();
  m_port->setValue(t.port);
  m_hostPerMode[static_cast<int>(Mode::Tailscale)] = ConnectionTarget::savedHost(Mode::Tailscale);
  m_hostPerMode[static_cast<int>(Mode::Manual)] = ConnectionTarget::savedHost(Mode::Manual);
  if (t.mode != Mode::Simulator && !t.host.isEmpty()) {
    m_hostPerMode[static_cast<int>(t.mode)] = t.host;
  }
  m_modes->setCurrentIndex(static_cast<int>(t.mode));

  // ---- wiring ----------------------------------------------------------------------
  connect(m_modes, &QTabBar::currentChanged, this, &ConnectDialog::onModeChanged);
  connect(m_host, &QLineEdit::textEdited, this, &ConnectDialog::onHostEdited);
  connect(m_host, &QLineEdit::returnPressed, this, [this]() {
    if (!m_connecting) {
      startConnect();
    }
  });
  connect(m_port, &QSpinBox::valueChanged, this, &ConnectDialog::onHostEdited);
  connect(m_refresh, &QToolButton::clicked, this, [this]() {
    m_monitor->refreshNow();
    restartProbe();
  });

  connect(m_monitor, &NetworkMonitor::tailscaleChanged, this, &ConnectDialog::refreshTailscale);
  connect(m_monitor, &NetworkMonitor::pingChanged, this, &ConnectDialog::refreshPing);
  connect(m_monitor, &NetworkMonitor::localChanged, this, &ConnectDialog::refreshMac);

  connect(m_drone, &Drone::connectionChanged, this, [this](bool connected) {
    if (connected && m_connecting) {
      m_connecting = false;
      m_slowTimer.stop();
      accept();
    }
  });
  connect(m_drone, &Drone::log, this, [this](const QString& text) {
    if (m_connecting && text.contains(QLatin1String("error"), Qt::CaseInsensitive)) {
      setStatus(tr("Connecting to %1 ...<br>%2")
                  .arg(currentTarget().url().toHtmlEscaped(), gray(text.toHtmlEscaped())));
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

  m_probeTimeout.setSingleShot(true);
  m_probeTimeout.setInterval(kProbeTimeoutMs);
  connect(&m_probeTimeout, &QTimer::timeout, this, [this]() {
    dropProbe();
    onProbeResult(false);
  });
  m_probeTimer.setInterval(kProbeIntervalMs);
  connect(&m_probeTimer, &QTimer::timeout, this, &ConnectDialog::probeNow);
  m_editDebounce.setSingleShot(true);
  m_editDebounce.setInterval(400);
  connect(&m_editDebounce, &QTimer::timeout, this, [this]() {
    refreshTailscale();
    restartProbe();
  });

  if (m_drone->isConnected()) {
    setStatus(tr("Currently connected to <b>%1</b>. Connect switches to the selection.")
                .arg(m_drone->target().url().toHtmlEscaped()));
  }

  setReady(false);
  m_monitor->refreshNow();
  onModeChanged();
  refreshTailscale();
  refreshPing();
  refreshMac();
  m_probeTimer.start();
}

// ---- building blocks --------------------------------------------------------------

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

QWidget* ConnectDialog::collapsible(const QString& title, QWidget* body, QWidget* headerExtra)
{
  auto* box = new QWidget;
  auto* header = new QToolButton;
  header->setText(title);
  header->setCheckable(true);
  header->setChecked(false);
  header->setAutoRaise(true);
  header->setArrowType(Qt::RightArrow);
  header->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  header->setStyleSheet(QStringLiteral("QToolButton { border: none; font-weight: 600; padding: 2px 0; }"));

  auto* headerRow = new QHBoxLayout;
  headerRow->setContentsMargins(0, 0, 0, 0);
  headerRow->addWidget(header);
  if (headerExtra) {
    headerRow->addSpacing(6);
    headerRow->addWidget(headerExtra, 1);
  } else {
    headerRow->addStretch(1);
  }

  auto* bodyWrap = new QWidget;
  auto* bodyLayout = new QVBoxLayout(bodyWrap);
  bodyLayout->setContentsMargins(22, 2, 0, 6);
  bodyLayout->addWidget(body);
  bodyWrap->hide();

  auto* v = new QVBoxLayout(box);
  v->setContentsMargins(0, 0, 0, 0);
  v->setSpacing(0);
  v->addLayout(headerRow);
  v->addWidget(bodyWrap);

  connect(header, &QToolButton::toggled, this, [this, header, bodyWrap](bool open) {
    header->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
    bodyWrap->setVisible(open);
    fitToContents();
  });
  return box;
}

QWidget* ConnectDialog::buildTailscalePage()
{
  // Details: only this Mac's Tailscale state and the drone's device, never the
  // rest of the tailnet.
  m_tsState = new QLabel;
  m_tsState->setWordWrap(true);
  m_tsDevice = new QLabel;
  m_tsDevice->setWordWrap(true);
  m_tsIp = new QLabel;
  m_tsIp->setTextInteractionFlags(Qt::TextSelectableByMouse);
  m_tsPath = new QLabel;
  m_tsPath->setTextInteractionFlags(Qt::TextSelectableByMouse);
  m_tsTest = new QPushButton(tr("Test path"));
  m_tsTest->setAutoDefault(false);
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

  auto* details = new QWidget;
  auto* form = new QFormLayout(details);
  form->setContentsMargins(0, 0, 0, 0);
  form->addRow(tr("Tailscale"), m_tsState);
  form->addRow(tr("Device"), m_tsDevice);
  form->addRow(tr("Tailscale IP"), m_tsIp);
  form->addRow(tr("Path"), pathRow);
  form->addRow(QString(), m_tsWarn);

  auto* help = howTo(tr(
    "<ol style='margin-left:-20px'>"
    "<li>On this Mac: Tailscale app running and logged in to the team tailnet.</li>"
    "<li>On the drone (once): <code>curl -fsSL https://tailscale.com/install.sh | sh</code> "
    "then <code>sudo tailscale up --hostname=mini-acrobat</code>.</li>"
    "<li>The drone needs internet (phone hotspot or any Wi-Fi) for Tailscale to come up. "
    "Mac and drone on the <i>same</i> network give a <b>direct</b> path; otherwise traffic is "
    "relayed (DERP) and too slow for the 300 ms heartbeat.</li>"
    "<li>Type the drone's Tailscale name (or 100.x IP) above and press <b>Connect</b>. "
    "On the drone <code>ros2 launch mini_acrobat_bringup bringup.launch.py</code> must be "
    "running.</li></ol>"));

  auto* page = new QWidget;
  auto* v = new QVBoxLayout(page);
  v->setContentsMargins(0, 0, 0, 0);
  v->setSpacing(2);
  v->addWidget(collapsible(tr("Details"), details));
  v->addWidget(collapsible(tr("How to connect"), help));
  return page;
}

QWidget* ConnectDialog::buildManualPage()
{
  auto* help = howTo(tr(
    "<ol style='margin-left:-20px'>"
    "<li>Put the Mac and the drone on the same network (e.g. the phone hotspot).</li>"
    "<li>Host: the drone's mDNS name (<code>raspberrypi.local</code>) or its IP "
    "(<code>hostname -I</code> on the drone, or the hotspot's device list).</li>"
    "<li>On the drone run <code>ros2 launch mini_acrobat_bringup bringup.launch.py</code> "
    "and press <b>Connect</b>.</li></ol>"
    "<span style='color:gray'>rosbridge has no authentication: only use trusted networks.</span>"));

  auto* page = new QWidget;
  auto* v = new QVBoxLayout(page);
  v->setContentsMargins(0, 0, 0, 0);
  v->addWidget(collapsible(tr("How to connect"), help));
  return page;
}

QWidget* ConnectDialog::buildSimulatorPage()
{
  auto* help = howTo(tr(
    "Fake drone on this Mac (no Pi, no ROS). It mimics the safety node, including the "
    "300 ms link-loss disarm, and publishes a fake <code>/link/status</code>."
    "<ol style='margin-left:-20px'>"
    "<li><code>python3 -m pip install websockets</code> (once)</li>"
    "<li>From the repo: <code>python3 gcs/tools/fake_drone/fake_drone.py --port 9090 -v</code></li>"
    "<li>Press <b>Connect</b> (connects to <code>localhost</code>).</li></ol>"));

  auto* page = new QWidget;
  auto* v = new QVBoxLayout(page);
  v->setContentsMargins(0, 0, 0, 0);
  v->addWidget(collapsible(tr("How to run the fake drone"), help));
  return page;
}

QWidget* ConnectDialog::buildMacSection()
{
  m_macBars = new SignalBars;
  m_macSummary = new QLabel;
  m_macSummary->setTextInteractionFlags(Qt::TextSelectableByMouse);
  auto* extra = new QWidget;
  auto* h = new QHBoxLayout(extra);
  h->setContentsMargins(0, 0, 0, 0);
  h->addWidget(m_macBars, 0, Qt::AlignVCenter);
  h->addWidget(m_macSummary, 1);

  auto* box = new MacNetworkBox(m_monitor);
  box->setTitle(QString());
  box->setFlat(true);
  box->setStyleSheet(QStringLiteral("QGroupBox { border: none; margin-top: 0; padding-top: 0; }"));
  return collapsible(tr("This Mac"), box, extra);
}

// ---- state -------------------------------------------------------------------------

Mode ConnectDialog::currentMode() const
{
  const int i = m_modes->currentIndex();
  return (i >= 0 && i < 3) ? kModes[i] : Mode::Tailscale;
}

ConnectionTarget ConnectDialog::currentTarget() const
{
  ConnectionTarget t;
  t.mode = currentMode();
  t.port = static_cast<quint16>(m_port->value());
  t.host = t.mode == Mode::Simulator ? QStringLiteral("localhost") : m_host->text().trimmed();
  return t;
}

void ConnectDialog::onModeChanged()
{
  // One host field, remembered per mode.
  if (m_lastMode >= 0 && kModes[m_lastMode] != Mode::Simulator) {
    m_hostPerMode[m_lastMode] = m_host->text().trimmed();
  }
  const Mode m = currentMode();
  m_lastMode = m_modes->currentIndex();
  const bool sim = (m == Mode::Simulator);
  m_host->setText(sim ? QStringLiteral("localhost") : m_hostPerMode.value(m_lastMode));
  m_host->setEnabled(!sim);
  m_host->setClearButtonEnabled(!sim);
  m_host->setPlaceholderText(placeholder(m));

  // Only the visible page counts for the dialog's size.
  m_pages->setCurrentIndex(m_lastMode);
  for (int i = 0; i < m_pages->count(); ++i) {
    m_pages->widget(i)->setSizePolicy(QSizePolicy::Preferred,
                                      i == m_lastMode ? QSizePolicy::Preferred : QSizePolicy::Ignored);
  }

  refreshTailscale();
  restartProbe();
  fitToContents();
}

void ConnectDialog::onHostEdited()
{
  m_editDebounce.start();
  m_probeKey.clear();
  m_probeState = Probe::Idle;
  updateSummary();
}

void ConnectDialog::updatePingTarget()
{
  // Probe only the drone, and only once Tailscale knows it and it is online.
  QString target;
  if (currentMode() == Mode::Tailscale) {
    const tailscale::Node* n = m_monitor->tailscale().findPeer(m_host->text());
    if (n && n->online) {
      target = m_host->text().trimmed();
    }
  }
  m_monitor->setPingTarget(target);
}

void ConnectDialog::refreshTailscale()
{
  const tailscale::Status& s = m_monitor->tailscale();
  const QString macIp = macTailscaleIp(m_monitor->localAddresses());
  if (!s.ok && !macIp.isEmpty()) {
    m_tsState->setText(tr("<span style='color:#b08800'>Up on this Mac (%1), but the Tailscale "
                          "CLI can't be queried:</span> %2")
                         .arg(macIp, gray(s.error.toHtmlEscaped())));
  } else {
    m_tsState->setText(linktext::tailscaleState(s));
  }

  const QString host = m_host->text().trimmed();
  const tailscale::Node* n = s.running() ? s.findPeer(host) : nullptr;
  if (!s.running() || host.isEmpty() || currentMode() != Mode::Tailscale) {
    m_tsDevice->setText(QStringLiteral("-"));
    m_tsIp->setText(QStringLiteral("-"));
  } else if (!n) {
    m_tsDevice->setText(tr("<span style='color:#b08800'>not in your tailnet</span>"));
    m_tsIp->setText(QStringLiteral("-"));
  } else {
    const QString state = n->online ? tr("<span style='color:#1a7f37'><b>online</b></span>")
                                    : tr("<span style='color:#c0392b'><b>offline</b></span>");
    m_tsDevice->setText(QStringLiteral("%1 · %2 · %3")
                          .arg(n->shortName().toHtmlEscaped(), n->os.toHtmlEscaped(), state));
    m_tsIp->setText(n->ipv4());
  }
  updatePingTarget();
  updateSummary();
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
  updateSummary();
}

void ConnectDialog::refreshMac()
{
  const WifiInfo& w = m_monitor->wifi();
  m_macBars->setRssi(w.rssiDbm);
  QStringList parts;
  if (!w.available || !w.powerOn || w.rssiDbm == 0) {
    parts << tr("Wi-Fi not connected");
  } else {
    parts << QStringLiteral("%1 dBm").arg(w.rssiDbm);
    if (w.bandGHz > 0) {
      parts << QStringLiteral("%1 GHz").arg(w.bandGHz);
    }
  }
  const QString tsIp = macTailscaleIp(m_monitor->localAddresses());
  if (!tsIp.isEmpty()) {
    parts << tr("Tailscale %1").arg(tsIp);
  }
  m_macSummary->setText(gray(parts.join(QStringLiteral("  ·  ")).toHtmlEscaped()));
}

void ConnectDialog::setSummary(Level level, const QString& html)
{
  const char* color = "#8a8a8a";
  switch (level) {
    case Level::Neutral: color = "#8a8a8a"; break;
    case Level::Ok: color = "#2f9e44"; break;
    case Level::Warn: color = "#e0a800"; break;
    case Level::Bad: color = "#d63939"; break;
  }
  m_summary->setText(QStringLiteral("<span style='color:%1; font-size:15pt'>&#9679;</span>&nbsp; %2")
                       .arg(QLatin1String(color), html));
}

void ConnectDialog::updateSummary()
{
  const ConnectionTarget t = currentTarget();
  const QString host = t.effectiveHost();
  const QString hp = QStringLiteral("<b>%1:%2</b>").arg(host.toHtmlEscaped()).arg(t.port);
  const bool open = m_probeState == Probe::Open;
  const bool closed = m_probeState == Probe::Closed;

  setReady(open);

  if (t.mode == Mode::Simulator) {
    if (open) {
      setSummary(Level::Ok, tr("Ready: fake drone answering on %1").arg(hp));
    } else if (closed) {
      setSummary(Level::Warn, tr("No fake drone on %1. Start <code>fake_drone.py</code> "
                                 "(see below).").arg(hp));
    } else {
      setSummary(Level::Neutral, tr("Checking %1 ...").arg(hp));
    }
    return;
  }

  if (host.isEmpty()) {
    setSummary(Level::Neutral, t.mode == Mode::Tailscale
                                 ? tr("Enter the drone's Tailscale name (e.g. mini-acrobat).")
                                 : tr("Enter the drone's hostname or IP."));
    return;
  }

  if (t.mode == Mode::Manual) {
    if (open) {
      setSummary(Level::Ok, tr("Ready: rosbridge answering at %1").arg(hp));
    } else if (closed) {
      setSummary(Level::Warn, tr("No rosbridge at %1. Same network? Bringup running on the "
                                 "drone?").arg(hp));
    } else {
      setSummary(Level::Neutral, tr("Checking %1 ...").arg(hp));
    }
    return;
  }

  // ---- Tailscale ----
  const tailscale::Status& s = m_monitor->tailscale();
  const bool macUp = s.running() || (!s.ok && !macTailscaleIp(m_monitor->localAddresses()).isEmpty());
  const tailscale::Ping& p = m_monitor->ping();
  const QString path = (p.ok && !m_monitor->pingTarget().isEmpty())
    ? QStringLiteral(" · %1 %2 ms")
        .arg(p.relayed() ? tr("relayed") : tr("direct"))
        .arg(p.ms, 0, 'f', p.ms < 10 ? 1 : 0)
    : QString();

  if (open) {
    if (p.ok && p.relayed()) {
      setSummary(Level::Warn, tr("Reachable at %1, but <b>relayed</b>%2: expect link-loss "
                                 "disarms. Put the Mac and drone on the same network.")
                                .arg(hp, path));
    } else {
      setSummary(Level::Ok, tr("Ready: rosbridge answering at %1%2").arg(hp, path));
    }
    return;
  }
  if (!macUp) {
    if (s.ok) {
      setSummary(Level::Bad, tr("Tailscale is <b>%1</b> on this Mac: open the Tailscale app "
                                "and connect.").arg(s.backendState.toHtmlEscaped()));
    } else {
      setSummary(Level::Bad, tr("Tailscale isn't running on this Mac: open the Tailscale app "
                                "and connect."));
    }
    return;
  }
  if (s.running()) {
    const tailscale::Node* n = s.findPeer(host);
    if (!n) {
      setSummary(Level::Warn, tr("'%1' isn't in your tailnet. Set up Tailscale on the drone, "
                                 "or check the name.").arg(host.toHtmlEscaped()));
      return;
    }
    if (!n->online) {
      setSummary(Level::Bad, tr("'%1' is offline: drone off, no internet, or tailscaled not "
                                "running.").arg(host.toHtmlEscaped()));
      return;
    }
    if (closed) {
      setSummary(Level::Warn, tr("Drone online%1, but no rosbridge on :%2. Is "
                                 "<code>bringup.launch.py</code> running?")
                                .arg(path).arg(t.port));
    } else {
      setSummary(Level::Neutral, tr("Drone online%1 · checking rosbridge ...").arg(path));
    }
    return;
  }
  // Tailscale is up but its CLI can't be queried: only the probe tells.
  if (closed) {
    setSummary(Level::Warn, tr("No rosbridge at %1. Drone online? Bringup running?").arg(hp));
  } else {
    setSummary(Level::Neutral, tr("Checking %1 ...").arg(hp));
  }
}

void ConnectDialog::setReady(bool ready)
{
  m_ready = ready && !m_connecting;
  m_connect->setStyleSheet(QLatin1String(m_ready ? kConnectReady : kConnectIdle));
  m_connect->setToolTip(m_ready ? tr("The drone's rosbridge is answering")
                                : tr("rosbridge not confirmed yet: you can still try"));
}

void ConnectDialog::fitToContents()
{
  // Grow/shrink vertically as dropdowns open and close; keep the user's width.
  // The per-mode pages sit in a QStackedWidget, whose layout caches its size:
  // invalidate from the current page up, or closing a dropdown there leaves
  // the dialog at its old (larger) height.
  QTimer::singleShot(0, this, [this]() {
    if (!layout()) {
      return;
    }
    if (QWidget* page = m_pages->currentWidget()) {
      if (page->layout()) {
        page->layout()->invalidate();
        page->layout()->activate();
      }
      page->updateGeometry();
    }
    m_pages->layout()->invalidate();
    m_pages->updateGeometry();
    layout()->invalidate();
    layout()->activate();
    const int h = layout()->hasHeightForWidth() ? layout()->totalHeightForWidth(width())
                                                : sizeHint().height();
    resize(width(), qMax(h, minimumSizeHint().height()));
  });
}

// ---- rosbridge probe -----------------------------------------------------------------

void ConnectDialog::dropProbe()
{
  // A fresh socket per attempt: an aborted host lookup can still report its
  // error later, which must not touch the next attempt.
  if (m_probe) {
    m_probe->disconnect(this);
    m_probe->abort();
    m_probe->deleteLater();
    m_probe = nullptr;
  }
}

void ConnectDialog::probeNow()
{
  if (m_connecting || m_probe) {
    return;  // connecting, or an attempt still running
  }
  const ConnectionTarget t = currentTarget();
  const QString host = t.effectiveHost();
  if (host.isEmpty()) {
    m_probeKey.clear();
    m_probeState = Probe::Idle;
    updateSummary();
    return;
  }
  const QString key = QStringLiteral("%1:%2").arg(host).arg(t.port);
  if (key != m_probeKey) {
    m_probeKey = key;
    m_probeState = Probe::Probing;
    updateSummary();
  }

  // rosbridge probe: a plain TCP connect to host:port, closed right away.
  auto* sock = new QTcpSocket(this);
  sock->setProxy(QNetworkProxy::NoProxy);
  m_probe = sock;
  connect(sock, &QTcpSocket::connected, this, [this, sock]() {
    if (sock == m_probe) {
      dropProbe();
      onProbeResult(true);
    }
  });
  connect(sock, &QTcpSocket::errorOccurred, this, [this, sock](QAbstractSocket::SocketError) {
    if (sock == m_probe) {
      dropProbe();
      onProbeResult(false);
    }
  });
  sock->connectToHost(host, t.port);
  m_probeTimeout.start();
}

void ConnectDialog::restartProbe()
{
  m_probeTimeout.stop();
  dropProbe();
  m_probeKey.clear();
  m_probeState = Probe::Idle;
  probeNow();
}

void ConnectDialog::onProbeResult(bool open)
{
  m_probeTimeout.stop();
  const Probe next = open ? Probe::Open : Probe::Closed;
  if (next != m_probeState) {
    m_probeState = next;
    updateSummary();
  }
}

// ---- connect -------------------------------------------------------------------------

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
  m_probeTimer.stop();
  m_probeTimeout.stop();
  dropProbe();
  setReady(false);
  m_connect->setText(tr("Cancel"));
  m_modes->setEnabled(false);
  m_host->setEnabled(false);
  m_port->setEnabled(false);
  m_refresh->setEnabled(false);
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
  m_host->setEnabled(currentMode() != Mode::Simulator);
  m_port->setEnabled(true);
  m_refresh->setEnabled(true);
  setStatus(tr("Cancelled."));
  m_probeTimer.start();
  restartProbe();
}

void ConnectDialog::reject()
{
  cancelConnect();
  QDialog::reject();
}

void ConnectDialog::setStatus(const QString& html)
{
  m_status->setText(html);
  m_status->setVisible(!html.isEmpty());
  fitToContents();
}

}  // namespace magcs
