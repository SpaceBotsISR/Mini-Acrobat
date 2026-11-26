#include "widgets/PlotWidget.hpp"

#include <QChart>
#include <QChartView>
#include <QLineSeries>
#include <QPainter>
#include <QValueAxis>
#include <QVBoxLayout>

#include "core/Drone.hpp"

namespace magcs {

PlotWidget::PlotWidget(Drone* drone, QWidget* parent)
: QWidget(parent)
{
  m_chart = new QChart;
  m_chart->setTitle(tr("Motor commands (/motor_cmd)"));
  m_chart->legend()->setAlignment(Qt::AlignBottom);

  m_x = new QValueAxis;
  m_x->setTitleText(tr("time [s] (0 = now)"));
  m_x->setLabelFormat("%.0f");
  m_x->setRange(-kWindowS, 0);
  m_x->setTickCount(7);  // every 5 s
  m_y = new QValueAxis;
  m_y->setRange(-1.0, 1.0);
  m_y->setTickCount(9);  // every 0.25
  m_y->setLabelFormat("%.2f");
  m_y->setTitleText(tr("throttle"));
  m_chart->addAxis(m_x, Qt::AlignBottom);
  m_chart->addAxis(m_y, Qt::AlignLeft);

  for (int i = 0; i < 3; ++i) {
    m_series[i] = new QLineSeries;
    m_series[i]->setName(tr("Motor %1").arg(i + 1));
    m_chart->addSeries(m_series[i]);
    m_series[i]->attachAxis(m_x);
    m_series[i]->attachAxis(m_y);
  }

  auto* view = new QChartView(m_chart);
  view->setRenderHint(QPainter::Antialiasing);
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(view);

  m_clock.start();
  connect(drone, &Drone::motorCmdReceived, this, &PlotWidget::onCmd);
  connect(&m_redraw, &QTimer::timeout, this, &PlotWidget::redraw);
  m_redraw.start(100);  // 10 fps is plenty and keeps the GUI thread free for the heartbeat
}

void PlotWidget::onCmd(const MotorCmd& cmd)
{
  const double t = m_clock.elapsed() / 1000.0;
  for (int i = 0; i < 3; ++i) {
    m_points[i].append(QPointF(t, cmd.throttle[i]));
  }
}

void PlotWidget::redraw()
{
  const double now = m_clock.elapsed() / 1000.0;
  for (int i = 0; i < 3; ++i) {
    auto& pts = m_points[i];
    while (!pts.isEmpty() && pts.first().x() < now - kWindowS) {
      pts.removeFirst();
    }
    QList<QPointF> rel;  // x relative to "now", so the axis is fixed at [-30, 0]
    rel.reserve(pts.size());
    for (const QPointF& p : pts) {
      rel.append(QPointF(p.x() - now, p.y()));
    }
    m_series[i]->replace(rel);
  }
}

}  // namespace magcs
