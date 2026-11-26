#include "panels/TopicsPanel.hpp"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QStyle>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "core/Drone.hpp"
#include "core/MessagePath.hpp"
#include "core/TopicHub.hpp"
#include "widgets/Theme.hpp"

namespace magcs {

namespace {

constexpr int kPathRole = Qt::UserRole;        // field items: "/topic.field"
constexpr int kTopicRole = Qt::UserRole + 1;   // topic items: "/topic"

// Field rows can be dragged onto Plot / Gauge panels.
class PathTree : public QTreeWidget {
public:
  using QTreeWidget::QTreeWidget;

protected:
  QStringList mimeTypes() const override { return {kPathMime, QStringLiteral("text/plain")}; }
  QMimeData* mimeData(const QList<QTreeWidgetItem*>& items) const override
  {
    for (QTreeWidgetItem* it : items) {
      const QString path = it->data(0, kPathRole).toString();
      if (!path.isEmpty()) {
        auto* m = new QMimeData;
        m->setData(kPathMime, path.toUtf8());
        m->setText(path);
        return m;
      }
    }
    return nullptr;
  }
};

QString hzText(double hz)
{
  if (hz <= 0.0) {
    return QString();
  }
  return hz < 10 ? QString::number(hz, 'f', 1) : QString::number(hz, 'f', 0);
}

}  // namespace

TopicsPanel::TopicsPanel(const PanelContext& ctx, QWidget* parent)
: Panel(ctx, parent)
{
  m_filter = new QLineEdit;
  m_filter->setPlaceholderText(tr("Filter topics"));
  m_filter->setClearButtonEnabled(true);
  auto* refresh = new QToolButton;
  refresh->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
  refresh->setToolTip(tr("Refresh the topic list"));
  m_status = new QLabel;
  m_status->setWordWrap(true);
  m_status->setStyleSheet(QStringLiteral("color: %1;").arg(theme::css(theme::kMuted)));

  m_tree = new PathTree;
  m_tree->setColumnCount(3);
  m_tree->setHeaderLabels({tr("Topic / field"), tr("Type / value"), tr("Hz")});
  m_tree->setDragEnabled(true);
  m_tree->setDragDropMode(QAbstractItemView::DragOnly);
  m_tree->setUniformRowHeights(true);
  m_tree->setAlternatingRowColors(true);
  m_tree->setRootIsDecorated(true);
  m_tree->header()->setStretchLastSection(false);
  m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
  m_tree->header()->setSectionResizeMode(1, QHeaderView::Interactive);
  m_tree->header()->setSectionResizeMode(2, QHeaderView::Fixed);
  m_tree->header()->resizeSection(1, 190);
  m_tree->header()->resizeSection(2, 48);

  auto* bar = new QHBoxLayout;
  bar->setContentsMargins(6, 6, 6, 0);
  bar->addWidget(m_filter, 1);
  bar->addWidget(refresh);
  auto* v = new QVBoxLayout(this);
  v->setContentsMargins(0, 0, 0, 0);
  v->setSpacing(4);
  v->addLayout(bar);
  auto* statusWrap = new QHBoxLayout;
  statusWrap->setContentsMargins(8, 0, 8, 0);
  statusWrap->addWidget(m_status);
  v->addLayout(statusWrap);
  v->addWidget(m_tree, 1);

  connect(refresh, &QToolButton::clicked, m_ctx.hub, &TopicHub::refreshTopics);
  connect(m_filter, &QLineEdit::textChanged, this, &TopicsPanel::applyFilter);
  connect(m_ctx.hub, &TopicHub::topicsChanged, this, &TopicsPanel::rebuild);
  connect(m_ctx.drone, &Drone::connectionChanged, this, &TopicsPanel::refreshStatus);
  connect(m_tree, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem* it) {
    const QString topic = it->data(0, kTopicRole).toString();
    if (!topic.isEmpty() && !m_open.contains(topic)) {
      m_open.insert(topic);
      m_ctx.hub->acquire(topic);
      fillFields(it);
    }
  });
  connect(m_tree, &QTreeWidget::itemCollapsed, this, [this](QTreeWidgetItem* it) {
    const QString topic = it->data(0, kTopicRole).toString();
    if (m_open.remove(topic)) {
      m_ctx.hub->release(topic);
    }
  });
  connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* it, int) {
    const QString path = it->data(0, kPathRole).toString();
    if (!path.isEmpty()) {
      emit plotRequested(path);
      return;
    }
    const QString topic = it->data(0, kTopicRole).toString();
    if (!topic.isEmpty()) {
      emit rawRequested(topic);
    }
  });

  m_timer.setInterval(250);
  connect(&m_timer, &QTimer::timeout, this, &TopicsPanel::refreshValues);
  m_timer.start();
  rebuild();
}

TopicsPanel::~TopicsPanel()
{
  for (const QString& t : std::as_const(m_open)) {
    m_ctx.hub->release(t);
  }
}

