#include "MainWindow.hpp"

#include <QAction>
#include <QCloseEvent>
#include <QComboBox>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>
#include <QStatusBar>
#include <QToolBar>
#include <QTimer>
#include <QToolButton>

#include "panels/FlyPanel.hpp"
#include "panels/GaugePanel.hpp"
#include "panels/PlotPanel.hpp"
#include "panels/PublishPanel.hpp"
#include "panels/RawPanel.hpp"
#include "panels/TopicsPanel.hpp"
#include "widgets/ConnectDialog.hpp"
#include "widgets/ConnectionWidget.hpp"
#include "widgets/ConsoleWidget.hpp"
#include "widgets/MotorsWidget.hpp"
#include "widgets/SafetyWidget.hpp"
#include "widgets/Theme.hpp"

namespace magcs {

namespace {

constexpr int kStateVersion = 2;  // QMainWindow::saveState version of this layout scheme
const QString kDefaultLayout = QStringLiteral("Default");

struct PanelType {
  const char* key;
  const char* title;
  bool single;  // at most one per layout
};
const PanelType kPanelTypes[] = {
  {"plot", "Plot", false},       {"topics", "Topics", true}, {"raw", "Raw messages", false},
  {"gauge", "Gauge", false},     {"publish", "Publish", false}, {"fly", "Fly mode", true},
};

const PanelType* panelType(const QString& key)
{
  for (const PanelType& t : kPanelTypes) {
    if (key == QLatin1String(t.key)) {
      return &t;
    }
  }
  return nullptr;
}

QString settingsKey(const QString& name)
{
  return QStringLiteral("layouts/data/") + QString::fromUtf8(name.toUtf8().toBase64(QByteArray::Base64UrlEncoding));
}

}  // namespace

MainWindow::MainWindow(QWidget* parent)
: QMainWindow(parent), m_hub(m_drone.client())
{
  m_ctx = PanelContext{&m_drone, &m_network, &m_hub};
  setWindowTitle(QStringLiteral("MAgcs"));
  m_drone.setTarget(ConnectionTarget::load());
  resize(1440, 900);
  setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowNestedDocks |
                 QMainWindow::AllowTabbedDocks | QMainWindow::GroupedDragging);
  setTabPosition(Qt::AllDockWidgetAreas, QTabWidget::North);
  // Left column full height; the console spans the rest at the bottom.
  setCorner(Qt::TopLeftCorner, Qt::LeftDockWidgetArea);
  setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);
  // Everything is a dock: the central widget is an empty, hidden placeholder.
  auto* central = new QWidget;
  central->setMaximumSize(0, 0);
  setCentralWidget(central);
  central->hide();

  m_connection = new ConnectionWidget(&m_drone, &m_network);
  connect(m_connection, &ConnectionWidget::changeRequested, this, [this]() {runConnectDialog(false);});
  m_fixed.insert("connection", addFixedDock(tr("Connection"), "dock_connection", m_connection));
  m_fixed.insert("safety", addFixedDock(tr("Safety"), "dock_safety", new SafetyWidget(&m_drone)));
  m_fixed.insert("motors", addFixedDock(tr("Motors"), "dock_motors", new MotorsWidget(&m_drone)));
  m_fixed.insert("console", addFixedDock(tr("Console"), "dock_console", new ConsoleWidget(&m_drone)));

  buildMenus();

  connect(&m_drone, &Drone::targetChanged, this, &MainWindow::followTarget);
  followTarget();
  connect(&m_drone, &Drone::connectionChanged, this, [this]() {updateStatusBar();});
  connect(&m_drone, &Drone::safetyChanged, this, [this]() {updateStatusBar();});
  updateStatusBar();

  // ---- layouts: the last one used, or the default arrangement on first run
  QSettings s;
  restoreGeometry(s.value("window/geometry").toByteArray());
  if (layoutNames().isEmpty()) {
    m_layoutName = kDefaultLayout;
    buildDefaultLayout();
    storeLayout(kDefaultLayout, captureLayout());
  } else {
    QString name = s.value("layouts/current").toString();
    if (!layoutNames().contains(name)) {
      name = layoutNames().contains(kDefaultLayout) ? kDefaultLayout : layoutNames().value(0);
    }
    m_layoutName = name;
    applyLayout(loadLayout(name));
  }
  refreshLayoutCombo();
}

