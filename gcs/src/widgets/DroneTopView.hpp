#pragma once
#include <QSet>
#include <QWidget>

namespace magcs {

// Top view of the Mini-Acrobat (schematic): triangular frame, the three motors
// numbered at the corners, the body x (forward, red) / y (left, green) axes,
// and the current fly command drawn on top (planar arrow + yaw arc).
// Motor positions are a guess from the CAD render: adjust kMotorAngleDeg.
class DroneTopView : public QWidget {
  Q_OBJECT
public:
  explicit DroneTopView(QWidget* parent = nullptr);

  // Normalised -1..1: x forward, y left, wz counter-clockwise.
  void setCommand(double vx, double vy, double wz);
  void setActive(bool on);              // fly mode enabled
  void setPressed(const QSet<int>& keys);  // Qt::Key_W, ... for the key caps

  QSize sizeHint() const override { return {320, 320}; }
  QSize minimumSizeHint() const override { return {200, 220}; }
  bool hasHeightForWidth() const override { return false; }

protected:
  void paintEvent(QPaintEvent* e) override;

private:
  double m_vx = 0, m_vy = 0, m_wz = 0;
  bool m_active = false;
  QSet<int> m_pressed;
};

}  // namespace magcs
