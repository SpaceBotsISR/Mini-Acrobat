#pragma once
#include <QMainWindow>

#include "core/Drone.hpp"
#include "core/NetworkMonitor.hpp"

class QDockWidget;

namespace magcs {

class ConnectionWidget;

class MainWindow : public QMainWindow {
  Q_OBJECT
public:
  explicit MainWindow(QWidget* parent = nullptr);

  Drone* drone() { return &m_drone; }
  NetworkMonitor* network() { return &m_network; }
  ConnectionWidget* connection() { return m_connection; }

  // Shows the "Connect to Mini-Acrobat" window. Startup: returns false if the
  // user chose Quit.
  bool runConnectDialog(bool startup);

protected:
  void closeEvent(QCloseEvent* event) override;

private:
  QDockWidget* addDock(const QString& title, const QString& objectName, QWidget* widget,
                       Qt::DockWidgetArea area);
  void updateStatusBar();
  void followTarget();  // Tailscale ping target = the drone, in Tailscale mode

  Drone m_drone;
  NetworkMonitor m_network;
  ConnectionWidget* m_connection = nullptr;
  QByteArray m_defaultState;
};

}  // namespace magcs
