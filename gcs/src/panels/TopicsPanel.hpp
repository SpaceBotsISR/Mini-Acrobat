#pragma once
#include <QHash>
#include <QSet>
#include <QTimer>

#include "panels/Panel.hpp"

class QLabel;
class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;

namespace magcs {

// Every topic on the drone (rosapi) with its type and live rate. Expand a topic
// to subscribe and see its numeric fields live; double-click a field to plot it
// in the current plot, or drag it onto any Plot / Gauge panel. Double-click a
// topic to open it in a Raw messages panel.
class TopicsPanel : public Panel {
  Q_OBJECT
public:
  explicit TopicsPanel(const PanelContext& ctx, QWidget* parent = nullptr);
  ~TopicsPanel() override;

  QString type() const override { return QStringLiteral("topics"); }

signals:
  void plotRequested(const QString& path);
  void rawRequested(const QString& topic);

private:
  void rebuild();
  void refreshValues();
  void refreshStatus();
  void applyFilter();
  void fillFields(QTreeWidgetItem* topicItem);

  QLineEdit* m_filter;
  QLabel* m_status;
  QTreeWidget* m_tree;
  QHash<QString, QTreeWidgetItem*> m_items;  // topic -> item
  QSet<QString> m_open;                      // expanded (subscribed) topics
  QTimer m_timer;
};

}  // namespace magcs
