#include "panels/RawPanel.hpp"

#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QScrollBar>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "core/MessagePath.hpp"
#include "core/TopicHub.hpp"
#include "widgets/Theme.hpp"

namespace magcs {

namespace {
constexpr int kMaxArrayRows = 50;
constexpr int kPathRole = Qt::UserRole;
}

RawPanel::RawPanel(const PanelContext& ctx, QWidget* parent)
: Panel(ctx, parent)
{
  m_topicBox = new QComboBox;
  m_topicBox->setEditable(true);
  m_topicBox->setInsertPolicy(QComboBox::NoInsert);
  m_topicBox->lineEdit()->setPlaceholderText(tr("/topic"));
  m_topicBox->setMinimumContentsLength(18);
  m_info = new QLabel;
  m_info->setStyleSheet(QStringLiteral("color: %1;").arg(theme::css(theme::kMuted)));

  m_tree = new QTreeWidget;
  m_tree->setColumnCount(2);
  m_tree->setHeaderLabels({tr("Field"), tr("Value")});
  m_tree->setAlternatingRowColors(true);
  m_tree->setUniformRowHeights(true);
  m_tree->header()->setSectionResizeMode(0, QHeaderView::Interactive);
  m_tree->header()->resizeSection(0, 220);
  m_tree->setDragEnabled(false);

  auto* bar = new QHBoxLayout;
  bar->setContentsMargins(6, 6, 6, 0);
  bar->addWidget(m_topicBox, 1);
  bar->addWidget(m_info);
  auto* v = new QVBoxLayout(this);
  v->setContentsMargins(0, 0, 0, 0);
  v->setSpacing(4);
  v->addLayout(bar);
  v->addWidget(m_tree, 1);

  connect(m_topicBox, &QComboBox::activated, this, [this]() {setTopic(m_topicBox->currentText());});
  connect(m_topicBox->lineEdit(), &QLineEdit::returnPressed, this,
          [this]() {setTopic(m_topicBox->currentText()); m_tree->setFocus();});
  connect(m_ctx.hub, &TopicHub::topicsChanged, this, &RawPanel::fillTopics);
  connect(m_tree, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem* it) {
    m_expanded.insert(it->data(0, kPathRole).toString());
  });
  connect(m_tree, &QTreeWidget::itemCollapsed, this, [this](QTreeWidgetItem* it) {
    m_expanded.remove(it->data(0, kPathRole).toString());
  });
  m_timer.setInterval(200);  // 5 Hz is plenty to read
  connect(&m_timer, &QTimer::timeout, this, &RawPanel::refresh);
  m_timer.start();
  fillTopics();
}

RawPanel::~RawPanel()
{
  if (!m_topic.isEmpty()) {
    m_ctx.hub->release(m_topic);
  }
}

void RawPanel::fillTopics()
{
  const QString cur = m_topicBox->currentText();
  QSignalBlocker b(m_topicBox);
  m_topicBox->clear();
  for (const TopicHub::Topic& t : m_ctx.hub->topics()) {
    m_topicBox->addItem(t.name);
  }
  m_topicBox->setEditText(m_topic.isEmpty() ? cur : m_topic);
}

void RawPanel::setTopic(const QString& topic)
{
  const QString t = topic.trimmed();
  if (t == m_topic) {
    return;
  }
  if (!m_topic.isEmpty()) {
    m_ctx.hub->release(m_topic);
  }
  m_topic = t;
  m_tree->clear();
  m_shownT = -2.0;
  m_expanded.clear();
  if (TopicHub::isHeavyType(m_ctx.hub->typeOf(m_topic))) {
    m_info->setText(tr("large message type: not decoded"));
    m_topic.clear();
    return;
  }
  if (!m_topic.isEmpty()) {
    m_ctx.hub->acquire(m_topic);
  }
  m_topicBox->setEditText(m_topic);
  refresh();
}

void RawPanel::refresh()
{
  if (m_topic.isEmpty()) {
    m_info->setText(QString());
    return;
  }
  const double t = m_ctx.hub->lastTime(m_topic);
  const QString type = m_ctx.hub->typeOf(m_topic);
  if (t < 0) {
    m_info->setText(tr("waiting..."));
    return;
  }
  const double age = m_ctx.hub->now() - t;
  m_info->setText(QStringLiteral("%1 · %2 Hz · %3 s ago")
                    .arg(type.section(QLatin1Char('/'), -1))
                    .arg(m_ctx.hub->rateHz(m_topic), 0, 'f', 1)
                    .arg(age, 0, 'f', 1));
  if (t == m_shownT || !isVisible()) {
    return;
  }
  m_shownT = t;
  // Rebuild, keeping what was expanded (by path) and the scroll position.
  const int scroll = m_tree->verticalScrollBar() ? m_tree->verticalScrollBar()->value() : 0;
  m_tree->setUpdatesEnabled(false);
  m_tree->clear();
  const QJsonObject msg = m_ctx.hub->lastMessage(m_topic);
  for (auto it = msg.constBegin(); it != msg.constEnd(); ++it) {
    fill(m_tree->invisibleRootItem(), it.key(), it.value(), it.key());
  }
  m_tree->verticalScrollBar()->setValue(scroll);
  m_tree->setUpdatesEnabled(true);
}

void RawPanel::fill(QTreeWidgetItem* parent, const QString& key, const QJsonValue& v, const QString& path)
{
  auto* it = new QTreeWidgetItem(parent);
  it->setText(0, key);
  it->setData(0, kPathRole, path);
  it->setText(1, msgpath::shortText(v, 200));
  if (v.isDouble() || v.isBool()) {
    it->setToolTip(0, m_topic + QLatin1Char('.') + path);
  } else if (v.isString()) {
    it->setForeground(1, QColor(0xf2, 0xd3, 0x4b));
  }
  if (v.isObject()) {
    const QJsonObject o = v.toObject();
    for (auto c = o.constBegin(); c != o.constEnd(); ++c) {
      fill(it, c.key(), c.value(), path + QLatin1Char('.') + c.key());
    }
  } else if (v.isArray()) {
    const QJsonArray a = v.toArray();
    const int n = qMin(a.size(), kMaxArrayRows);
    for (int i = 0; i < n; ++i) {
      fill(it, QStringLiteral("[%1]").arg(i), a.at(i), QStringLiteral("%1[%2]").arg(path).arg(i));
    }
    if (a.size() > n) {
      auto* more = new QTreeWidgetItem(it);
      more->setText(0, tr("... %1 more").arg(a.size() - n));
      more->setForeground(0, theme::kMuted);
    }
  }
  if (m_expanded.contains(path)) {
    it->setExpanded(true);
  }
}

QJsonObject RawPanel::saveConfig() const
{
  return {{"topic", m_topic}};
}

void RawPanel::loadConfig(const QJsonObject& cfg)
{
  setTopic(cfg.value("topic").toString());
}

}  // namespace magcs
