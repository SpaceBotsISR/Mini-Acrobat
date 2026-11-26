#pragma once
#include <QWidget>

class QLabel;
class QPushButton;

namespace magcs {

class Drone;
class NetworkMonitor;
class SignalBars;

// "Connection" dock: target and rosbridge link, Tailscale path, this Mac's
// Wi-Fi/IPs and the drone's Wi-Fi/IPs (/link/status).
class ConnectionWidget : public QWidget {
  Q_OBJECT
public:
  ConnectionWidget(Drone* drone, NetworkMonitor* monitor, QWidget* parent = nullptr);

  void connectNow();  // to Drone::target()

signals:
  void changeRequested();  // open the connect dialog

private:
  void toggle();
  void refreshLink();
  void refreshStats();
  void refreshTailscale();
  void refreshDrone();

  Drone* m_drone;
  NetworkMonitor* m_monitor;

  QLabel* m_mode;
  QLabel* m_target;
  QPushButton* m_button;
  QPushButton* m_change;
  QLabel* m_status;
  QLabel* m_addresses;
  QLabel* m_stats;
  QLabel* m_heartbeat;

  QWidget* m_tsBox;
  QLabel* m_tsPeer;
  QLabel* m_tsPath;
  QLabel* m_tsWarn;

  SignalBars* m_droneBars;
  QLabel* m_droneWifi;
  QLabel* m_droneIps;
  QLabel* m_droneAge;

  int m_rttMs = -1;
  quint64 m_lastRx = 0;
  quint32 m_lastHb = 0;
  qint64 m_lastStatsMs = 0;
};

}  // namespace magcs
