#pragma once
#include <QGroupBox>

class QLabel;
class QPushButton;

namespace magcs {

class NetworkMonitor;
class SignalBars;

// "This Mac" box: Wi-Fi signal and local IPs. Used by the connect dialog and
// the connection dock.
class MacNetworkBox : public QGroupBox {
  Q_OBJECT
public:
  explicit MacNetworkBox(NetworkMonitor* monitor, QWidget* parent = nullptr);

private:
  void refresh();

  NetworkMonitor* m_monitor;
  SignalBars* m_bars;
  QLabel* m_wifi;
  QLabel* m_ips;
  QPushButton* m_askName;
};

}  // namespace magcs
