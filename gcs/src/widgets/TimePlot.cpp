#include "widgets/TimePlot.hpp"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

#include "widgets/Theme.hpp"

namespace magcs {

namespace {

constexpr double kKeepS = 600.0;       // data kept per series (for zooming out)
constexpr int kMaxPoints = 120000;     // per series
constexpr double kMinWindow = 1.0;
constexpr double kMaxWindow = 600.0;

double niceStep(double range, int target)
{
  if (range <= 0 || !std::isfinite(range)) {
    return 1.0;
  }
  const double raw = range / target;
  const double mag = std::pow(10.0, std::floor(std::log10(raw)));
  const double r = raw / mag;
  const double nice = r < 1.5 ? 1.0 : r < 3.0 ? 2.0 : r < 7.0 ? 5.0 : 10.0;
  return nice * mag;
}

QString fmt(double v, double step)
{
  if (v == 0.0) {
    return QStringLiteral("0");
  }
  const int dec = std::clamp(static_cast<int>(-std::floor(std::log10(std::max(step, 1e-12)))), 0, 6);
  if (std::abs(v) >= 1e5 || std::abs(v) < 1e-4) {
    return QString::number(v, 'g', 4);
  }
  return QString::number(v, 'f', dec);
}

QString valueText(double v)
{
  if (std::abs(v) >= 1e5 || (std::abs(v) < 1e-3 && v != 0.0)) {
    return QString::number(v, 'g', 4);
  }
  return QString::number(v, 'f', 3);
}

}  // namespace

TimePlot::TimePlot(QWidget* parent)
: QWidget(parent)
{
  setMouseTracking(true);
  setAttribute(Qt::WA_OpaquePaintEvent);
  m_frame.setInterval(33);
  connect(&m_frame, &QTimer::timeout, this, qOverload<>(&QWidget::update));
}

int TimePlot::addSeries(const QString& label, const QColor& color)
{
  Series s;
  s.id = m_nextId++;
  s.label = label;
  s.color = color;
  m_series.append(s);
  update();
  return s.id;
}

void TimePlot::removeSeries(int id)
{
  m_series.erase(std::remove_if(m_series.begin(), m_series.end(),
                                [id](const Series& s) {return s.id == id;}),
                 m_series.end());
  update();
}

void TimePlot::clearSeries()
{
  m_series.clear();
  update();
}

void TimePlot::clearData()
{
  for (Series& s : m_series) {
    s.pts.clear();
  }
  update();
}

TimePlot::Series* TimePlot::find(int id)
{
  for (Series& s : m_series) {
    if (s.id == id) {
      return &s;
    }
  }
  return nullptr;
}

const TimePlot::Series* TimePlot::find(int id) const
{
  for (const Series& s : m_series) {
    if (s.id == id) {
      return &s;
    }
  }
  return nullptr;
}

void TimePlot::append(int id, double t, double v)
{
  Series* s = find(id);
  if (!s || !std::isfinite(v)) {
    return;
  }
  if (!s->pts.isEmpty() && t < s->pts.last().x()) {
    t = s->pts.last().x();  // keep t monotonic
  }
  s->pts.append(QPointF(t, v));
  prune(*s, t);
}

void TimePlot::prune(Series& s, double now)
{
  // Erase in batches: cheap enough at any message rate.
  const double cutoff = now - kKeepS;
  if (s.pts.size() > kMaxPoints || (!s.pts.isEmpty() && s.pts.first().x() < cutoff - 30.0)) {
    auto it = std::lower_bound(s.pts.begin(), s.pts.end(), cutoff,
                               [](const QPointF& p, double t) {return p.x() < t;});
    int drop = static_cast<int>(it - s.pts.begin());
    drop = std::max(drop, static_cast<int>(s.pts.size()) - kMaxPoints);
    if (drop > 0) {
      s.pts.remove(0, drop);
    }
  }
}

void TimePlot::setColor(int id, const QColor& c)
{
  if (Series* s = find(id)) {
    s->color = c;
    update();
  }
}

QColor TimePlot::color(int id) const
{
  const Series* s = find(id);
  return s ? s->color : QColor();
}

void TimePlot::setSeriesVisible(int id, bool on)
{
  if (Series* s = find(id)) {
    s->visible = on;
    update();
  }
}

bool TimePlot::seriesVisible(int id) const
{
  const Series* s = find(id);
  return s && s->visible;
}

QString TimePlot::label(int id) const
{
  const Series* s = find(id);
  return s ? s->label : QString();
}

QList<int> TimePlot::seriesIds() const
{
  QList<int> ids;
  for (const Series& s : m_series) {
    ids << s.id;
  }
  return ids;
}

void TimePlot::setWindow(double seconds)
{
  seconds = std::clamp(seconds, kMinWindow, kMaxWindow);
  if (seconds != m_window) {
    m_window = seconds;
    update();
    emit windowChanged(m_window);
  }
}

void TimePlot::setAutoY(bool on)
{
  m_autoY = on;
  update();
}

void TimePlot::setYRange(double lo, double hi)
{
  if (hi <= lo) {
    hi = lo + 1.0;
  }
  m_yMin = lo;
  m_yMax = hi;
  update();
}

void TimePlot::setPaused(bool on)
{
  if (on && !m_paused) {
    m_pausedAt = m_now ? m_now() : 0.0;
  }
  m_paused = on;
  update();
}

double TimePlot::viewNow() const
{
  if (m_paused) {
    return m_pausedAt;
  }
  return m_now ? m_now() : 0.0;
}

QRectF TimePlot::plotRect() const
{
  // Left margin for y labels, bottom for x labels.
  return QRectF(54, 10, std::max(10, width() - 54 - 12), std::max(10, height() - 10 - 26));
}

void TimePlot::showEvent(QShowEvent* e)
{
  m_frame.start();
  QWidget::showEvent(e);
}

void TimePlot::hideEvent(QHideEvent* e)
{
  m_frame.stop();
  QWidget::hideEvent(e);
}

void TimePlot::paintEvent(QPaintEvent*)
{
  QPainter p(this);
  p.fillRect(rect(), theme::kPanel);
  const QRectF r = plotRect();
  const double now = viewNow();
  const double x0 = now - m_window;

  // ---- y range
  double lo = m_yMin;
  double hi = m_yMax;
  if (m_autoY) {
    lo = std::numeric_limits<double>::infinity();
    hi = -lo;
    for (const Series& s : m_series) {
      if (!s.visible) {
        continue;
      }
      auto it = std::lower_bound(s.pts.begin(), s.pts.end(), x0,
                                 [](const QPointF& q, double t) {return q.x() < t;});
      for (; it != s.pts.end() && it->x() <= now; ++it) {
        lo = std::min(lo, it->y());
        hi = std::max(hi, it->y());
      }
    }
    if (!std::isfinite(lo)) {
      lo = -1.0;
      hi = 1.0;
    } else if (hi - lo < 1e-9) {
      const double pad = std::max(std::abs(hi) * 0.1, 0.5);
      lo -= pad;
      hi += pad;
    } else {
      const double pad = (hi - lo) * 0.08;
      lo -= pad;
      hi += pad;
    }
  }
  auto mapX = [&](double t) {return r.left() + (t - x0) / m_window * r.width();};
  auto mapY = [&](double v) {return r.bottom() - (v - lo) / (hi - lo) * r.height();};

  QFont small = font();
  small.setPixelSize(11);
  p.setFont(small);
  const QFontMetrics fm(small);

  // ---- grid + labels
  p.setPen(QPen(theme::kGrid, 1));
  const double ys = niceStep(hi - lo, 5);
  for (double v = std::ceil(lo / ys) * ys; v <= hi + 1e-12; v += ys) {
    const double y = mapY(v);
    p.setPen(QPen(std::abs(v) < ys * 1e-6 ? theme::kBorder.lighter(130) : theme::kGrid, 1));
    p.drawLine(QPointF(r.left(), y), QPointF(r.right(), y));
    p.setPen(theme::kMuted);
    p.drawText(QRectF(0, y - 8, r.left() - 6, 16), Qt::AlignRight | Qt::AlignVCenter, fmt(v, ys));
  }
  const double xs = niceStep(m_window, 6);
  for (double rel = -std::floor(m_window / xs) * xs; rel <= 1e-9; rel += xs) {
    const double x = mapX(now + rel);
    p.setPen(QPen(theme::kGrid, 1));
    p.drawLine(QPointF(x, r.top()), QPointF(x, r.bottom()));
    p.setPen(theme::kMuted);
    p.drawText(QRectF(x - 30, r.bottom() + 4, 60, 16), Qt::AlignCenter,
               rel == 0.0 ? QStringLiteral("now") : fmt(rel, xs) + QStringLiteral(" s"));
  }
  p.setPen(QPen(theme::kBorder, 1));
  p.drawRect(r);

  // ---- series
  p.save();
  p.setClipRect(r.adjusted(0, -1, 1, 1));
  p.setRenderHint(QPainter::Antialiasing, true);
  const int cols = std::max(1, static_cast<int>(r.width()));
  for (const Series& s : m_series) {
    if (!s.visible || s.pts.isEmpty()) {
      continue;
    }
    auto it = std::lower_bound(s.pts.begin(), s.pts.end(), x0,
                               [](const QPointF& q, double t) {return q.x() < t;});
    if (it != s.pts.begin()) {
      --it;  // one point before the window, so the line enters from the edge
    }
    auto end = std::upper_bound(it, s.pts.end(), now,
                                [](double t, const QPointF& q) {return t < q.x();});
    if (end != s.pts.end()) {
      ++end;
    }
    const int n = static_cast<int>(end - it);
    QPolygonF poly;
    if (n > cols * 3) {
      // Min/max per pixel column: same look, bounded cost.
      poly.reserve(cols * 2 + 2);
      int col = -1;
      double mn = 0, mx = 0;
      for (auto q = it; q != end; ++q) {
        const int c = static_cast<int>(mapX(q->x()) - r.left());
        if (c != col) {
          if (col >= 0) {
            poly << QPointF(r.left() + col, mapY(mn)) << QPointF(r.left() + col, mapY(mx));
          }
          col = c;
          mn = mx = q->y();
        } else {
          mn = std::min(mn, q->y());
          mx = std::max(mx, q->y());
        }
      }
      if (col >= 0) {
        poly << QPointF(r.left() + col, mapY(mn)) << QPointF(r.left() + col, mapY(mx));
      }
    } else {
      poly.reserve(n);
      for (auto q = it; q != end; ++q) {
        poly << QPointF(mapX(q->x()), mapY(q->y()));
      }
    }
    p.setPen(QPen(s.color, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawPolyline(poly);
  }
  p.restore();

  // ---- legend (top-left, inside)
  m_legend.clear();
  if (!m_series.isEmpty()) {
    int x = static_cast<int>(r.left()) + 8;
    int y = static_cast<int>(r.top()) + 6;
    const int lh = fm.height() + 4;
    QList<QPair<QRect, const Series*>> items;
    for (const Series& s : m_series) {
      const QString last = s.pts.isEmpty() ? QStringLiteral("-") : valueText(s.pts.last().y());
      const QString text = s.label + QStringLiteral("  ") + last;
      const int w = 14 + fm.horizontalAdvance(text) + 14;
      if (x + w > r.right() - 4 && x > r.left() + 8) {
        x = static_cast<int>(r.left()) + 8;
        y += lh;
      }
      items.append({QRect(x, y, w, lh), &s});
      x += w;
    }
    QRect bg;
    for (const auto& it : items) {
      bg = bg.united(it.first);
    }
    p.setPen(Qt::NoPen);
    QColor back = theme::kPanel;
    back.setAlpha(200);
    p.setBrush(back);
    p.drawRoundedRect(bg.adjusted(-4, -2, 0, 2), 4, 4);
    for (const auto& it : items) {
      const Series* s = it.second;
      const QRect box = it.first;
      const QRect sw(box.left(), box.center().y() - 5, 10, 10);
      p.setBrush(s->visible ? s->color : Qt::transparent);
      p.setPen(QPen(s->color, 1.5));
      p.drawRoundedRect(sw, 2, 2);
      const QString last = s->pts.isEmpty() ? QStringLiteral("-") : valueText(s->pts.last().y());
      const QRect tr(sw.right() + 5, box.top(), box.width() - 19, box.height());
      p.setPen(s->visible ? theme::kText : theme::kMuted);
      p.drawText(tr, Qt::AlignLeft | Qt::AlignVCenter, s->label);
      p.setPen(theme::kMuted);
      p.drawText(tr.adjusted(fm.horizontalAdvance(s->label + QStringLiteral("  ")), 0, 0, 0),
                 Qt::AlignLeft | Qt::AlignVCenter, last);
      m_legend.append({s->id, sw.adjusted(-3, -3, 3, 3), tr});
    }
  } else {
    p.setPen(theme::kMuted);
    p.drawText(r, Qt::AlignCenter,
               tr("Add a series: type a path like /motor_cmd.throttle[0],\n"
                  "or drag a field here from the Topics panel."));
  }

  if (m_paused) {
    p.setPen(theme::kWarn);
    p.drawText(r.adjusted(0, 4, -8, 0), Qt::AlignRight | Qt::AlignTop, tr("PAUSED"));
  }

  // ---- hover crosshair
  if (r.contains(m_hover) && !m_series.isEmpty()) {
    const double t = x0 + (m_hover.x() - r.left()) / r.width() * m_window;
    p.setPen(QPen(theme::kMuted, 1, Qt::DashLine));
    p.drawLine(QPointF(m_hover.x(), r.top()), QPointF(m_hover.x(), r.bottom()));
    QStringList lines{QStringLiteral("%1 s").arg(t - now, 0, 'f', 2)};
    QList<QColor> colors{theme::kMuted};
    for (const Series& s : m_series) {
      if (!s.visible || s.pts.isEmpty()) {
        continue;
      }
      auto it = std::upper_bound(s.pts.begin(), s.pts.end(), t,
                                 [](double tt, const QPointF& q) {return tt < q.x();});
      if (it == s.pts.begin()) {
        continue;
      }
      --it;
      lines << QStringLiteral("%1  %2").arg(s.label, valueText(it->y()));
      colors << s.color;
      p.setPen(Qt::NoPen);
      p.setBrush(s.color);
      p.drawEllipse(QPointF(m_hover.x(), mapY(it->y())), 3, 3);
    }
    int w = 0;
    for (const QString& l : lines) {
      w = std::max(w, fm.horizontalAdvance(l));
    }
    const int lh = fm.height() + 2;
    QRect box(m_hover.x() + 12, m_hover.y() + 12, w + 16, lines.size() * lh + 8);
    if (box.right() > r.right()) {
      box.moveRight(m_hover.x() - 12);
    }
    if (box.bottom() > r.bottom()) {
      box.moveBottom(m_hover.y() - 12);
    }
    p.setPen(QPen(theme::kBorder, 1));
    p.setBrush(theme::kRaised);
    p.drawRoundedRect(box, 4, 4);
    for (int i = 0; i < lines.size(); ++i) {
      p.setPen(i == 0 ? theme::kMuted : theme::kText);
      if (i > 0) {
        p.fillRect(QRect(box.left() + 6, box.top() + 4 + i * lh + lh / 2 - 1, 4, 3), colors[i]);
      }
      p.drawText(QRect(box.left() + (i > 0 ? 14 : 8), box.top() + 4 + i * lh, w + 4, lh),
                 Qt::AlignLeft | Qt::AlignVCenter, lines[i]);
    }
  }
}

void TimePlot::mouseMoveEvent(QMouseEvent* e)
{
  m_hover = e->position().toPoint();
  bool overLegend = false;
  for (const LegendHit& h : m_legend) {
    overLegend |= h.swatch.contains(m_hover) || h.text.contains(m_hover);
  }
  setCursor(overLegend ? Qt::PointingHandCursor : Qt::CrossCursor);
  update();
}

void TimePlot::mousePressEvent(QMouseEvent* e)
{
  const QPoint pos = e->position().toPoint();
  for (const LegendHit& h : m_legend) {
    if (e->button() == Qt::RightButton && (h.swatch.contains(pos) || h.text.contains(pos))) {
      emit legendMenuRequested(h.id, e->globalPosition().toPoint());
      return;
    }
    if (e->button() == Qt::LeftButton && h.swatch.contains(pos)) {
      emit colorClicked(h.id);
      return;
    }
    if (e->button() == Qt::LeftButton && h.text.contains(pos)) {
      setSeriesVisible(h.id, !seriesVisible(h.id));
      return;
    }
  }
  QWidget::mousePressEvent(e);
}

void TimePlot::leaveEvent(QEvent* e)
{
  m_hover = QPoint(-1, -1);
  update();
  QWidget::leaveEvent(e);
}

void TimePlot::wheelEvent(QWheelEvent* e)
{
  const double steps = e->angleDelta().y() / 120.0;
  if (steps != 0.0) {
    setWindow(m_window * std::pow(0.85, steps));
  }
  e->accept();
}

}  // namespace magcs
