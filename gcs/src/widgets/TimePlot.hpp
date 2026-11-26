#pragma once
#include <QColor>
#include <QList>
#include <QPointF>
#include <QString>
#include <QTimer>
#include <QVector>
#include <QWidget>
#include <functional>

namespace magcs {

// Fast, dark, multi-series time plot (QPainter, no QtCharts).
//  - x: seconds relative to "now" (window of N s), y: auto-scaled or fixed;
//  - legend inside the plot: click a swatch to change the colour, click the
//    name to hide/show, right-click for a menu (signals, the owner decides);
//  - hover: crosshair with every series' value at that time;
//  - wheel: zoom the time window; Pause freezes the view (data keeps coming).
// Repaints at most ~30 fps and only while visible, so the GUI thread (which
// also sends the safety heartbeat) stays light.
class TimePlot : public QWidget {
  Q_OBJECT
public:
  explicit TimePlot(QWidget* parent = nullptr);

  // Clock shared with the data source (seconds, monotonic).
  void setClock(std::function<double()> now) { m_now = std::move(now); }

  int addSeries(const QString& label, const QColor& color);  // returns id
  void removeSeries(int id);
  void clearSeries();
  void append(int id, double t, double v);
  void clearData();

  void setColor(int id, const QColor& c);
  QColor color(int id) const;
  void setSeriesVisible(int id, bool on);
  bool seriesVisible(int id) const;
  QString label(int id) const;
  QList<int> seriesIds() const;

  void setWindow(double seconds);
  double window() const { return m_window; }
  void setAutoY(bool on);
  bool autoY() const { return m_autoY; }
  void setYRange(double lo, double hi);
  double yMin() const { return m_yMin; }
  double yMax() const { return m_yMax; }
  void setPaused(bool on);
  bool paused() const { return m_paused; }

  QSize sizeHint() const override { return {480, 260}; }
  QSize minimumSizeHint() const override { return {200, 120}; }

signals:
  void colorClicked(int id);
  void legendMenuRequested(int id, const QPoint& globalPos);
  void windowChanged(double seconds);

protected:
  void paintEvent(QPaintEvent* e) override;
  void mouseMoveEvent(QMouseEvent* e) override;
  void mousePressEvent(QMouseEvent* e) override;
  void leaveEvent(QEvent* e) override;
  void wheelEvent(QWheelEvent* e) override;
  void showEvent(QShowEvent* e) override;
  void hideEvent(QHideEvent* e) override;

private:
  struct Series {
    int id = 0;
    QString label;
    QColor color;
    bool visible = true;
    QVector<QPointF> pts;  // (t, v), t increasing
  };
  struct LegendHit {
    int id;
    QRect swatch;
    QRect text;
  };

  Series* find(int id);
  const Series* find(int id) const;
  double viewNow() const;
  void prune(Series& s, double now);
  QRectF plotRect() const;

  QList<Series> m_series;
  int m_nextId = 1;
  std::function<double()> m_now;
  double m_window = 30.0;
  bool m_autoY = true;
  double m_yMin = -1.0;
  double m_yMax = 1.0;
  bool m_paused = false;
  double m_pausedAt = 0.0;
  QPoint m_hover{-1, -1};
  QList<LegendHit> m_legend;
  QTimer m_frame;
};

}  // namespace magcs
