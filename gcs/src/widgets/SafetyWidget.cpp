#include "widgets/SafetyWidget.hpp"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include "core/Drone.hpp"

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

  m_arm = new QPushButton(tr("ARM"));
  m_disarm = new QPushButton(tr("DISARM  (Space)"));
  m_disarm->setMinimumHeight(40);
  m_disarm->setStyleSheet("QPushButton { background: #c0392b; color: white; font-weight: bold; }"
                          "QPushButton:disabled { background: #888; }");
  m_arm->setMinimumHeight(40);

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
    m_result->setStyleSheet(ok ? "color: #1a7f37;" : "color: #c0392b; font-weight: bold;");
  });
  connect(m_drone, &Drone::disarmResult, this, [this](bool ok, const QString& msg) {
    m_result->setText(tr("disarm: %1").arg(msg));
    m_result->setStyleSheet(ok ? "color: #1a7f37;" : "color: #c0392b; font-weight: bold;");
  });
  refresh();
}

void SafetyWidget::refresh()
{
  const SafetyState& s = m_drone->safety();
  const bool connected = m_drone->isConnected();

  QString style = "border-radius: 6px; color: white; background: %1;";
  if (!s.valid) {
    m_state->setStyleSheet(style.arg("#888"));
  } else if (s.armed()) {
    m_state->setStyleSheet(style.arg("#1a7f37"));
  } else {
    m_state->setStyleSheet(style.arg(s.reason == SafetyState::kReasonLinkLost ? "#c0392b" : "#555"));
  }
  m_state->setText(s.stateText());
  m_reason->setText(s.reasonText());
  m_link->setText(!s.valid ? "-" : (s.linkOk ? tr("yes") : tr("NO")));
  m_age->setText(s.heartbeatAgeMs >= 0 ? tr("%1 ms").arg(s.heartbeatAgeMs, 0, 'f', 0) : "-");
  m_maxGap->setText(s.valid ? tr("%1 ms").arg(s.heartbeatMaxGapMs, 0, 'f', 0) : "-");
  m_seq->setText(s.valid ? QString::number(s.lastHeartbeatSeq) : "-");

  m_arm->setEnabled(connected && s.valid && !s.armed());
  m_disarm->setEnabled(connected);  // always available while connected
}

}  // namespace magcs
