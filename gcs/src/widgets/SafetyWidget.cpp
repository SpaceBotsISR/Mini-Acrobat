#include "widgets/SafetyWidget.hpp"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include "core/Drone.hpp"
#include "widgets/Theme.hpp"

namespace magcs {

SafetyWidget::SafetyWidget(Drone* drone, QWidget* parent)
: QWidget(parent), m_drone(drone)
{
  m_state = new QLabel;
  m_state->setAlignment(Qt::AlignCenter);
  QFont f = m_state->font();
  f.setPointSize(f.pointSize() + 8);
  f.setBold(true);
  m_state->setFont(f);
  m_state->setMinimumHeight(48);

  m_reason = new QLabel;
  m_link = new QLabel;
  m_age = new QLabel;
  m_maxGap = new QLabel;
  m_seq = new QLabel;
  m_result = new QLabel;
  m_result->setWordWrap(true);

  m_arm = new QPushButton(tr("ARM   ⇧Space"));
  m_disarm = new QPushButton(tr("DISARM   Space"));
  m_arm->setToolTip(tr("Arm the drone (Shift+Space)"));
  m_disarm->setToolTip(tr("Disarm now (Space, works from anywhere in the app)"));
  for (QPushButton* b : {m_arm, m_disarm}) {
    b->setMinimumHeight(42);
    b->setCursor(Qt::PointingHandCursor);
  }

  auto* form = new QFormLayout;
  form->addRow(tr("Reason"), m_reason);
  form->addRow(tr("Link OK"), m_link);
  form->addRow(tr("Heartbeat age"), m_age);
  form->addRow(tr("Max gap (1 s)"), m_maxGap);
  form->addRow(tr("Last seq"), m_seq);

  auto* buttons = new QHBoxLayout;
  buttons->addWidget(m_arm);
  buttons->addWidget(m_disarm);

  auto* root = new QVBoxLayout(this);
  root->addWidget(m_state);
  root->addLayout(form);
  root->addLayout(buttons);
  root->addWidget(m_result);
  root->addStretch();

  connect(m_arm, &QPushButton::clicked, m_drone, &Drone::arm);
  connect(m_disarm, &QPushButton::clicked, m_drone, &Drone::disarm);
  connect(m_drone, &Drone::safetyChanged, this, [this]() {refresh();});
  connect(m_drone, &Drone::connectionChanged, this, [this]() {refresh();});
  connect(m_drone, &Drone::armResult, this, [this](bool ok, const QString& msg) {
    m_result->setText(tr("arm: %1").arg(msg));
    m_result->setStyleSheet(ok ? "color: #3dd68c;" : "color: #e85d5d; font-weight: bold;");
  });
  connect(m_drone, &Drone::disarmResult, this, [this](bool ok, const QString& msg) {
    m_result->setText(tr("disarm: %1").arg(msg));
    m_result->setStyleSheet(ok ? "color: #3dd68c;" : "color: #e85d5d; font-weight: bold;");
  });
  refresh();
}

void SafetyWidget::refresh()
{
  const SafetyState& s = m_drone->safety();
  const bool connected = m_drone->isConnected();

  // Banner: what the drone IS. ARMED = danger (red), DISARMED = safe (green).
  QString style = "border-radius: 8px; color: white; background: %1;";
  if (!s.valid) {
    m_state->setStyleSheet(style.arg(theme::css(theme::kRaised)) + "color: " + theme::css(theme::kMuted) + ";");
  } else if (s.armed()) {
    m_state->setStyleSheet(style.arg(theme::css(theme::kBad)));
  } else {
    m_state->setStyleSheet(style.arg(s.reason == SafetyState::kReasonLinkLost ? theme::css(theme::kWarn)
                                                                              : theme::css(theme::kOk)));
  }
  m_state->setText(s.stateText());
  m_reason->setText(s.reasonText());
  m_link->setText(!s.valid ? "-" : (s.linkOk ? tr("yes") : tr("NO")));
  m_age->setText(s.heartbeatAgeMs >= 0 ? tr("%1 ms").arg(s.heartbeatAgeMs, 0, 'f', 0) : "-");
  m_maxGap->setText(s.valid ? tr("%1 ms").arg(s.heartbeatMaxGapMs, 0, 'f', 0) : "-");
  m_seq->setText(s.valid ? QString::number(s.lastHeartbeatSeq) : "-");

  const bool canArm = connected && s.valid && !s.armed();
  const bool canDisarm = connected && s.armed();
  m_arm->setEnabled(canArm);
  m_disarm->setEnabled(connected);  // always available while connected (panic button)
  // Colour = "this button would change the state now": ARM red, DISARM green.
  const QString lit = QStringLiteral(
    "QPushButton { background: %1; border: 1px solid %1; color: white; font-weight: 700;"
    " border-radius: 6px; } QPushButton:hover { background: %2; }");
  const QString idle = QStringLiteral(
    "QPushButton { font-weight: 600; border-radius: 6px; }");
  m_arm->setStyleSheet(canArm ? lit.arg(theme::css(theme::kBad), theme::css(theme::kBad.lighter(115))) : idle);
  m_disarm->setStyleSheet(canDisarm ? lit.arg(theme::css(theme::kOk), theme::css(theme::kOk.lighter(115)))
                                    : idle);
}

}  // namespace magcs
