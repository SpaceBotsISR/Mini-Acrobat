#pragma once
#include "panels/Panel.hpp"

class QDoubleSpinBox;
class QLineEdit;
class QToolButton;
class QWidget;

namespace magcs {

class GaugeView;

// One numeric field as a big number and a bar between min and max.
// Config: {path, min, max}.
class GaugePanel : public Panel {
  Q_OBJECT
public:
  explicit GaugePanel(const PanelContext& ctx, QWidget* parent = nullptr);
  ~GaugePanel() override;

  QString type() const override { return QStringLiteral("gauge"); }
  QJsonObject saveConfig() const override;
  void loadConfig(const QJsonObject& cfg) override;

  void setPath(const QString& path);

protected:
  void dragEnterEvent(QDragEnterEvent* e) override;
  void dropEvent(QDropEvent* e) override;

private:
  void onMessage(const QString& topic, const QJsonObject& msg, double t);

  QLineEdit* m_path;
  QDoubleSpinBox* m_min;
  QDoubleSpinBox* m_max;
  QWidget* m_settings;
  GaugeView* m_view;
  QString m_topic;
  QString m_field;
};

}  // namespace magcs
