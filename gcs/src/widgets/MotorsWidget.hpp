#pragma once
#include <QWidget>
#include <array>

#include "core/SafetyState.hpp"

class QDoubleSpinBox;
class QLabel;
class QPushButton;

namespace magcs {

class Drone;
class MotorSlider;

// Manual per-motor throttle (/control/motor_setpoint) with the drone's actual
// output (/motor_cmd) shown on the same sliders.
class MotorsWidget : public QWidget {
  Q_OBJECT
public:
  explicit MotorsWidget(Drone* drone, QWidget* parent = nullptr);

private:
  void syncFromDrone();
  void refreshEnabled();

  Drone* m_drone;
  QPushButton* m_enable;
  QDoubleSpinBox* m_limit;
  std::array<MotorSlider*, 3> m_slider{};
  QLabel* m_readout;
};

}  // namespace magcs
