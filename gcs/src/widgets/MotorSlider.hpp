#pragma once
#include <QWidget>

namespace magcs {

// Slim bipolar throttle slider (-1..1) for one motor.
//  - fill from the centre to the value (blue forward, orange reverse);
//  - the part beyond +-limit is shaded (the GCS clamps there);
//  - a small green tick shows what the drone actually outputs (/motor_cmd);
//  - drag or click to set, wheel for fine steps, double-click to zero,
//    arrow keys +-0.01 (Shift: +-0.1).
class MotorSlider : public QWidget {
  Q_OBJECT
public:
  explicit MotorSlider(const QString& name, QWidget* parent = nullptr);

  double value() const { return m_value; }
  void setValue(double v);          // no valueEdited signal
  void setLimit(double limit);      // 0..1
  void setActual(double v, bool valid);

  QSize sizeHint() const override { return {320, 38}; }
  QSize minimumSizeHint() const override { return {180, 34}; }

signals:
  void valueEdited(double v);

protected:
  void paintEvent(QPaintEvent* e) override;
  void mousePressEvent(QMouseEvent* e) override;
  void mouseMoveEvent(QMouseEvent* e) override;
  void mouseReleaseEvent(QMouseEvent* e) override;
  void mouseDoubleClickEvent(QMouseEvent* e) override;
  void wheelEvent(QWheelEvent* e) override;
  void keyPressEvent(QKeyEvent* e) override;

private:
  QRectF track() const;
  double valueAt(double x) const;
  void edit(double v);

  QString m_name;
  double m_value = 0.0;
  double m_limit = 1.0;
  double m_actual = 0.0;
  bool m_actualValid = false;
  bool m_dragging = false;
};

}  // namespace magcs