MainWindow::~MainWindow()
{
  // Panels use m_drone / m_hub in their destructors (release subscriptions,
  // fly mode off). Members die before QWidget deletes the children, so delete
  // every dock while they are still alive.
  clearPanels();
  qDeleteAll(m_fixed);
  m_fixed.clear();
}

// ---- docks & panels ---------------------------------------------------------------

QDockWidget* MainWindow::addFixedDock(const QString& title, const QString& objectName, QWidget* widget)
{
  auto* dock = new QDockWidget(title, this);
  dock->setObjectName(objectName);  // needed by saveState()
  dock->setWidget(widget);
  addDockWidget(Qt::RightDockWidgetArea, dock);
  return dock;
}

QDockWidget* MainWindow::createPanel(const QString& type, QString id, const QJsonObject& config)
{
  const PanelType* pt = panelType(type);
  if (!pt) {
    return nullptr;
  }
  if (pt->single) {
    if (QDockWidget* d = findPanel(type)) {
      return d;
    }
  }
  if (id.isEmpty() || m_panels.contains(id)) {
    int n = 1;
    while (m_panels.contains(QStringLiteral("%1-%2").arg(type).arg(n))) {
      ++n;
    }
    id = QStringLiteral("%1-%2").arg(type).arg(n);
  }

  Panel* panel = nullptr;
  if (type == QLatin1String("plot")) {
    auto* p = new PlotPanel(m_ctx);
    connect(p, &Panel::activated, this, [this](Panel* self) {m_activePlot = qobject_cast<PlotPanel*>(self);});
    if (!m_activePlot) {
      m_activePlot = p;
    }
    panel = p;
  } else if (type == QLatin1String("topics")) {
    auto* p = new TopicsPanel(m_ctx);
    connect(p, &TopicsPanel::plotRequested, this, &MainWindow::plotPath);
    connect(p, &TopicsPanel::rawRequested, this, &MainWindow::openRaw);
    panel = p;
  } else if (type == QLatin1String("raw")) {
    panel = new RawPanel(m_ctx);
  } else if (type == QLatin1String("gauge")) {
    panel = new GaugePanel(m_ctx);
  } else if (type == QLatin1String("publish")) {
    panel = new PublishPanel(m_ctx);
  } else if (type == QLatin1String("fly")) {
    panel = new FlyPanel(m_ctx);
  }
  panel->loadConfig(config);

  // "Plot", "Plot 2", ...
  QString title = tr(pt->title);
  if (!pt->single) {
    const QString num = id.section(QLatin1Char('-'), -1);
    if (num != QLatin1String("1")) {
      title += QLatin1Char(' ') + num;
    }
  }
  auto* dock = new QDockWidget(title, this);
  dock->setObjectName(QStringLiteral("panel_") + id);
  dock->setProperty("panelId", id);
  dock->setProperty("panelType", type);
  dock->setWidget(panel);
  dock->setAttribute(Qt::WA_DeleteOnClose);  // closing a panel removes it from the layout
  m_panels.insert(id, dock);
  connect(dock, &QObject::destroyed, this, [this, id]() {m_panels.remove(id);});
  addDockWidget(Qt::RightDockWidgetArea, dock);
  return dock;
}

QDockWidget* MainWindow::findPanel(const QString& type) const
{
  for (QDockWidget* d : m_panels) {
    if (d->property("panelType").toString() == type) {
      return d;
    }
  }
  return nullptr;
}

QDockWidget* MainWindow::addPanelFromMenu(const QString& type)
{
  const PanelType* pt = panelType(type);
  if (pt && pt->single) {
    if (QDockWidget* d = findPanel(type)) {
      d->show();
      d->raise();
      return d;
    }
  }
  QDockWidget* sibling = findPanel(type);  // next to one of the same kind, if any
  QDockWidget* dock = createPanel(type);
  if (!dock) {
    return nullptr;
  }
  if (sibling && sibling != dock && !sibling->isFloating()) {
    splitDockWidget(sibling, dock, type == QLatin1String("plot") ? Qt::Vertical : Qt::Horizontal);
  } else if (QDockWidget* plot = findPanel(QStringLiteral("plot")); plot && plot != dock) {
    splitDockWidget(plot, dock, Qt::Horizontal);
  }
  dock->show();
  dock->raise();
  if (type == QLatin1String("plot")) {
    m_activePlot = qobject_cast<PlotPanel*>(dock->widget());
  }
  return dock;
}

