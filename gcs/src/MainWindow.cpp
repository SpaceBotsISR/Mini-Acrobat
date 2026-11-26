#include "MainWindow.hpp"

#include <QAction>
#include <QCloseEvent>
#include <QDockWidget>
#include <QMenuBar>
#include <QSettings>
#include <QStatusBar>

#include "widgets/ConnectDialog.hpp"
#include "widgets/ConnectionWidget.hpp"
#include "widgets/ConsoleWidget.hpp"
#include "widgets/MotorsWidget.hpp"
#include "widgets/PlotWidget.hpp"
#include "widgets/SafetyWidget.hpp"

namespace magcs {

MainWindow::MainWindow(QWidget* parent)
: QMainWindow(parent)
{
  setWindowTitle(QStringLiteral("MAgcs"));
  m_drone.setTarget(ConnectionTarget::load());
  resize(1280, 800);
  setDockNestingEnabled(true);

  setCentralWidget(new PlotWidget(&m_drone));

  m_connection = new ConnectionWidget(&m_drone, &m_network);
  connect(m_connection, &ConnectionWidget::changeRequested, this, [this]() {runConnectDialog(false);});
  // Left: safety. Right: motors on top of the connection/network panel.
  auto* safetyDock = addDock(tr("Safety"), "dock_safety", new SafetyWidget(&m_drone), Qt::LeftDockWidgetArea);
  auto* motorsDock = addDock(tr("Motors"), "dock_motors", new MotorsWidget(&m_drone), Qt::RightDockWidgetArea);
  auto* connDock = addDock(tr("Connection"), "dock_connection", m_connection, Qt::RightDockWidgetArea);
  splitDockWidget(motorsDock, connDock, Qt::Vertical);
  auto* consoleDock = addDock(tr("Console"), "dock_console", new ConsoleWidget(&m_drone), Qt::BottomDockWidgetArea);
  resizeDocks({safetyDock, connDock}, {300, 360}, Qt::Horizontal);
  resizeDocks({motorsDock, connDock}, {230, 520}, Qt::Vertical);
  resizeDocks({consoleDock}, {130}, Qt::Vertical);

  // Menus
  auto* conn = menuBar()->addMenu(tr("&Connection"));
  auto* connectAct = conn->addAction(tr("Connect to drone..."), this, [this]() {runConnectDialog(false);});
  connectAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_K));
  auto* disconnectAct = conn->addAction(tr("Disconnect"), &m_drone, &Drone::disconnectFromHost);
  connect(&m_drone, &Drone::connectionChanged, disconnectAct, &QAction::setEnabled);
  disconnectAct->setEnabled(false);

  auto* view = menuBar()->addMenu(tr("&View"));
  for (QDockWidget* d : findChildren<QDockWidget*>()) {
    view->addAction(d->toggleViewAction());
  }
  m_defaultState = saveState();
  view->addSeparator();
  view->addAction(tr("Reset layout"), this, [this]() {restoreState(m_defaultState);});

  auto* safety = menuBar()->addMenu(tr("&Safety"));
  auto* estop = safety->addAction(tr("DISARM now"));
  estop->setShortcut(QKeySequence(Qt::Key_Space));
  estop->setShortcutContext(Qt::ApplicationShortcut);
  connect(estop, &QAction::triggered, &m_drone, &Drone::disarm);

  auto* test = safety->addAction(tr("Simulate link loss (pause heartbeat)"));
  test->setCheckable(true);
  connect(test, &QAction::toggled, this, [this](bool on) {
    m_drone.heartbeat()->setPaused(on);
    m_drone.log(on ? tr("heartbeat PAUSED (simulated link loss)") : tr("heartbeat resumed"));
  });

  connect(&m_drone, &Drone::targetChanged, this, &MainWindow::followTarget);
  followTarget();
  connect(&m_drone, &Drone::connectionChanged, this, [this]() {updateStatusBar();});
  connect(&m_drone, &Drone::safetyChanged, this, [this]() {updateStatusBar();});
  updateStatusBar();

  QSettings s;
  restoreGeometry(s.value("window/geometry").toByteArray());
  restoreState(s.value("window/state").toByteArray());
}

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

QDockWidget* MainWindow::addDock(const QString& title, const QString& objectName, QWidget* widget,
                                 Qt::DockWidgetArea area)
{
  auto* dock = new QDockWidget(title, this);
  dock->setObjectName(objectName);  // needed by saveState()
  dock->setWidget(widget);
  addDockWidget(area, dock);
  return dock;
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
  s.setValue("window/state", saveState());
  // Best effort: tell the drone to disarm. Closing also stops the heartbeat, so
  // the safety node disarms by itself within heartbeat_timeout_ms anyway.
  if (m_drone.isConnected()) {
    m_drone.disarm();
    m_drone.client()->close();
  }
  QMainWindow::closeEvent(event);
}

}  // namespace magcs
