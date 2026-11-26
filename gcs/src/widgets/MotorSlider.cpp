#include "widgets/MotorSlider.hpp"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

#include "widgets/Theme.hpp"

namespace magcs {

namespace {
const QColor kReverse(0xf5, 0x9f, 0x3a);
constexpr double kNameW = 64.0;
constexpr double kValueW = 64.0;
}  // namespace

MotorSlider::MotorSlider(const QString& name, QWidget* parent)
: QWidget(parent), m_name(name)
{
  setFocusPolicy(Qt::StrongFocus);
  setCursor(Qt::PointingHandCursor);
  setToolTip(tr("Drag to set the throttle · double-click to zero · wheel / arrow keys for fine steps"));
}

void MotorSlider::setValue(double v)
{
  v = std::clamp(v, -1.0, 1.0);
  if (v != m_value) {
    m_value = v;
    update();
  }
}

void MotorSlider::setLimit(double limit)
{
  m_limit = std::clamp(limit, 0.0, 1.0);
  update();
}

void MotorSlider::setActual(double v, bool valid)
{
  m_actual = std::clamp(v, -1.0, 1.0);
  m_actualValid = valid;
  update();
}

QRectF MotorSlider::track() const
{
  const double h = 6.0;
  return QRectF(kNameW, height() / 2.0 - h / 2.0, std::max(20.0, width() - kNameW - kValueW - 8), h);
}

double MotorSlider::valueAt(double x) const
{
  const QRectF t = track();
  return std::clamp((x - t.center().x()) / (t.width() / 2.0), -1.0, 1.0);
}

void MotorSlider::edit(double v)
{
  v = std::round(std::clamp(v, -1.0, 1.0) * 1000.0) / 1000.0;
  if (v == m_value) {
    return;
  }
  m_value = v;
  update();
  emit valueEdited(v);
}

void MotorSlider::paintEvent(QPaintEvent*)
{
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  const QRectF t = track();
  const double cx = t.center().x();
  const double half = t.width() / 2.0;
  auto xOf = [&](double v) {return cx + v * half;};
  const bool on = isEnabled();

  // Name
  p.setPen(on ? theme::kText : theme::kMuted);
  p.drawText(QRectF(0, 0, kNameW - 8, height()), Qt::AlignLeft | Qt::AlignVCenter, m_name);

  // Track + clamp zones
  p.setPen(Qt::NoPen);
  p.setBrush(theme::kRaised);
  p.drawRoundedRect(t, 3, 3);
  if (m_limit < 1.0) {
    QColor z = theme::kBad;
    z.setAlpha(40);
    p.setBrush(z);
    p.drawRoundedRect(QRectF(t.left(), t.top(), xOf(-m_limit) - t.left(), t.height()), 3, 3);
    p.drawRoundedRect(QRectF(xOf(m_limit), t.top(), t.right() - xOf(m_limit), t.height()), 3, 3);
  }

  // Fill from centre
  const QColor fill = !on ? theme::kBorder : (m_value >= 0 ? theme::kAccent : kReverse);
  p.setBrush(fill);
  const double vx = xOf(m_value);
  p.drawRoundedRect(QRectF(std::min(cx, vx), t.top(), std::abs(vx - cx), t.height()), 3, 3);

  // Ticks: centre, +-0.5, limits
  p.setPen(QPen(theme::kMuted, 1));
  p.drawLine(QPointF(cx, t.top() - 5), QPointF(cx, t.bottom() + 5));
  p.setPen(QPen(theme::kBorder.lighter(130), 1));
  for (double v : {-0.5, 0.5}) {
    p.drawLine(QPointF(xOf(v), t.bottom() + 2), QPointF(xOf(v), t.bottom() + 5));
  }
  if (m_limit < 1.0) {
    p.setPen(QPen(theme::kBad.lighter(120), 1));
    for (double v : {-m_limit, m_limit}) {
      p.drawLine(QPointF(xOf(v), t.top() - 4), QPointF(xOf(v), t.bottom() + 4));
    }
  }

  // Actual output from the drone
  if (m_actualValid) {
    const double ax = xOf(m_actual);
    p.setPen(Qt::NoPen);
    p.setBrush(theme::kOk);
    QPolygonF tri;
    tri << QPointF(ax, t.top() - 2) << QPointF(ax - 4, t.top() - 8) << QPointF(ax + 4, t.top() - 8);
    p.drawPolygon(tri);
  }

  // Handle
  p.setPen(QPen(hasFocus() ? theme::kAccent : theme::kBorder.lighter(150), 1.5));
  p.setBrush(on ? theme::kText : theme::kMuted);
  p.drawEllipse(QPointF(vx, t.center().y()), 7, 7);

  // Value
  QFont mono = font();
  mono.setFamily(QStringLiteral("Menlo"));
  mono.setStyleHint(QFont::Monospace);
  p.setFont(mono);
  p.setPen(on ? (m_value == 0.0 ? theme::kMuted : theme::kText) : theme::kMuted);
  p.drawText(QRectF(width() - kValueW, 0, kValueW, height()), Qt::AlignRight | Qt::AlignVCenter,
             QString::asprintf("%+.3f", m_value));
}

void MotorSlider::mousePressEvent(QMouseEvent* e)
{
  if (e->button() == Qt::LeftButton) {
    m_dragging = true;
    edit(valueAt(e->position().x()));
  }
}

void MotorSlider::mouseMoveEvent(QMouseEvent* e)
{
  if (m_dragging) {
    edit(valueAt(e->position().x()));
  }
}

void MotorSlider::mouseReleaseEvent(QMouseEvent*)
{
  m_dragging = false;
}

void MotorSlider::mouseDoubleClickEvent(QMouseEvent*)
{
  edit(0.0);
}

void MotorSlider::wheelEvent(QWheelEvent* e)
{
  const double steps = e->angleDelta().y() / 120.0;
  edit(m_value + steps * 0.01);
  e->accept();
}

void MotorSlider::keyPressEvent(QKeyEvent* e)
{
  const double step = (e->modifiers() & Qt::ShiftModifier) ? 0.1 : 0.01;
  switch (e->key()) {
    case Qt::Key_Right:
    case Qt::Key_Up: edit(m_value + step); break;
    case Qt::Key_Left:
    case Qt::Key_Down: edit(m_value - step); break;
    case Qt::Key_0:
    case Qt::Key_Home: edit(0.0); break;
    default: QWidget::keyPressEvent(e);
  }
}

}  // namespace magcs
