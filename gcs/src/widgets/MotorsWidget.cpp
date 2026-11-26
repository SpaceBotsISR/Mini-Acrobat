#include "widgets/MotorsWidget.hpp"

#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

#include "core/Drone.hpp"
#include "widgets/MotorSlider.hpp"
#include "widgets/Theme.hpp"

namespace magcs {

MotorsWidget::MotorsWidget(Drone* drone, QWidget* parent)
: QWidget(parent), m_drone(drone)
{
  m_enable = new QPushButton;
  m_enable->setCheckable(true);
  m_enable->setMinimumHeight(30);
  m_enable->setToolTip(tr("Publish the sliders at 20 Hz. Only while ARMED, with all throttles at "
                          "zero. Turns fly mode off."));

  m_limit = new QDoubleSpinBox;
  m_limit->setRange(0.0, 1.0);
  m_limit->setSingleStep(0.05);
  m_limit->setDecimals(2);
  m_limit->setFixedWidth(64);
  m_limit->setValue(QSettings().value("motors/limit", 0.25).toDouble());
  m_drone->setThrottleLimit(m_limit->value());

  auto* zero = new QPushButton(tr("Zero all"));
  auto* top = new QHBoxLayout;
  top->addWidget(m_enable, 1);
  top->addSpacing(6);
  top->addWidget(new QLabel(tr("limit ±")));
  top->addWidget(m_limit);
  top->addWidget(zero);

  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(10, 8, 10, 8);
  root->setSpacing(2);
  root->addLayout(top);
  root->addSpacing(6);
  for (int i = 0; i < 3; ++i) {
    m_slider[i] = new MotorSlider(tr("Motor %1").arg(i + 1));
    root->addWidget(m_slider[i]);
    connect(m_slider[i], &MotorSlider::valueEdited, this, [this, i](double v) {m_drone->setThrottle(i, v);});
  }
  m_readout = new QLabel;
  m_readout->setStyleSheet(QStringLiteral("color: %1; font-size: 11px;").arg(theme::css(theme::kMuted)));
  root->addSpacing(4);
  root->addWidget(m_readout);
  root->addStretch();

  connect(zero, &QPushButton::clicked, m_drone, &Drone::zeroThrottle);
  connect(m_limit, &QDoubleSpinBox::valueChanged, this, [this](double v) {
    m_drone->setThrottleLimit(v);
    QSettings().setValue("motors/limit", v);
    for (MotorSlider* s : m_slider) {
      s->setLimit(v);
    }
  });
  connect(m_enable, &QPushButton::clicked, this, [this](bool on) {
    if (!m_drone->setControlEnabled(on)) {
      m_enable->setChecked(false);
    }
    refreshEnabled();
  });
  connect(m_drone, &Drone::controlEnabledChanged, this, [this]() {refreshEnabled();});
  connect(m_drone, &Drone::throttleChanged, this, [this]() {syncFromDrone();});
  connect(m_drone, &Drone::safetyChanged, this, [this]() {refreshEnabled();});
  connect(m_drone, &Drone::connectionChanged, this, [this]() {refreshEnabled();});
  connect(m_drone, &Drone::motorCmdReceived, this, [this](const MotorCmd& c) {
    const bool valid = m_drone->isConnected() && m_drone->safety().valid;
    for (int i = 0; i < 3; ++i) {
      m_slider[i]->setActual(c.throttle[i], valid);
    }
    m_readout->setText(valid ? tr("▲ drone output (/motor_cmd): %1   %2  %3  %4")
                                 .arg(c.armed ? tr("ARMED") : tr("disarmed"))
                                 .arg(c.throttle[0], 0, 'f', 3)
                                 .arg(c.throttle[1], 0, 'f', 3)
                                 .arg(c.throttle[2], 0, 'f', 3)
                             : tr("▲ drone output (/motor_cmd): -"));
  });
  for (MotorSlider* s : m_slider) {
    s->setLimit(m_limit->value());
  }
  refreshEnabled();
  syncFromDrone();
  m_readout->setText(tr("▲ drone output (/motor_cmd): -"));
}

void MotorsWidget::syncFromDrone()
{
  const auto t = m_drone->throttle();
  for (int i = 0; i < 3; ++i) {
    m_slider[i]->setValue(t[i]);
  }
}

void MotorsWidget::refreshEnabled()
{
  const bool on = m_drone->controlEnabled();
  const bool possible = m_drone->isConnected() && m_drone->safety().armed();
  m_enable->setChecked(on);
  m_enable->setEnabled(on || possible);
  m_enable->setText(on ? tr("Control output ON · 20 Hz")
                       : possible ? tr("Enable control output") : tr("Control output (needs ARMED)"));
  m_enable->setStyleSheet(on ? QStringLiteral("QPushButton { background: %1; border-color: %1; color: white; "
                                              "font-weight: 600; }").arg(theme::css(theme::kAccent))
                             : QString());
}

}  // namespace magcs
