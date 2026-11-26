#pragma once
#include <QDialog>
#include <QTimer>

#include "core/ConnectionTarget.hpp"

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;
class QStackedWidget;

namespace magcs {

class Drone;
class NetworkMonitor;

// "Connect to Mini-Acrobat" window. Shown when MAgcs starts (Startup) and from
// Connection > Connect to drone... (Change).
//
// Left: connection modes. Right: the mode's page (Tailscale: state of this
// Mac and of the drone's device only, path test; Manual: host; Simulator: fake
// drone) with its "how to
// connect" steps. Bottom: this Mac's Wi-Fi / IPs and the rosbridge port.
// Connect -> Drone::connectTo(); the dialog closes once rosbridge is connected.
class ConnectDialog : public QDialog {
  Q_OBJECT
public:
  enum class Purpose { Startup, Change };

  ConnectDialog(Drone* drone, NetworkMonitor* monitor, Purpose purpose, QWidget* parent = nullptr);

public slots:
  void reject() override;

private:
  QWidget* buildTailscalePage();
  QWidget* buildManualPage();
  QWidget* buildSimulatorPage();
  static QLabel* howTo(const QString& html);

  ConnectionTarget::Mode currentMode() const;
  ConnectionTarget currentTarget() const;
  void onModeChanged();
  void refreshTailscale();
  void refreshPing();
  void updatePingTarget();
  void startConnect();
  void cancelConnect();
  void setStatus(const QString& html);

  Drone* m_drone;
  NetworkMonitor* m_monitor;
  Purpose m_purpose;

  QListWidget* m_modes;
  QStackedWidget* m_pages;

  QLabel* m_tsState;
  QLineEdit* m_tsHost;
  QLabel* m_tsDevice;
  QLabel* m_tsIp;
  QLabel* m_tsPath;
  QLabel* m_tsWarn;
  QPushButton* m_tsTest;

  QLineEdit* m_manualHost;

  QSpinBox* m_port;
  QLabel* m_status;
  QPushButton* m_connect;

  bool m_connecting = false;
  QTimer m_slowTimer;
};

}  // namespace magcs
