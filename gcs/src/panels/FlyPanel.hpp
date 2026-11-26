#pragma once
#include <QSet>

#include "panels/Panel.hpp"

class QDoubleSpinBox;
class QLabel;
class QPushButton;

namespace magcs {

class DroneTopView;

// Fly / test mode: WASD (x/y) and Q/E (yaw) drive Drone::setFlyCommand while
// fly mode is on (Drone publishes /control/cmd at 20 Hz, only while ARMED).
// Keys are read application-wide while fly mode is on, except while typing in a
// text field; releasing all keys, or the app losing focus, sends zero.
// Config: {maxXY, maxYaw}.
class FlyPanel : public Panel {
  Q_OBJECT
public:
  explicit FlyPanel(const PanelContext& ctx, QWidget* parent = nullptr);
  ~FlyPanel() override;

  QString type() const override { return QStringLiteral("fly"); }
  QJsonObject saveConfig() const override;
  void loadConfig(const QJsonObject& cfg) override;

protected:
  bool eventFilter(QObject* o, QEvent* e) override;

private:
  void refresh();
  void applyKeys();
  void releaseAll();

  DroneTopView* m_view;
  QPushButton* m_toggle;
  QDoubleSpinBox* m_maxXY;
  QDoubleSpinBox* m_maxYaw;
  QLabel* m_status;
  QSet<int> m_keys;
  bool m_filtering = false;
};

}  // namespace magcs