void TopicsPanel::refreshStatus()
{
  if (!m_ctx.drone->isConnected()) {
    m_status->setText(tr("Not connected."));
  } else if (!m_ctx.hub->rosapiAvailable()) {
    m_status->setText(m_ctx.hub->rosapiError().isEmpty()
                        ? tr("Loading topics...")
                        : tr("<span style='color:%1'>Topic list unavailable (%2).</span> rosapi must run "
                             "on the drone: launch rosbridge with "
                             "<code>rosbridge_websocket_launch.xml</code> (it starts rosapi).")
                            .arg(theme::css(theme::kWarn), m_ctx.hub->rosapiError().toHtmlEscaped()));
  } else {
    m_status->setText(tr("%n topic(s) · expand one to see its fields · drag a field to a plot", nullptr,
                         m_ctx.hub->topics().size()));
  }
}

void TopicsPanel::rebuild()
{
  const auto& topics = m_ctx.hub->topics();
  QSet<QString> names;
  for (const TopicHub::Topic& t : topics) {
    names.insert(t.name);
    QTreeWidgetItem* it = m_items.value(t.name);
    if (!it) {
      it = new QTreeWidgetItem(m_tree);
      it->setText(0, t.name);
      it->setData(0, kTopicRole, t.name);
      it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
      m_items.insert(t.name, it);
    }
    it->setText(1, t.type);
    it->setToolTip(1, t.type);
    const bool heavy = TopicHub::isHeavyType(t.type);
    it->setChildIndicatorPolicy(heavy ? QTreeWidgetItem::DontShowIndicator
                                      : QTreeWidgetItem::ShowIndicator);
    it->setToolTip(0, heavy ? tr("Large message type: not decoded here")
                            : tr("Expand to subscribe and see the fields · double-click: raw messages"));
  }
  // Topics that disappeared
  for (auto it = m_items.begin(); it != m_items.end();) {
    if (!names.contains(it.key())) {
      if (m_open.remove(it.key())) {
        m_ctx.hub->release(it.key());
      }
      delete it.value();
      it = m_items.erase(it);
    } else {
      ++it;
    }
  }
  m_tree->sortItems(0, Qt::AscendingOrder);
  applyFilter();
  refreshStatus();
}

void TopicsPanel::applyFilter()
{
  const QString f = m_filter->text().trimmed();
  for (auto it = m_items.cbegin(); it != m_items.cend(); ++it) {
    const bool show = f.isEmpty() || it.key().contains(f, Qt::CaseInsensitive) ||
                      it.value()->text(1).contains(f, Qt::CaseInsensitive);
    it.value()->setHidden(!show);
  }
}

void TopicsPanel::fillFields(QTreeWidgetItem* topicItem)
{
  const QString topic = topicItem->data(0, kTopicRole).toString();
  if (!m_ctx.hub->hasMessage(topic)) {
    if (topicItem->childCount() == 0) {
      auto* wait = new QTreeWidgetItem(topicItem);
      wait->setText(0, tr("waiting for a message..."));
      wait->setForeground(0, theme::kMuted);
      wait->setFlags(Qt::ItemIsEnabled);
    }
    return;
  }
  const auto leaves = msgpath::numericLeaves(m_ctx.hub->lastMessage(topic));
  // Reuse rows by index (the layout of a topic's message does not change).
  if (topicItem->childCount() == 1 && topicItem->child(0)->data(0, kPathRole).toString().isEmpty()) {
    delete topicItem->takeChild(0);
  }
  while (topicItem->childCount() > leaves.size()) {
    delete topicItem->takeChild(topicItem->childCount() - 1);
  }
  for (int i = 0; i < leaves.size(); ++i) {
    QTreeWidgetItem* c = i < topicItem->childCount() ? topicItem->child(i) : new QTreeWidgetItem(topicItem);
    const QString path = topic + QLatin1Char('.') + leaves[i].first;
    if (c->data(0, kPathRole).toString() != path) {
      c->setText(0, leaves[i].first);
      c->setData(0, kPathRole, path);
      c->setToolTip(0, tr("%1\ndouble-click: plot · drag: onto a Plot or Gauge").arg(path));
      c->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled);
    }
    const double v = leaves[i].second;
    c->setText(1, std::abs(v) >= 1e5 || (std::abs(v) < 1e-3 && v != 0.0) ? QString::number(v, 'g', 5)
                                                                           : QString::number(v, 'f', 4));
  }
  if (leaves.isEmpty()) {
    auto* none = new QTreeWidgetItem(topicItem);
    none->setText(0, tr("no numeric fields"));
    none->setForeground(0, theme::kMuted);
  }
}

void TopicsPanel::refreshValues()
{
  if (!isVisible()) {
    return;
  }
  for (auto it = m_items.cbegin(); it != m_items.cend(); ++it) {
    it.value()->setText(2, hzText(m_ctx.hub->rateHz(it.key())));
  }
  for (const QString& t : std::as_const(m_open)) {
    if (QTreeWidgetItem* it = m_items.value(t)) {
      fillFields(it);
    }
  }
}

}  // namespace magcs
