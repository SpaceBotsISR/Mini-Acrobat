#pragma once
#include <QDialog>
#include <QHash>
#include <QTimer>

#include "core/ConnectionTarget.hpp"

class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QTabBar;
class QTcpSocket;
class QToolButton;
class QWidget;

namespace magcs {

class Drone;
class NetworkMonitor;
class SignalBars;

// "Connect to Mini-Acrobat" window. Shown when MAgcs starts (Startup) and from
// Connection > Connect to drone... (Change).
//
//   [ Tailscale | Manual | Simulator ]          mode tabs (Tailscale default)
//   Drone [host.................] : [9090] ⟳    one host field, remembered per mode
//   ● one-line readiness summary
//   ▸ Details / ▸ How to connect                 collapsed by default
//   ▸ This Mac  ▂▄▆ -62 dBm · 5 GHz · 100.x      collapsed by default
//   Quit  Open without connecting      [Connect] Connect lights up once rosbridge answers
//
// Readiness comes from a TCP probe of host:port every 2 s (plus, in Tailscale
// mode, the peer's state in `tailscale status`). Connect is always clickable.
// Connect -> Drone::connectTo(); the dialog closes once rosbridge is connected.
class ConnectDialog : public QDialog {
  Q_OBJECT
public:
  enum class Purpose { Startup, Change };

  ConnectDialog(Drone* drone, NetworkMonitor* monitor, Purpose purpose, QWidget* parent = nullptr);

public slots:
  void reject() override;

private:
  enum class Probe { Idle, Probing, Open, Closed };
  enum class Level { Neutral, Ok, Warn, Bad };

  QWidget* buildTailscalePage();
  QWidget* buildManualPage();
  QWidget* buildSimulatorPage();
  QWidget* buildMacSection();
  // Arrow header ("▸ title [extra]") that shows/hides body. Collapsed at start.
  QWidget* collapsible(const QString& title, QWidget* body, QWidget* headerExtra = nullptr);
  static QLabel* howTo(const QString& html);

  ConnectionTarget::Mode currentMode() const;
  ConnectionTarget currentTarget() const;
  void onModeChanged();
  void onHostEdited();
  void refreshTailscale();
  void refreshPing();
  void refreshMac();
  void updatePingTarget();
  void updateSummary();
  void setSummary(Level level, const QString& html);
  void setReady(bool ready);
  void fitToContents();

  void probeNow();
  void dropProbe();
  void restartProbe();
  void onProbeResult(bool open);

  void startConnect();
  void cancelConnect();
  void setStatus(const QString& html);

  Drone* m_drone;
  NetworkMonitor* m_monitor;
  Purpose m_purpose;

  QTabBar* m_modes;
  QLineEdit* m_host;
  QSpinBox* m_port;
  QToolButton* m_refresh;
  QHash<int, QString> m_hostPerMode;  // what was typed in each mode
  int m_lastMode = -1;

  QLabel* m_summary;
  QStackedWidget* m_pages;

  // Tailscale details
  QLabel* m_tsState;
  QLabel* m_tsDevice;
  QLabel* m_tsIp;
  QLabel* m_tsPath;
  QLabel* m_tsWarn;
  QPushButton* m_tsTest;

  // This Mac
  SignalBars* m_macBars;
  QLabel* m_macSummary;

  QLabel* m_status;
  QPushButton* m_connect;
  bool m_ready = false;

  QTcpSocket* m_probe = nullptr;
  QTimer m_probeTimer;     // every 2 s
  QTimer m_probeTimeout;   // per attempt
  QTimer m_editDebounce;
  QString m_probeKey;      // "host:port" the current result belongs to
  Probe m_probeState = Probe::Idle;

  bool m_connecting = false;
  QTimer m_slowTimer;
};

}  // namespace magcs