PlotPanel* MainWindow::currentPlot()
{
  if (m_activePlot) {
    return m_activePlot;
  }
  if (QDockWidget* d = findPanel(QStringLiteral("plot"))) {
    m_activePlot = qobject_cast<PlotPanel*>(d->widget());
    return m_activePlot;
  }
  QDockWidget* d = addPanelFromMenu(QStringLiteral("plot"));
  return d ? qobject_cast<PlotPanel*>(d->widget()) : nullptr;
}

void MainWindow::plotPath(const QString& path)
{
  if (PlotPanel* p = currentPlot()) {
    p->addPath(path);
    if (auto* d = qobject_cast<QDockWidget*>(p->parentWidget())) {
      d->raise();
    }
  }
}

void MainWindow::openRaw(const QString& topic)
{
  QDockWidget* d = addPanelFromMenu(QStringLiteral("raw"));
  if (auto* raw = d ? qobject_cast<RawPanel*>(d->widget()) : nullptr) {
    raw->setTopic(topic);
  }
}

// ---- layouts ----------------------------------------------------------------------

QJsonObject MainWindow::captureLayout() const
{
  QJsonArray panels;
  for (auto it = m_panels.cbegin(); it != m_panels.cend(); ++it) {
    const auto* p = qobject_cast<const Panel*>(it.value()->widget());
    if (!p) {
      continue;
    }
    panels.append(QJsonObject{{"id", it.key()}, {"type", p->type()}, {"config", p->saveConfig()}});
  }
  return {{"version", 1},
          {"state", QString::fromLatin1(saveState(kStateVersion).toBase64())},
          {"panels", panels}};
}

void MainWindow::clearPanels()
{
  const auto docks = m_panels.values();
  m_panels.clear();
  for (QDockWidget* d : docks) {
    removeDockWidget(d);
    delete d;
  }
  m_activePlot = nullptr;
}

void MainWindow::applyLayout(const QJsonObject& layout)
{
  clearPanels();
  for (const QJsonValue& v : layout.value("panels").toArray()) {
    const QJsonObject o = v.toObject();
    createPanel(o.value("type").toString(), o.value("id").toString(), o.value("config").toObject());
  }
  const QByteArray state = QByteArray::fromBase64(layout.value("state").toString().toLatin1());
  if (!restoreState(state, kStateVersion)) {
    // Unknown / old layout: fall back to the default arrangement of what exists.
    for (QDockWidget* d : m_fixed) {
      d->show();
    }
  }
  // Fixed sections must never be lost: a hidden one can come back from View.
}

void MainWindow::buildDefaultLayout()
{
  clearPanels();
  QDockWidget* conn = m_fixed.value("connection");
  QDockWidget* safety = m_fixed.value("safety");
  QDockWidget* motors = m_fixed.value("motors");
  QDockWidget* console = m_fixed.value("console");
  for (QDockWidget* d : m_fixed) {
    d->setFloating(false);
    d->show();
  }

  QDockWidget* topics = createPanel(QStringLiteral("topics"));
  QJsonArray series;
  for (int i = 0; i < 3; ++i) {
    series.append(QJsonObject{{"path", QStringLiteral("/motor_cmd.throttle[%1]").arg(i)},
                              {"color", theme::seriesColor(i).name()}});
  }
  QDockWidget* plot = createPanel(QStringLiteral("plot"), QString(),
                                  QJsonObject{{"window", 30.0}, {"autoY", false}, {"yMin", -1.0},
                                              {"yMax", 1.0}, {"series", series}});
  QDockWidget* fly = createPanel(QStringLiteral("fly"));

  // Left: Connection | Topics (tabs), Safety below.
  addDockWidget(Qt::LeftDockWidgetArea, conn);
  addDockWidget(Qt::LeftDockWidgetArea, safety);
  splitDockWidget(conn, safety, Qt::Vertical);
  tabifyDockWidget(conn, topics);
  conn->raise();
  // Centre: plot. Right: Motors over Fly mode. Bottom: console.
  addDockWidget(Qt::RightDockWidgetArea, plot);
  splitDockWidget(plot, motors, Qt::Horizontal);
  splitDockWidget(motors, fly, Qt::Vertical);
  addDockWidget(Qt::BottomDockWidgetArea, console);

  m_activePlot = qobject_cast<PlotPanel*>(plot->widget());
  // Sizes only stick once the window has its real geometry: apply them after
  // the event loop has laid everything out (and again right now, harmless).
  auto sizes = [this, safety = QPointer<QDockWidget>(safety), plot = QPointer<QDockWidget>(plot),
                motors = QPointer<QDockWidget>(motors), fly = QPointer<QDockWidget>(fly),
                console = QPointer<QDockWidget>(console)]() {
    if (!safety || !plot || !motors || !fly || !console) {
      return;  // layout switched meanwhile
    }
    const int w = width();
    // (conn is tabbed with Topics: size the column through Safety instead)
    resizeDocks({safety, plot, motors}, {340, qMax(500, w - 740), 360}, Qt::Horizontal);
    resizeDocks({safety}, {300}, Qt::Vertical);
    resizeDocks({motors, fly}, {230, 470}, Qt::Vertical);
    resizeDocks({console}, {150}, Qt::Vertical);
  };
  sizes();
  QTimer::singleShot(0, this, [this, sizes]() {
    sizes();
    if (!m_layoutName.isEmpty()) {
      storeLayout(m_layoutName, captureLayout());
    }
  });
}

