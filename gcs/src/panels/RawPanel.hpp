#pragma once
#include <QSet>
#include <QTimer>

#include "panels/Panel.hpp"

class QComboBox;
class QLabel;
class QTreeWidget;
class QTreeWidgetItem;

namespace magcs {

// Last message of one topic as an expandable tree (Foxglove "Raw Messages").
// Config: {topic}.
class RawPanel : public Panel {
  Q_OBJECT
public:
  explicit RawPanel(const PanelContext& ctx, QWidget* parent = nullptr);
  ~RawPanel() override;

  QString type() const override { return QStringLiteral("raw"); }
  QJsonObject saveConfig() const override;
  void loadConfig(const QJsonObject& cfg) override;

  void setTopic(const QString& topic);

private:
  void refresh();
  void fillTopics();
  void fill(QTreeWidgetItem* parent, const QString& key, const QJsonValue& v, const QString& path);

  QComboBox* m_topicBox;
  QLabel* m_info;
  QTreeWidget* m_tree;
  QString m_topic;
  double m_shownT = -2.0;
  QSet<QString> m_expanded;
  QTimer m_timer;
};

}  // namespace magcs
