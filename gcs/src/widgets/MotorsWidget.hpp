#pragma once
#include <QWidget>
#include <array>

#include "core/SafetyState.hpp"

class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QToolButton;

namespace magcs {

class Drone;
class MotorSlider;

// Manual per-motor throttle (/control/motor_setpoint) with the drone's actual
// output (/motor_cmd) shown on the same sliders.
class MotorsWidget : public QWidget {
  Q_OBJECT
public:
  explicit MotorsWidget(Drone* drone, QWidget* parent = nullptr);

  // Locked: the limit spinbox can only go up to kSafeLimit. Unlocking (after a
  // warning dialog) allows up to 1.0. The app always starts locked; the locked
  // and unlocked limit values are saved separately (motors/limit,
  // motors/limit_unlocked) so each mode comes back with its own value.
  static constexpr double kSafeLimit = 0.25;

private:
  void syncFromDrone();
  void refreshEnabled();
  void setUnlocked(bool on);
  void applyLimit(double v);

  Drone* m_drone;
  QPushButton* m_enable;
  QDoubleSpinBox* m_limit;
  QToolButton* m_lock;
  QLabel* m_banner;
  bool m_unlocked = false;
  bool m_switching = false;
  std::array<MotorSlider*, 3> m_slider{};
  QLabel* m_readout;
};

}  // namespace magcs