QStringList MainWindow::layoutNames() const
{
  QSettings s;
  const QStringList names = s.value("layouts/names").toStringList();
  return names;
}

QJsonObject MainWindow::loadLayout(const QString& name) const
{
  QSettings s;
  return QJsonDocument::fromJson(s.value(settingsKey(name)).toByteArray()).object();
}

void MainWindow::storeLayout(const QString& name, const QJsonObject& layout)
{
  QSettings s;
  s.setValue(settingsKey(name), QJsonDocument(layout).toJson(QJsonDocument::Compact));
  QStringList names = s.value("layouts/names").toStringList();
  if (!names.contains(name)) {
    names << name;
    s.setValue("layouts/names", names);
  }
}

void MainWindow::refreshLayoutCombo()
{
  if (!m_layoutBox) {
    return;
  }
  QSignalBlocker b(m_layoutBox);
  m_layoutBox->clear();
  m_layoutBox->addItems(layoutNames());
  m_layoutBox->setCurrentText(m_layoutName);
  QSettings().setValue("layouts/current", m_layoutName);
}

void MainWindow::saveCurrentLayout()
{
  if (!m_layoutName.isEmpty()) {
    storeLayout(m_layoutName, captureLayout());
    statusBar()->showMessage(tr("Layout '%1' saved").arg(m_layoutName), 2500);
  }
}

void MainWindow::switchLayout(const QString& name)
{
  if (name == m_layoutName || !layoutNames().contains(name)) {
    return;
  }
  saveCurrentLayout();
  m_layoutName = name;
  applyLayout(loadLayout(name));
  refreshLayoutCombo();
}

void MainWindow::saveLayoutAs()
{
  bool ok = false;
  const QString name = QInputDialog::getText(this, tr("Save layout as"),
                                             tr("Name (e.g. the test run):"), QLineEdit::Normal,
                                             m_layoutName + tr(" copy"), &ok).trimmed();
  if (!ok || name.isEmpty()) {
    return;
  }
  if (layoutNames().contains(name) &&
      QMessageBox::question(this, tr("Replace layout"), tr("Replace the layout '%1'?").arg(name)) !=
        QMessageBox::Yes) {
    return;
  }
  m_layoutName = name;
  storeLayout(name, captureLayout());
  refreshLayoutCombo();
}

void MainWindow::renameLayout()
{
  bool ok = false;
  const QString name = QInputDialog::getText(this, tr("Rename layout"), tr("New name:"),
                                             QLineEdit::Normal, m_layoutName, &ok).trimmed();
  if (!ok || name.isEmpty() || name == m_layoutName) {
    return;
  }
  if (layoutNames().contains(name)) {
    QMessageBox::warning(this, tr("Rename layout"), tr("A layout called '%1' already exists.").arg(name));
    return;
  }
  QSettings s;
  QStringList names = layoutNames();
  s.remove(settingsKey(m_layoutName));
  names.replace(names.indexOf(m_layoutName), name);
  s.setValue("layouts/names", names);
  m_layoutName = name;
  storeLayout(name, captureLayout());
  refreshLayoutCombo();
}

