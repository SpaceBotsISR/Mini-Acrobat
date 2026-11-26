#pragma once
#include <QHash>
#include <QJsonObject>
#include <QMainWindow>
#include <QPointer>

#include "core/Drone.hpp"
#include "core/NetworkMonitor.hpp"
#include "core/TopicHub.hpp"
#include "panels/Panel.hpp"

class QComboBox;
class QDockWidget;

namespace magcs {

class ConnectionWidget;
class PlotPanel;

// Main window: every section is a dock that can be moved, tabbed, split,
// floated or closed. Fixed sections (Connection, Safety, Motors, Console) always
// exist (View menu shows/hides them); panels (Plot, Topics, Raw messages, Gauge,
// Publish, Fly mode) are added from the Panels menu, as many as needed.
//
// Layouts: the arrangement plus every panel's settings, saved by name (e.g. one
// per test run) and switched from the toolbar. The current layout is saved
// automatically when switching and on quit.
class MainWindow : public QMainWindow {
  Q_OBJECT
public:
  explicit MainWindow(QWidget* parent = nullptr);
  ~MainWindow() override;

  Drone* drone() { return &m_drone; }
  NetworkMonitor* network() { return &m_network; }
  ConnectionWidget* connection() { return m_connection; }

  // Shows the "Connect to Mini-Acrobat" window. Startup: returns false if the
  // user chose Quit.
  bool runConnectDialog(bool startup);

protected:
  void closeEvent(QCloseEvent* event) override;

private:
  QDockWidget* addFixedDock(const QString& title, const QString& objectName, QWidget* widget);
  QDockWidget* createPanel(const QString& type, QString id = QString(),
                           const QJsonObject& config = QJsonObject());
  QDockWidget* addPanelFromMenu(const QString& type);
  QDockWidget* findPanel(const QString& type) const;
  PlotPanel* currentPlot();
  void plotPath(const QString& path);
  void openRaw(const QString& topic);

  // ---- layouts
  QJsonObject captureLayout() const;
  void applyLayout(const QJsonObject& layout);
  void buildDefaultLayout();
  void clearPanels();
  QStringList layoutNames() const;
  QJsonObject loadLayout(const QString& name) const;
  void storeLayout(const QString& name, const QJsonObject& layout);
  void switchLayout(const QString& name);
  void saveCurrentLayout();
  void saveLayoutAs();
  void renameLayout();
  void deleteLayout();
  void exportLayout();
  void importLayout();
  void refreshLayoutCombo();

  void buildMenus();
  void updateStatusBar();
  void followTarget();  // Tailscale ping target = the drone, in Tailscale mode

  Drone m_drone;
  NetworkMonitor m_network;
  TopicHub m_hub;
  PanelContext m_ctx;
  ConnectionWidget* m_connection = nullptr;
  QHash<QString, QDockWidget*> m_fixed;   // "connection", "safety", "motors", "console"
  QHash<QString, QDockWidget*> m_panels;  // panel id -> dock
  QPointer<PlotPanel> m_activePlot;
  QComboBox* m_layoutBox = nullptr;
  QString m_layoutName;
};

}  // namespace magcs
