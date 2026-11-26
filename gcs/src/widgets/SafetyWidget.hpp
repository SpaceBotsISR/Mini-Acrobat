#pragma once
#include <QWidget>

#include "core/SafetyState.hpp"

class QLabel;
class QPushButton;

namespace magcs {

class Drone;

class SafetyWidget : public QWidget {
  Q_OBJECT
public:
  explicit SafetyWidget(Drone* drone, QWidget* parent = nullptr);

private:
  void refresh();

  Drone* m_drone;
  QLabel* m_state;
  QLabel* m_reason;
  QLabel* m_link;
  QLabel* m_age;
  QLabel* m_maxGap;
  QLabel* m_seq;
  QLabel* m_result;
  QPushButton* m_arm;
  QPushButton* m_disarm;
};

}  // namespace magcs
