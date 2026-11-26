#pragma once
#include <QList>

#include "panels/Panel.hpp"

class QCheckBox;
class QComboBox;
class QCompleter;
class QDoubleSpinBox;
class QLineEdit;
class QStringListModel;
class QToolButton;

namespace magcs {

class TimePlot;

// Plot panel: any number of numeric message paths, from any topics, on one time
// axis. Type a path (with completion) or drop one from the Topics panel.
// Config: {window, autoY, yMin, yMax, series: [{path, color, visible}]}.
class PlotPanel : public Panel {
  Q_OBJECT
public:
  explicit PlotPanel(const PanelContext& ctx, QWidget* parent = nullptr);
  ~PlotPanel() override;

  QString type() const override { return QStringLiteral("plot"); }
  QJsonObject saveConfig() const override;
  void loadConfig(const QJsonObject& cfg) override;

  void addPath(const QString& path, const QColor& color = QColor(), bool visible = true);

protected:
  void dragEnterEvent(QDragEnterEvent* e) override;
  void dropEvent(QDropEvent* e) override;
  bool eventFilter(QObject* o, QEvent* e) override;

private:
  struct Series {
    QString path;
    QString topic;
    QString field;
    int id = 0;
  };

  void onMessage(const QString& topic, const QJsonObject& msg, double t);
  void addFromEdit();
  void removeSeries(int id);
  void updateCompletions();
  void setProbe(const QString& topic);
  void showLegendMenu(int id, const QPoint& pos);
  void syncYControls();

  TimePlot* m_plot;
  QLineEdit* m_path;
  QCompleter* m_completer;
  QStringListModel* m_completions;
  QComboBox* m_window;
  QCheckBox* m_autoY;
  QDoubleSpinBox* m_yMin;
  QDoubleSpinBox* m_yMax;
  QToolButton* m_pause;
  QList<Series> m_series;
  QString m_probe;  // topic subscribed only to complete its fields
};

}  // namespace magcs