void MainWindow::deleteLayout()
{
  const QStringList names = layoutNames();
  if (names.size() <= 1) {
    QMessageBox::information(this, tr("Delete layout"), tr("This is the only layout."));
    return;
  }
  if (QMessageBox::question(this, tr("Delete layout"), tr("Delete the layout '%1'?").arg(m_layoutName)) !=
      QMessageBox::Yes) {
    return;
  }
  QSettings s;
  QStringList left = names;
  left.removeAll(m_layoutName);
  s.remove(settingsKey(m_layoutName));
  s.setValue("layouts/names", left);
  m_layoutName = left.first();
  applyLayout(loadLayout(m_layoutName));
  refreshLayoutCombo();
}

void MainWindow::exportLayout()
{
  const QString path = QFileDialog::getSaveFileName(this, tr("Export layout"),
                                                    m_layoutName + QStringLiteral(".magcs-layout.json"),
                                                    tr("MAgcs layout (*.json)"));
  if (path.isEmpty()) {
    return;
  }
  QJsonObject o = captureLayout();
  o.insert("name", m_layoutName);
  QFile f(path);
  if (!f.open(QIODevice::WriteOnly) || f.write(QJsonDocument(o).toJson()) < 0) {
    QMessageBox::warning(this, tr("Export layout"), tr("Could not write %1").arg(path));
  }
}

void MainWindow::importLayout()
{
  const QString path = QFileDialog::getOpenFileName(this, tr("Import layout"), QString(),
                                                    tr("MAgcs layout (*.json)"));
  if (path.isEmpty()) {
    return;
  }
  QFile f(path);
  const QJsonObject o = f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.readAll()).object()
                                                    : QJsonObject();
  if (!o.contains("panels") || !o.contains("state")) {
    QMessageBox::warning(this, tr("Import layout"), tr("%1 is not a MAgcs layout.").arg(path));
    return;
  }
  QString name = o.value("name").toString(QFileInfo(path).baseName());
  for (int n = 2; layoutNames().contains(name); ++n) {
    name = o.value("name").toString(QFileInfo(path).baseName()) + QStringLiteral(" (%1)").arg(n);
  }
  saveCurrentLayout();
  storeLayout(name, o);
  m_layoutName = name;
  applyLayout(o);
  refreshLayoutCombo();
}

// ---- menus & toolbar ----------------------------------------------------------------

