#include "panels/PlotPanel.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QColorDialog>
#include <QComboBox>
#include <QCompleter>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFocusEvent>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QStringListModel>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

#include "core/MessagePath.hpp"
#include "core/TopicHub.hpp"
#include "widgets/Theme.hpp"
#include "widgets/TimePlot.hpp"

namespace magcs {

namespace {
const double kWindows[] = {5, 10, 30, 60, 120, 300, 600};
}

PlotPanel::PlotPanel(const PanelContext& ctx, QWidget* parent)
: Panel(ctx, parent)
{
  setAcceptDrops(true);

  m_plot = new TimePlot;
  m_plot->setClock([hub = m_ctx.hub]() {return hub->now();});
  m_plot->installEventFilter(this);

  // ---- toolbar: path + add | window | y range | pause | clear
  m_path = new QLineEdit;
  m_path->setPlaceholderText(tr("/topic.field  e.g. /motor_cmd.throttle[0]"));
  m_path->setClearButtonEnabled(true);
  m_completions = new QStringListModel(this);
  m_completer = new QCompleter(m_completions, this);
  m_completer->setCaseSensitivity(Qt::CaseInsensitive);
  m_completer->setFilterMode(Qt::MatchContains);
  m_completer->setMaxVisibleItems(14);
  m_path->setCompleter(m_completer);
  m_path->installEventFilter(this);
  auto* add = new QToolButton;
  add->setText(QStringLiteral("+"));
  add->setToolTip(tr("Add series"));

  m_window = new QComboBox;
  for (double w : kWindows) {
    m_window->addItem(w < 60 ? tr("%1 s").arg(w) : tr("%1 min").arg(w / 60), w);
  }
  m_window->setToolTip(tr("Time window (mouse wheel on the plot zooms)"));

  m_autoY = new QCheckBox(tr("Auto Y"));
  m_autoY->setChecked(true);
  m_yMin = new QDoubleSpinBox;
  m_yMax = new QDoubleSpinBox;
  for (QDoubleSpinBox* s : {m_yMin, m_yMax}) {
    s->setRange(-1e9, 1e9);
    s->setDecimals(3);
    s->setButtonSymbols(QAbstractSpinBox::NoButtons);
    s->setFixedWidth(70);
  }
  m_yMin->setValue(-1.0);
  m_yMax->setValue(1.0);
  m_yMin->setToolTip(tr("y min"));
  m_yMax->setToolTip(tr("y max"));

  m_pause = new QToolButton;
  m_pause->setCheckable(true);
  m_pause->setIcon(style()->standardIcon(QStyle::SP_MediaPause));
  m_pause->setToolTip(tr("Pause the view (data keeps being recorded)"));
  auto* clear = new QToolButton;
  clear->setIcon(style()->standardIcon(QStyle::SP_DialogResetButton));
  clear->setToolTip(tr("Clear data"));

  auto* bar = new QHBoxLayout;
  bar->setContentsMargins(6, 6, 6, 0);
  bar->setSpacing(6);
  bar->addWidget(m_path, 1);
  bar->addWidget(add);
  bar->addSpacing(8);
  bar->addWidget(m_window);
  bar->addWidget(m_autoY);
  bar->addWidget(m_yMin);
  bar->addWidget(m_yMax);
  bar->addWidget(m_pause);
  bar->addWidget(clear);

  auto* v = new QVBoxLayout(this);
  v->setContentsMargins(0, 0, 0, 0);
  v->setSpacing(4);
  v->addLayout(bar);
  v->addWidget(m_plot, 1);

  m_window->setCurrentIndex(2);  // 30 s
  syncYControls();

  connect(add, &QToolButton::clicked, this, &PlotPanel::addFromEdit);
  connect(m_path, &QLineEdit::returnPressed, this, &PlotPanel::addFromEdit);
  connect(m_path, &QLineEdit::textEdited, this, [this](const QString& text) {
    const msgpath::Path p = msgpath::parse(text);
    setProbe(text.contains(QLatin1Char('.')) && p.valid() ? p.topic : QString());
    updateCompletions();
  });
  connect(m_window, &QComboBox::currentIndexChanged, this, [this](int i) {
    m_plot->setWindow(m_window->itemData(i).toDouble());
  });
  connect(m_plot, &TimePlot::windowChanged, this, [this](double w) {
    // Wheel zoom: show the closest preset without fighting the exact value.
    QSignalBlocker b(m_window);
    int best = 0;
    for (int i = 0; i < m_window->count(); ++i) {
      if (std::abs(m_window->itemData(i).toDouble() - w) <
          std::abs(m_window->itemData(best).toDouble() - w)) {
        best = i;
      }
    }
    m_window->setCurrentIndex(best);
  });
  connect(m_autoY, &QCheckBox::toggled, this, [this](bool on) {
    m_plot->setAutoY(on);
    syncYControls();
  });
  for (QDoubleSpinBox* s : {m_yMin, m_yMax}) {
    connect(s, &QDoubleSpinBox::valueChanged, this, [this]() {
      m_plot->setYRange(m_yMin->value(), m_yMax->value());
    });
  }
  connect(m_pause, &QToolButton::toggled, m_plot, &TimePlot::setPaused);
  connect(clear, &QToolButton::clicked, m_plot, &TimePlot::clearData);
  connect(m_plot, &TimePlot::colorClicked, this, [this](int id) {
    const QColor c = QColorDialog::getColor(m_plot->color(id), this, tr("Series colour"));
    if (c.isValid()) {
      m_plot->setColor(id, c);
    }
  });
  connect(m_plot, &TimePlot::legendMenuRequested, this, &PlotPanel::showLegendMenu);
  connect(m_ctx.hub, &TopicHub::message, this, &PlotPanel::onMessage);
  connect(m_ctx.hub, &TopicHub::topicsChanged, this, &PlotPanel::updateCompletions);
  updateCompletions();
}

PlotPanel::~PlotPanel()
{
  for (const Series& s : m_series) {
    m_ctx.hub->release(s.topic);
  }
  setProbe(QString());
}

void PlotPanel::syncYControls()
{
  m_yMin->setVisible(!m_autoY->isChecked());
  m_yMax->setVisible(!m_autoY->isChecked());
  if (!m_autoY->isChecked()) {
    m_plot->setYRange(m_yMin->value(), m_yMax->value());
  }
}

void PlotPanel::addPath(const QString& text, const QColor& color, bool visible)
{
  const msgpath::Path p = msgpath::parse(text);
  if (!p.valid() || p.field.isEmpty()) {
    return;
  }
  for (const Series& s : m_series) {
    if (s.path == p.full()) {
      return;  // already plotted
    }
  }
  Series s;
  s.path = p.full();
  s.topic = p.topic;
  s.field = p.field;
  const QColor c = color.isValid() ? color : theme::seriesColor(m_series.size());
  s.id = m_plot->addSeries(s.path, c);
  m_plot->setSeriesVisible(s.id, visible);
  m_series.append(s);
  m_ctx.hub->acquire(s.topic);
  // Seed with the last value we already have, so the series shows up at once.
  if (m_ctx.hub->hasMessage(s.topic)) {
    if (const auto x = msgpath::numberAt(m_ctx.hub->lastMessage(s.topic), s.field)) {
      m_plot->append(s.id, m_ctx.hub->lastTime(s.topic), *x);
    }
  }
}

void PlotPanel::addFromEdit()
{
  const QString text = m_path->text().trimmed();
  if (text.isEmpty()) {
    return;
  }
  const msgpath::Path p = msgpath::parse(text);
  if (!p.valid() || p.field.isEmpty()) {
    m_path->setStyleSheet(QStringLiteral("border-color: %1;").arg(theme::css(theme::kBad)));
    m_path->setToolTip(tr("Use /topic.field, e.g. /motor_cmd.throttle[0]"));
    return;
  }
  m_path->setStyleSheet(QString());
  addPath(text);
  m_path->clear();
  setProbe(QString());
  m_plot->setFocus();  // leave the text field: Space must disarm again
}

void PlotPanel::removeSeries(int id)
{
  for (int i = 0; i < m_series.size(); ++i) {
    if (m_series[i].id == id) {
      m_ctx.hub->release(m_series[i].topic);
      m_plot->removeSeries(id);
      m_series.removeAt(i);
      return;
    }
  }
}

void PlotPanel::showLegendMenu(int id, const QPoint& pos)
{
  QString path;
  for (const Series& s : m_series) {
    if (s.id == id) {
      path = s.path;
    }
  }
  QMenu menu(this);
  menu.addAction(tr("Change colour..."), this, [this, id]() {
    const QColor c = QColorDialog::getColor(m_plot->color(id), this, tr("Series colour"));
    if (c.isValid()) {
      m_plot->setColor(id, c);
    }
  });
  menu.addAction(m_plot->seriesVisible(id) ? tr("Hide") : tr("Show"), this,
                 [this, id]() {m_plot->setSeriesVisible(id, !m_plot->seriesVisible(id));});
  menu.addAction(tr("Copy path"), this, [path]() {QApplication::clipboard()->setText(path);});
  menu.addSeparator();
  menu.addAction(tr("Remove"), this, [this, id]() {removeSeries(id);});
  menu.exec(pos);
}

void PlotPanel::onMessage(const QString& topic, const QJsonObject& msg, double t)
{
  for (const Series& s : m_series) {
    if (s.topic == topic) {
      if (const auto x = msgpath::numberAt(msg, s.field)) {
        m_plot->append(s.id, t, *x);
      }
    }
  }
  if (topic == m_probe && m_completions->rowCount() < 2) {
    updateCompletions();
  }
}

void PlotPanel::setProbe(const QString& topic)
{
  if (topic == m_probe) {
    return;
  }
  if (!m_probe.isEmpty()) {
    m_ctx.hub->release(m_probe);
  }
  m_probe = topic;
  if (!m_probe.isEmpty()) {
    m_ctx.hub->acquire(m_probe);
  }
}

void PlotPanel::updateCompletions()
{
  QStringList items;
  for (const TopicHub::Topic& t : m_ctx.hub->topics()) {
    if (TopicHub::isHeavyType(t.type)) {
      continue;
    }
    const QStringList fields = m_ctx.hub->numericFields(t.name);
    if (fields.isEmpty()) {
      items << t.name + QLatin1Char('.');
    }
    for (const QString& f : fields) {
      items << t.name + QLatin1Char('.') + f;
    }
  }
  if (!m_probe.isEmpty() && m_ctx.hub->typeOf(m_probe).isEmpty()) {
    for (const QString& f : m_ctx.hub->numericFields(m_probe)) {
      items << m_probe + QLatin1Char('.') + f;
    }
  }
  items.removeDuplicates();
  m_completions->setStringList(items);
}

QJsonObject PlotPanel::saveConfig() const
{
  QJsonArray series;
  for (const Series& s : m_series) {
    series.append(QJsonObject{{"path", s.path},
                              {"color", m_plot->color(s.id).name()},
                              {"visible", m_plot->seriesVisible(s.id)}});
  }
  return {{"window", m_plot->window()},
          {"autoY", m_autoY->isChecked()},
          {"yMin", m_yMin->value()},
          {"yMax", m_yMax->value()},
          {"series", series}};
}

void PlotPanel::loadConfig(const QJsonObject& cfg)
{
  const double w = cfg.value("window").toDouble(30.0);
  m_plot->setWindow(w);
  int idx = m_window->findData(w);
  if (idx >= 0) {
    m_window->setCurrentIndex(idx);
  }
  m_yMin->setValue(cfg.value("yMin").toDouble(-1.0));
  m_yMax->setValue(cfg.value("yMax").toDouble(1.0));
  m_autoY->setChecked(cfg.value("autoY").toBool(true));
  syncYControls();
  for (const QJsonValue& v : cfg.value("series").toArray()) {
    const QJsonObject o = v.toObject();
    addPath(o.value("path").toString(), QColor(o.value("color").toString()),
            o.value("visible").toBool(true));
  }
}

void PlotPanel::dragEnterEvent(QDragEnterEvent* e)
{
  if (!pathFromMime(e->mimeData()).isEmpty()) {
    e->acceptProposedAction();
  }
}

void PlotPanel::dropEvent(QDropEvent* e)
{
  const QString path = pathFromMime(e->mimeData());
  if (!path.isEmpty()) {
    addPath(path);
    e->acceptProposedAction();
    emit activated(this);
  }
}

bool PlotPanel::eventFilter(QObject* o, QEvent* e)
{
  if (e->type() == QEvent::MouseButtonPress || e->type() == QEvent::FocusIn) {
    emit activated(this);
  }
  if (o == m_path && e->type() == QEvent::FocusOut &&
      static_cast<QFocusEvent*>(e)->reason() != Qt::PopupFocusReason) {
    setProbe(QString());
  }
  return Panel::eventFilter(o, e);
}

}  // namespace magcs
