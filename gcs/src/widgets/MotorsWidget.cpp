#include "widgets/MotorsWidget.hpp"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QVBoxLayout>

#include "core/Drone.hpp"

namespace magcs {

namespace {
constexpr int kScale = 1000;  // slider units per 1.0 of throttle
}

MotorsWidget::MotorsWidget(Drone* drone, QWidget* parent)
: QWidget(parent), m_drone(drone)
{
  m_enable = new QCheckBox(tr("Control output enabled (20 Hz)"));
  m_enable->setToolTip(tr("Only possible while ARMED, with all throttles at zero."));

  m_limit = new QDoubleSpinBox;
  m_limit->setRange(0.0, 1.0);
  m_limit->setSingleStep(0.05);
  m_limit->setDecimals(2);
  m_limit->setValue(QSettings().value("motors/limit", 0.25).toDouble());
  m_drone->setThrottleLimit(m_limit->value());

  auto* limitRow = new QHBoxLayout;
  limitRow->addWidget(new QLabel(tr("Limit |throttle| <=")));
  limitRow->addWidget(m_limit);
  limitRow->addStretch();
  auto* zero = new QPushButton(tr("Zero all"));
  limitRow->addWidget(zero);

  auto* grid = new QGridLayout;
  for (int i = 0; i < 3; ++i) {
    m_slider[i] = new QSlider(Qt::Horizontal);
    m_slider[i]->setRange(-kScale, kScale);
    m_slider[i]->setTickPosition(QSlider::TicksBelow);
    m_slider[i]->setTickInterval(kScale / 2);
    m_value[i] = new QLabel("+0.000");
    m_value[i]->setMinimumWidth(56);
    m_value[i]->setStyleSheet("font-family: monospace;");
    grid->addWidget(new QLabel(tr("Motor %1").arg(i + 1)), i, 0);
    grid->addWidget(m_slider[i], i, 1);
    grid->addWidget(m_value[i], i, 2);
    connect(m_slider[i], &QSlider::valueChanged, this, [this, i](int v) {
      m_drone->setThrottle(i, static_cast<double>(v) / kScale);
    });
  }

  m_readout = new QLabel;
  m_readout->setStyleSheet("font-family: monospace; color: gray;");

  auto* root = new QVBoxLayout(this);
  root->addWidget(m_enable);
  root->addLayout(limitRow);
  root->addLayout(grid);
  root->addWidget(new QLabel(tr("Output from the drone (/motor_cmd):")));
  root->addWidget(m_readout);
  root->addStretch();

  connect(zero, &QPushButton::clicked, m_drone, &Drone::zeroThrottle);
  connect(m_limit, &QDoubleSpinBox::valueChanged, this, [this](double v) {
    m_drone->setThrottleLimit(v);
    QSettings().setValue("motors/limit", v);
  });
  connect(m_enable, &QCheckBox::toggled, this, [this](bool on) {
    if (!m_drone->setControlEnabled(on)) {
      QSignalBlocker b(m_enable);
      m_enable->setChecked(false);
    }
  });
  connect(m_drone, &Drone::controlEnabledChanged, this, [this](bool on) {
    QSignalBlocker b(m_enable);
    m_enable->setChecked(on);
  });
  connect(m_drone, &Drone::throttleChanged, this, [this]() {syncFromDrone();});
  connect(m_drone, &Drone::safetyChanged, this, [this]() {refreshEnabled();});
  connect(m_drone, &Drone::connectionChanged, this, [this]() {refreshEnabled();});
  connect(m_drone, &Drone::motorCmdReceived, this, [this](const MotorCmd& c) {
    m_readout->setText(QString::asprintf("%s  %+.3f  %+.3f  %+.3f",
                                         c.armed ? "ARMED   " : "disarmed",
                                         c.throttle[0], c.throttle[1], c.throttle[2]));
  });
  refreshEnabled();
  syncFromDrone();
}

void MotorsWidget::syncFromDrone()
{
  const auto t = m_drone->throttle();
  for (int i = 0; i < 3; ++i) {
    QSignalBlocker b(m_slider[i]);
    m_slider[i]->setValue(static_cast<int>(t[i] * kScale));
    m_value[i]->setText(QString::asprintf("%+.3f", t[i]));
  }
}

void MotorsWidget::refreshEnabled()
{
  m_enable->setEnabled(m_drone->isConnected() && m_drone->safety().armed());
}

}  // namespace magcs