void MainWindow::buildMenus()
{
  // Toolbar: layout switcher + add panel
  auto* bar = addToolBar(tr("Layout"));
  bar->setObjectName("toolbar_layout");
  bar->setMovable(false);
  bar->addWidget(new QLabel(tr("  Layout ")));
  m_layoutBox = new QComboBox;
  m_layoutBox->setMinimumContentsLength(14);
  m_layoutBox->setToolTip(tr("Saved layouts (arrangement + panel settings)"));
  bar->addWidget(m_layoutBox);
  connect(m_layoutBox, &QComboBox::activated, this, [this]() {switchLayout(m_layoutBox->currentText());});

  auto* layoutMenu = new QMenu(tr("&Layout"), this);
  auto* save = layoutMenu->addAction(tr("Save"), this, &MainWindow::saveCurrentLayout);
  save->setShortcut(QKeySequence::Save);
  layoutMenu->addAction(tr("Save as new..."), this, &MainWindow::saveLayoutAs);
  layoutMenu->addAction(tr("Rename..."), this, &MainWindow::renameLayout);
  layoutMenu->addAction(tr("Delete"), this, &MainWindow::deleteLayout);
  layoutMenu->addSeparator();
  layoutMenu->addAction(tr("Reset to the default arrangement"), this, [this]() {
    buildDefaultLayout();
    saveCurrentLayout();
  });
  layoutMenu->addSeparator();
  layoutMenu->addAction(tr("Export..."), this, &MainWindow::exportLayout);
  layoutMenu->addAction(tr("Import..."), this, &MainWindow::importLayout);

  auto* layoutBtn = new QToolButton;
  layoutBtn->setText(tr("Layout ▾"));
  layoutBtn->setPopupMode(QToolButton::InstantPopup);
  layoutBtn->setMenu(layoutMenu);
  bar->addWidget(layoutBtn);

  auto* panelsMenu = new QMenu(tr("&Panels"), this);
  for (const PanelType& t : kPanelTypes) {
    panelsMenu->addAction(tr("Add %1").arg(tr(t.title)), this,
                          [this, key = QString::fromLatin1(t.key)]() {addPanelFromMenu(key);});
  }
  auto* addBtn = new QToolButton;
  addBtn->setText(tr("+ Panel"));
  addBtn->setPopupMode(QToolButton::InstantPopup);
  addBtn->setMenu(panelsMenu);
  bar->addSeparator();
  bar->addWidget(addBtn);

  // Menu bar
  auto* conn = menuBar()->addMenu(tr("&Connection"));
  auto* connectAct = conn->addAction(tr("Connect to drone..."), this, [this]() {runConnectDialog(false);});
  connectAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_K));
  auto* disconnectAct = conn->addAction(tr("Disconnect"), &m_drone, &Drone::disconnectFromHost);
  connect(&m_drone, &Drone::connectionChanged, disconnectAct, &QAction::setEnabled);
  disconnectAct->setEnabled(false);

  menuBar()->addMenu(layoutMenu);
  menuBar()->addMenu(panelsMenu);

  auto* view = menuBar()->addMenu(tr("&View"));
  for (const char* key : {"connection", "safety", "motors", "console"}) {
    view->addAction(m_fixed.value(QString::fromLatin1(key))->toggleViewAction());
  }

  auto* safety = menuBar()->addMenu(tr("&Safety"));
  auto* estop = safety->addAction(tr("DISARM now"));
  estop->setShortcut(QKeySequence(Qt::Key_Space));
  estop->setShortcutContext(Qt::ApplicationShortcut);
  connect(estop, &QAction::triggered, &m_drone, &Drone::disarm);
  addAction(estop);  // works even with the menu bar hidden / native

  auto* armAct = safety->addAction(tr("ARM"));
  armAct->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_Space));
  armAct->setShortcutContext(Qt::ApplicationShortcut);
  connect(armAct, &QAction::triggered, this, [this]() {
    const SafetyState& s = m_drone.safety();
    if (m_drone.isConnected() && s.valid && !s.armed()) {
      m_drone.arm();
    }
  });
  addAction(armAct);

  safety->addSeparator();
  auto* test = safety->addAction(tr("Simulate link loss (pause heartbeat)"));
  test->setCheckable(true);
  connect(test, &QAction::toggled, this, [this](bool on) {
    m_drone.heartbeat()->setPaused(on);
    m_drone.log(on ? tr("heartbeat PAUSED (simulated link loss)") : tr("heartbeat resumed"));
  });
}

// ---- misc ---------------------------------------------------------------------------

bool MainWindow::runConnectDialog(bool startup)
{
  ConnectDialog dlg(&m_drone, &m_network,
                    startup ? ConnectDialog::Purpose::Startup : ConnectDialog::Purpose::Change,
                    startup ? nullptr : this);
  const int result = dlg.exec();
  followTarget();  // the dialog probes whatever is selected; go back to the real target
  return result == QDialog::Accepted || !startup;
}

void MainWindow::followTarget()
{
  const ConnectionTarget& t = m_drone.target();
  m_network.setPingTarget(t.mode == ConnectionTarget::Mode::Tailscale ? t.host : QString());
}

void MainWindow::updateStatusBar()
{
  if (!m_drone.isConnected()) {
    statusBar()->showMessage(tr("Not connected"));
    return;
  }
  const SafetyState& s = m_drone.safety();
  statusBar()->showMessage(tr("Connected to %1  |  %2  (%3)")
                             .arg(m_drone.target().url(), s.stateText(), s.reasonText()));
}

void MainWindow::closeEvent(QCloseEvent* event)
{
  QSettings s;
  s.setValue("window/geometry", saveGeometry());
  saveCurrentLayout();
  // Best effort: tell the drone to disarm. Closing also stops the heartbeat, so
  // the safety node disarms by itself within heartbeat_timeout_ms anyway.
  if (m_drone.isConnected()) {
    m_drone.disarm();
    m_drone.client()->close();
  }
  QMainWindow::closeEvent(event);
}

}  // namespace magcs
