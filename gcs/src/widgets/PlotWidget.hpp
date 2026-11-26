#pragma once
#include <QElapsedTimer>
#include <QList>
#include <QPointF>
#include <QTimer>
#include <QWidget>
#include <array>

#include "core/SafetyState.hpp"

class QChart;
class QChartView;
class QLineSeries;
class QValueAxis;

namespace magcs {

class Drone;

// Live plot of /motor_cmd (what the drone actually sends to the ESCs).
class PlotWidget : public QWidget {
  Q_OBJECT
public:
  explicit PlotWidget(Drone* drone, QWidget* parent = nullptr);

private:
  void onCmd(const MotorCmd& cmd);
  void redraw();

  static constexpr double kWindowS = 30.0;

  QChart* m_chart;
  QValueAxis* m_x;
  QValueAxis* m_y;
  std::array<QLineSeries*, 3> m_series{};
  std::array<QList<QPointF>, 3> m_points;
  QElapsedTimer m_clock;
  QTimer m_redraw;
};

}  // namespace magcs
