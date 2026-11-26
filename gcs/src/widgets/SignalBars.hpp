#pragma once
#include <QWidget>

namespace magcs {

// Four signal bars from an RSSI in dBm (0 = unknown: all grey).
class SignalBars : public QWidget {
  Q_OBJECT
public:
  explicit SignalBars(QWidget* parent = nullptr);
  void setRssi(int rssiDbm);
  QSize sizeHint() const override { return {30, 16}; }

protected:
  void paintEvent(QPaintEvent* event) override;

private:
  int m_rssi = 0;
};

}  // namespace magcs
