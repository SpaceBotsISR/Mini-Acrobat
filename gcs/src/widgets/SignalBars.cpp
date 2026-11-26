#include "widgets/SignalBars.hpp"

#include <QPainter>

#include "platform/WifiInfo.hpp"

namespace magcs {

SignalBars::SignalBars(QWidget* parent)
: QWidget(parent)
{
  setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

void SignalBars::setRssi(int rssiDbm)
{
  if (rssiDbm == m_rssi) {
    return;
  }
  m_rssi = rssiDbm;
  setToolTip(rssiDbm < 0 ? QStringLiteral("%1 dBm (%2)").arg(rssiDbm).arg(rssiQuality(rssiDbm))
                         : QStringLiteral("no signal data"));
  update();
}

void SignalBars::paintEvent(QPaintEvent*)
{
  const int bars = rssiToBars(m_rssi);
  QColor on = bars >= 3 ? QColor("#1a7f37") : bars == 2 ? QColor("#b08800") : QColor("#c0392b");
  const QColor off = palette().color(QPalette::Mid);

  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  p.setPen(Qt::NoPen);
  const int n = 4;
  const qreal gap = 2.0;
  const qreal w = (width() - gap * (n - 1)) / n;
  for (int i = 0; i < n; ++i) {
    const qreal h = height() * (i + 1) / n;
    p.setBrush(i < bars ? on : off);
    p.drawRoundedRect(QRectF(i * (w + gap), height() - h, w, h), 1.5, 1.5);
  }
}

}  // namespace magcs
