#include "panels/GaugePanel.hpp"

#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

#include "core/MessagePath.hpp"
#include "core/TopicHub.hpp"
#include "widgets/Theme.hpp"

namespace magcs {

class GaugeView : public QWidget {
public:
  using QWidget::QWidget;

  void setRange(double lo, double hi)
  {
    m_lo = lo;
    m_hi = hi > lo ? hi : lo + 1.0;
    update();
  }
  void setValue(double v, bool valid)
  {
    m_v = v;
    m_valid = valid;
    update();
  }
  void setLabel(const QString& s)
  {
    m_label = s;
    update();
  }
  QSize sizeHint() const override { return {220, 120}; }

protected:
  void paintEvent(QPaintEvent*) override
  {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), theme::kPanel);
    const QRectF r = QRectF(rect()).adjusted(14, 8, -14, -10);

    QFont small = font();
    small.setPixelSize(11);
    p.setFont(small);
    p.setPen(theme::kMuted);
    p.drawText(QRectF(r.left(), r.top(), r.width(), 16), Qt::AlignLeft | Qt::AlignVCenter,
               m_label.isEmpty() ? tr("drop a field here") : m_label);

    QFont big = font();
    big.setPixelSize(std::clamp(static_cast<int>(r.height() * 0.38), 16, 64));
    big.setWeight(QFont::DemiBold);
    p.setFont(big);
    p.setPen(m_valid ? theme::kText : theme::kMuted);
    const QString text = !m_valid ? QStringLiteral("-")
                         : (std::abs(m_v) >= 1e5 || (std::abs(m_v) < 1e-3 && m_v != 0.0))
                           ? QString::number(m_v, 'g', 4)
                           : QString::number(m_v, 'f', 3);
    const QRectF valueRect(r.left(), r.top() + 16, r.width(), r.height() - 16 - 22);
    p.drawText(valueRect, Qt::AlignCenter, text);

    const QRectF bar(r.left(), r.bottom() - 10, r.width(), 8);
    p.setPen(Qt::NoPen);
    p.setBrush(theme::kRaised);
    p.drawRoundedRect(bar, 4, 4);
    if (m_valid) {
      const double f = std::clamp((m_v - m_lo) / (m_hi - m_lo), 0.0, 1.0);
      const bool out = m_v < m_lo || m_v > m_hi;
      p.setBrush(out ? theme::kWarn : theme::kAccent);
      p.drawRoundedRect(QRectF(bar.left(), bar.top(), std::max(8.0, bar.width() * f), bar.height()), 4, 4);
    }
    p.setFont(small);
    p.setPen(theme::kMuted);
    p.drawText(QRectF(bar.left(), bar.top() - 16, 80, 14), Qt::AlignLeft, QString::number(m_lo, 'g', 4));
    p.drawText(QRectF(bar.right() - 80, bar.top() - 16, 80, 14), Qt::AlignRight, QString::number(m_hi, 'g', 4));
  }

private:
  double m_lo = 0.0;
  double m_hi = 1.0;
  double m_v = 0.0;
  bool m_valid = false;
  QString m_label;
};

GaugePanel::GaugePanel(const PanelContext& ctx, QWidget* parent)
: Panel(ctx, parent)
{
  setAcceptDrops(true);
  m_view = new GaugeView;

  m_path = new QLineEdit;
  m_path->setPlaceholderText(tr("/topic.field"));
  m_min = new QDoubleSpinBox;
  m_max = new QDoubleSpinBox;
  for (QDoubleSpinBox* s : {m_min, m_max}) {
    s->setRange(-1e9, 1e9);
    s->setDecimals(3);
    s->setButtonSymbols(QAbstractSpinBox::NoButtons);
    s->setFixedWidth(70);
  }
  m_min->setValue(-1.0);
  m_max->setValue(1.0);

  m_settings = new QWidget;
  auto* row = new QHBoxLayout(m_settings);
  row->setContentsMargins(0, 0, 0, 0);
  row->addWidget(m_path, 1);
  row->addWidget(new QLabel(tr("min")));
  row->addWidget(m_min);
  row->addWidget(new QLabel(tr("max")));
  row->addWidget(m_max);

  auto* gear = new QToolButton;
  gear->setText(QStringLiteral("⚙"));
  gear->setCheckable(true);
  gear->setChecked(true);
  gear->setAutoRaise(true);
  gear->setToolTip(tr("Show / hide the settings"));

  auto* bar = new QHBoxLayout;
  bar->setContentsMargins(6, 6, 6, 0);
  bar->addWidget(m_settings, 1);
  bar->addWidget(gear, 0, Qt::AlignRight);
  auto* v = new QVBoxLayout(this);
  v->setContentsMargins(0, 0, 0, 0);
  v->addLayout(bar);
  v->addWidget(m_view, 1);

  connect(gear, &QToolButton::toggled, m_settings, &QWidget::setVisible);
  connect(m_path, &QLineEdit::returnPressed, this, [this]() {
    setPath(m_path->text());
    m_view->setFocus();
  });
  for (QDoubleSpinBox* s : {m_min, m_max}) {
    connect(s, &QDoubleSpinBox::valueChanged, this, [this]() {m_view->setRange(m_min->value(), m_max->value());});
  }
  connect(m_ctx.hub, &TopicHub::message, this, &GaugePanel::onMessage);
  m_view->setRange(m_min->value(), m_max->value());
}

GaugePanel::~GaugePanel()
{
  if (!m_topic.isEmpty()) {
    m_ctx.hub->release(m_topic);
  }
}

void GaugePanel::setPath(const QString& text)
{
  const msgpath::Path p = msgpath::parse(text);
  if (!m_topic.isEmpty()) {
    m_ctx.hub->release(m_topic);
    m_topic.clear();
  }
  m_field.clear();
  m_view->setValue(0, false);
  if (!p.valid() || p.field.isEmpty()) {
    m_view->setLabel(QString());
    return;
  }
  m_topic = p.topic;
  m_field = p.field;
  m_path->setText(p.full());
  m_view->setLabel(p.full());
  m_ctx.hub->acquire(m_topic);
  if (m_ctx.hub->hasMessage(m_topic)) {
    const auto x = msgpath::numberAt(m_ctx.hub->lastMessage(m_topic), m_field);
    m_view->setValue(x.value_or(0.0), x.has_value());
  }
}

void GaugePanel::onMessage(const QString& topic, const QJsonObject& msg, double)
{
  if (topic == m_topic) {
    const auto x = msgpath::numberAt(msg, m_field);
    m_view->setValue(x.value_or(0.0), x.has_value());
  }
}

QJsonObject GaugePanel::saveConfig() const
{
  return {{"path", m_topic.isEmpty() ? QString() : m_topic + QLatin1Char('.') + m_field},
          {"min", m_min->value()},
          {"max", m_max->value()}};
}

void GaugePanel::loadConfig(const QJsonObject& cfg)
{
  m_min->setValue(cfg.value("min").toDouble(-1.0));
  m_max->setValue(cfg.value("max").toDouble(1.0));
  setPath(cfg.value("path").toString());
}

void GaugePanel::dragEnterEvent(QDragEnterEvent* e)
{
  if (!pathFromMime(e->mimeData()).isEmpty()) {
    e->acceptProposedAction();
  }
}

void GaugePanel::dropEvent(QDropEvent* e)
{
  const QString path = pathFromMime(e->mimeData());
  if (!path.isEmpty()) {
    setPath(path);
    e->acceptProposedAction();
  }
}

}  // namespace magcs
