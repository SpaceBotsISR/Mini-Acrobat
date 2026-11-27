#include "widgets/MotorsWidget.hpp"

#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>

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
  m_limit->setRange(0.0, kSafeLimit);
  m_limit->setSingleStep(0.05);
  m_limit->setDecimals(2);
  m_limit->setFixedWidth(64);
  m_limit->setToolTip(tr("Throttle limit (locked at ±%1 max). Unlock with the padlock.").arg(kSafeLimit));
  m_limit->setValue(std::clamp(QSettings().value("motors/limit", kSafeLimit).toDouble(), 0.0, kSafeLimit));
  m_drone->setThrottleLimit(m_limit->value());

  m_lock = new QToolButton;
  m_lock->setCheckable(true);
  m_lock->setText(QStringLiteral("🔒"));
  m_lock->setToolTip(tr("Throttle limit locked (max ±%1). Click to unlock.").arg(kSafeLimit));

  m_banner = new QLabel;
  m_banner->setWordWrap(true);
  m_banner->setStyleSheet(QStringLiteral("QLabel { background: %1; color: white; font-weight: 600; "
                                         "border-radius: 4px; padding: 4px 8px; }")
                            .arg(theme::css(theme::kBad)));
  m_banner->hide();

  auto* zero = new QPushButton(tr("Zero all"));
  auto* top = new QHBoxLayout;
  top->addWidget(m_enable, 1);
  top->addSpacing(6);
  top->addWidget(new QLabel(tr("limit ±")));
  top->addWidget(m_limit);
  top->addWidget(m_lock);
  top->addWidget(zero);

  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(10, 8, 10, 8);
  root->setSpacing(2);
  root->addLayout(top);
  root->addSpacing(4);
  root->addWidget(m_banner);
  root->addSpacing(2);
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
  connect(m_limit, &QDoubleSpinBox::valueChanged, this, [this](double v) {applyLimit(v);});
  connect(m_lock, &QToolButton::clicked, this, [this](bool wantUnlocked) {
    if (!wantUnlocked) {
      setUnlocked(false);
      return;
    }
    QMessageBox box(QMessageBox::Warning, tr("⚠ WARNING — unlock throttle limit"),
                    tr("<b>⚠ WARNING!</b><br><br>You are about to remove the ±%1 safety limit on the "
                       "motor control output. The motors can then be driven up to full throttle "
                       "(±1.0).<br><br>Make sure the drone is secured, props are clear and nobody is "
                       "near it.<br><br>Unlock the throttle limit?").arg(kSafeLimit),
                    QMessageBox::Cancel, this);
    QPushButton* yes = box.addButton(tr("Unlock"), QMessageBox::DestructiveRole);
    box.setDefaultButton(QMessageBox::Cancel);
    box.exec();
    setUnlocked(box.clickedButton() == yes);
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

void MotorsWidget::applyLimit(double v)
{
  m_drone->setThrottleLimit(v);
  if (!m_switching) {
    QSettings().setValue(m_unlocked ? "motors/limit_unlocked" : "motors/limit", v);
  }
  for (MotorSlider* s : m_slider) {
    s->setLimit(v);
  }
  if (m_unlocked) {
    m_banner->setText(tr("⚠ WARNING: throttle limit UNLOCKED — output limit ±%1 (safe max ±%2)")
                        .arg(v, 0, 'f', 2).arg(kSafeLimit, 0, 'f', 2));
  }
}

void MotorsWidget::setUnlocked(bool on)
{
  m_unlocked = on;
  m_switching = true;  // range/value changes below are not user edits: don't save them
  QSettings st;
  if (on) {
    m_limit->setRange(0.0, 1.0);
    m_limit->setValue(std::clamp(st.value("motors/limit_unlocked", kSafeLimit).toDouble(), 0.0, 1.0));
    emit m_drone->log(tr("throttle limit UNLOCKED (±%1)").arg(m_limit->value(), 0, 'f', 2));
  } else {
    m_limit->setRange(0.0, kSafeLimit);  // clamps immediately if above
    m_limit->setValue(std::clamp(st.value("motors/limit", kSafeLimit).toDouble(), 0.0, kSafeLimit));
    emit m_drone->log(tr("throttle limit locked (±%1)").arg(m_limit->value(), 0, 'f', 2));
  }
  m_switching = false;
  applyLimit(m_limit->value());  // also covers "value didn't change" (no valueChanged)

  m_lock->setChecked(on);
  m_lock->setText(on ? QStringLiteral("🔓") : QStringLiteral("🔒"));
  m_lock->setToolTip(on ? tr("Throttle limit UNLOCKED. Click to lock back to ±%1 max.").arg(kSafeLimit)
                        : tr("Throttle limit locked (max ±%1). Click to unlock.").arg(kSafeLimit));
  m_limit->setStyleSheet(on ? QStringLiteral("QDoubleSpinBox { border: 2px solid %1; color: %1; font-weight: 600; }")
                                .arg(theme::css(theme::kBad))
                            : QString());
  m_banner->setVisible(on);
}

}  // namespace magcs
