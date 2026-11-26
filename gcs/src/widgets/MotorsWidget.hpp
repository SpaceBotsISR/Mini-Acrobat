#pragma once
#include <QWidget>
#include <array>

#include "core/SafetyState.hpp"

class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QSlider;

namespace magcs {

class Drone;

class MotorsWidget : public QWidget {
  Q_OBJECT
public:
  explicit MotorsWidget(Drone* drone, QWidget* parent = nullptr);

private:
  void syncFromDrone();
  void refreshEnabled();

  Drone* m_drone;
  QCheckBox* m_enable;
  QDoubleSpinBox* m_limit;
  std::array<QSlider*, 3> m_slider{};
  std::array<QLabel*, 3> m_value{};
  QLabel* m_readout;
};

}  // namespace magcs
