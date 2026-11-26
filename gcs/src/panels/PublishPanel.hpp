#pragma once
#include <QTimer>

#include "panels/Panel.hpp"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;

namespace magcs {

// Publish a hand-written JSON message on a topic, once or repeatedly
// (Foxglove "Publish"). "Template" fills the message from rosapi's layout.
// Topics MAgcs itself drives (heartbeat, motor setpoint, fly command) are
// refused here: use the Safety / Motors / Fly mode panels for those.
// Config: {topic, type, message, rateHz}.
class PublishPanel : public Panel {
  Q_OBJECT
public:
  explicit PublishPanel(const PanelContext& ctx, QWidget* parent = nullptr);

  QString type() const override { return QStringLiteral("publish"); }
  QJsonObject saveConfig() const override;
  void loadConfig(const QJsonObject& cfg) override;

  // Builds a default message from rosapi typedefs (exposed for tests).
  static QJsonObject defaultMessage(const QJsonValue& typedefs, const QString& type);

private:
  bool publishOnce();
  void fillTopics();
  void onTopicChosen();
  void loadTemplate();
  void setStatus(const QString& html);

  QComboBox* m_topic;
  QLineEdit* m_type;
  QPlainTextEdit* m_json;
  QPushButton* m_publish;
  QCheckBox* m_repeat;
  QDoubleSpinBox* m_rate;
  QLabel* m_status;
  QTimer m_timer;
  quint64 m_sent = 0;
};

}  // namespace magcs
