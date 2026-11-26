#include "widgets/DroneTopView.hpp"

#include <QPainter>
#include <QPainterPath>
#include <cmath>

#include "widgets/Theme.hpp"

namespace magcs {

namespace {

// Motor positions on the frame, degrees counter-clockwise from body +x (forward).
constexpr double kMotorAngleDeg[3] = {0.0, 120.0, 240.0};
const QColor kAxisX(0xe8, 0x5d, 0x5d);
const QColor kAxisY(0x3d, 0xd6, 0x8c);

void arrow(QPainter& p, QPointF from, QPointF to, const QColor& c, double width, double head)
{
  const QLineF l(from, to);
  if (l.length() < 1.0) {
    return;
  }
  p.setPen(QPen(c, width, Qt::SolidLine, Qt::RoundCap));
  p.drawLine(QLineF(from, l.pointAt(std::max(0.0, 1.0 - head * 0.7 / l.length()))));
  const double a = std::atan2(-(to.y() - from.y()), to.x() - from.x());
  QPolygonF tip;
  tip << to
      << to - QPointF(std::cos(a + M_PI / 7) * head, -std::sin(a + M_PI / 7) * head)
      << to - QPointF(std::cos(a - M_PI / 7) * head, -std::sin(a - M_PI / 7) * head);
  p.setPen(Qt::NoPen);
  p.setBrush(c);
  p.drawPolygon(tip);
}

}  // namespace

DroneTopView::DroneTopView(QWidget* parent)
: QWidget(parent)
{
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void DroneTopView::setCommand(double vx, double vy, double wz)
{
  m_vx = vx;
  m_vy = vy;
  m_wz = wz;
  update();
}

void DroneTopView::setActive(bool on)
{
  m_active = on;
  update();
}

void DroneTopView::setPressed(const QSet<int>& keys)
{
  m_pressed = keys;
  update();
}

void DroneTopView::paintEvent(QPaintEvent*)
{
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  p.fillRect(rect(), theme::kPanel);

  const double keysH = 74.0;
  const QRectF area(0, 0, width(), std::max(60.0, height() - keysH));
  const QPointF c = area.center() + QPointF(0, 6);
  const double R = std::min(area.width(), area.height()) / 2.0 * 0.66;

  // body (x fwd, y left) -> screen
  auto S = [&](double bx, double by) {return c + QPointF(-by, -bx);};
  auto polar = [&](double deg, double r) {
    const double a = deg * M_PI / 180.0;
    return S(r * std::cos(a), r * std::sin(a));
  };

  const double dim = m_active ? 1.0 : 0.55;
  auto faded = [&](QColor col) {
    col.setAlphaF(col.alphaF() * dim);
    return col;
  };

  // Frame
  QPainterPath frame;
  frame.moveTo(polar(kMotorAngleDeg[0], R));
  frame.lineTo(polar(kMotorAngleDeg[1], R));
  frame.lineTo(polar(kMotorAngleDeg[2], R));
  frame.closeSubpath();
  p.setPen(QPen(faded(theme::kBorder.lighter(160)), R * 0.10, Qt::SolidLine, Qt::RoundCap,
                Qt::RoundJoin));
  p.setBrush(faded(QColor(0x2b, 0x30, 0x38)));
  p.drawPath(frame);

  // Electronics (Pi) block, slightly towards the back
  p.setPen(QPen(faded(theme::kBorder.lighter(140)), 1));
  p.setBrush(faded(QColor(0x1f, 0x5e, 0x3a)));
  const QPointF pc = S(-R * 0.18, 0);
  p.drawRoundedRect(QRectF(pc.x() - R * 0.22, pc.y() - R * 0.14, R * 0.44, R * 0.28), 3, 3);

  // Motors
  QFont f = font();
  f.setBold(true);
  f.setPixelSize(std::max(10, static_cast<int>(R * 0.11)));
  p.setFont(f);
  for (int i = 0; i < 3; ++i) {
    const QPointF m = polar(kMotorAngleDeg[i], R);
    QColor disk = theme::kAccent;
    disk.setAlpha(m_active ? 45 : 20);
    p.setPen(QPen(faded(theme::kAccent), 1, Qt::DashLine));
    p.setBrush(disk);
    p.drawEllipse(m, R * 0.26, R * 0.26);
    p.setPen(QPen(faded(theme::kBorder.lighter(170)), 1.5));
    p.setBrush(faded(QColor(0x3a, 0x3f, 0x48)));
    p.drawEllipse(m, R * 0.12, R * 0.12);
    p.setPen(faded(theme::kText));
    p.drawText(QRectF(m.x() - 12, m.y() - 12, 24, 24), Qt::AlignCenter, QString::number(i + 1));
  }

  // Body axes
  const double axisL = R * 0.62;
  arrow(p, c, S(axisL, 0), faded(kAxisX), 2.2, 9);
  arrow(p, c, S(0, axisL), faded(kAxisY), 2.2, 9);
  QFont small = font();
  small.setPixelSize(12);
  small.setBold(true);
  p.setFont(small);
  p.setPen(faded(kAxisX));
  const QPointF xl = S(axisL + 14, 0);
  p.drawText(QRectF(xl.x() - 10, xl.y() - 10, 20, 20), Qt::AlignCenter, QStringLiteral("x"));
  p.setPen(faded(kAxisY));
  const QPointF yl = S(0, axisL + 14);
  p.drawText(QRectF(yl.x() - 10, yl.y() - 10, 20, 20), Qt::AlignCenter, QStringLiteral("y"));
  p.setPen(Qt::NoPen);
  p.setBrush(faded(theme::kText));
  p.drawEllipse(c, 3, 3);

  // Command
  if (m_active) {
    const QPointF tip = S(m_vx * R * 0.95, m_vy * R * 0.95);
    if (QLineF(c, tip).length() > 2) {
      arrow(p, c, tip, theme::kAccent.lighter(120), 4.0, 14);
    }
    if (std::abs(m_wz) > 1e-3) {
      const double rr = R * 0.36;
      const QRectF box(c.x() - rr, c.y() - rr, 2 * rr, 2 * rr);
      const double span = 150.0 * m_wz;  // CCW positive (Qt: positive = CCW)
      const double start = 90.0;         // from the +x (up) direction
      p.setPen(QPen(theme::kWarn, 3.5, Qt::SolidLine, Qt::RoundCap));
      p.setBrush(Qt::NoBrush);
      p.drawArc(box, static_cast<int>(start * 16), static_cast<int>(span * 16));
      const double endA = (start + span) * M_PI / 180.0;
      const QPointF end = c + QPointF(rr * std::cos(endA), -rr * std::sin(endA));
      const double dir = endA + (m_wz > 0 ? M_PI / 2 : -M_PI / 2);
      arrow(p, end - QPointF(std::cos(dir), -std::sin(dir)) * 2,
            end + QPointF(std::cos(dir), -std::sin(dir)) * 9, theme::kWarn, 3.0, 10);
    }
  }

  // Readout
  QFont mono = font();
  mono.setFamily(QStringLiteral("Menlo"));
  mono.setStyleHint(QFont::Monospace);
  mono.setPixelSize(12);
  p.setFont(mono);
  p.setPen(m_active ? theme::kText : theme::kMuted);
  p.drawText(QRectF(8, 6, width() - 16, 18), Qt::AlignLeft | Qt::AlignVCenter,
             m_active ? QString::asprintf("x %+.2f   y %+.2f   yaw %+.2f", m_vx, m_vy, m_wz)
                      : tr("fly mode off"));

  // Key caps:  Q W E / A S D
  const double k = 28.0;
  const double gap = 5.0;
  const double kx = width() / 2.0 - (3 * k + 2 * gap) / 2.0;
  const double ky = height() - keysH + 8;
  const struct { int key; const char* text; int row; int col; } caps[] = {
    {Qt::Key_Q, "Q", 0, 0}, {Qt::Key_W, "W", 0, 1}, {Qt::Key_E, "E", 0, 2},
    {Qt::Key_A, "A", 1, 0}, {Qt::Key_S, "S", 1, 1}, {Qt::Key_D, "D", 1, 2},
  };
  QFont capFont = font();
  capFont.setBold(true);
  capFont.setPixelSize(12);
  p.setFont(capFont);
  for (const auto& cap : caps) {
    const QRectF r(kx + cap.col * (k + gap), ky + cap.row * (k + gap), k, k);
    const bool down = m_pressed.contains(cap.key);
    p.setPen(QPen(down ? theme::kAccent : theme::kBorder, 1.2));
    p.setBrush(down ? theme::kAccent : theme::kRaised);
    p.drawRoundedRect(r, 5, 5);
    p.setPen(down ? Qt::white : (m_active ? theme::kText : theme::kMuted));
    p.drawText(r, Qt::AlignCenter, QString::fromLatin1(cap.text));
  }
  p.setFont(small);
  p.setPen(theme::kMuted);
  QFont hint = font();
  hint.setPixelSize(11);
  p.setFont(hint);
  p.drawText(QRectF(kx + 3 * (k + gap) + 6, ky, 140, 2 * k + gap), Qt::AlignLeft | Qt::AlignVCenter,
             tr("W/S  x ±\nA/D  y ±\nQ/E  yaw ±"));
}

}  // namespace magcs
