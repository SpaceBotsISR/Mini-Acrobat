#include "panels/FlyPanel.hpp"

#include <QAbstractSpinBox>
#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QTimer>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTextEdit>
#include <QVBoxLayout>
#include <cmath>

#include "core/Drone.hpp"
#include "core/Topics.hpp"
#include "widgets/DroneTopView.hpp"
#include "widgets/Theme.hpp"

namespace magcs {

namespace {

const QSet<int> kFlyKeys{Qt::Key_W, Qt::Key_A, Qt::Key_S, Qt::Key_D, Qt::Key_Q, Qt::Key_E};

bool typingInTextField()
{
  QWidget* w = QApplication::focusWidget();
  if (!w) {
    return false;
  }
  if (auto* cb = qobject_cast<QComboBox*>(w)) {
    return cb->isEditable();
  }
  if (qobject_cast<QAbstractSpinBox*>(w) || qobject_cast<QAbstractSpinBox*>(w->parentWidget())) {
    return false;  // numbers only: W/A/S/D/Q/E mean nothing there, so they drive
  }
  if (auto* pe = qobject_cast<QPlainTextEdit*>(w)) {
    return !pe->isReadOnly();
  }
  if (auto* te = qobject_cast<QTextEdit*>(w)) {
    return !te->isReadOnly();
  }
  return qobject_cast<QLineEdit*>(w) != nullptr;
}

}  // namespace

FlyPanel::FlyPanel(const PanelContext& ctx, QWidget* parent)
: Panel(ctx, parent)
{
  m_view = new DroneTopView;

  m_toggle = new QPushButton(tr("Enable fly mode"));
  m_toggle->setCheckable(true);
  m_toggle->setMinimumHeight(32);
  m_toggle->setToolTip(tr("Only while ARMED. Turns the Motors control output off."));

  m_maxXY = new QDoubleSpinBox;
  m_maxYaw = new QDoubleSpinBox;
  for (QDoubleSpinBox* s : {m_maxXY, m_maxYaw}) {
    s->setRange(0.0, 1.0);
    s->setSingleStep(0.05);
    s->setDecimals(2);
    s->setValue(0.3);
    s->setFixedWidth(70);
  }
  m_status = new QLabel;
  m_status->setWordWrap(true);
  m_status->setStyleSheet(QStringLiteral("color: %1;").arg(theme::css(theme::kMuted)));
  m_status->setText(tr("Publishes %1 (%2, normalised -1..1) at 20 Hz. No drone node uses it yet.")
                      .arg(topics::kFlyCmd, topics::kFlyCmdType));
  m_idleText = m_status->text();
  m_hintTimer.setSingleShot(true);
  connect(&m_hintTimer, &QTimer::timeout, this, [this]() {
    m_status->setText(m_idleText);
    m_status->setStyleSheet(QStringLiteral("color: %1;").arg(theme::css(theme::kMuted)));
  });

  auto* limits = new QHBoxLayout;
  limits->addWidget(new QLabel(tr("max |x|, |y|")));
  limits->addWidget(m_maxXY);
  limits->addSpacing(10);
  limits->addWidget(new QLabel(tr("max |yaw|")));
  limits->addWidget(m_maxYaw);
  limits->addStretch();

  auto* v = new QVBoxLayout(this);
  v->setContentsMargins(8, 8, 8, 8);
  v->addWidget(m_toggle);
  v->addLayout(limits);
  v->addWidget(m_view, 1);
  v->addWidget(m_status);

  connect(m_toggle, &QPushButton::clicked, this, [this](bool on) {
    if (!m_ctx.drone->setFlyEnabled(on)) {
      m_toggle->setChecked(false);
    } else if (on) {
      setFocus(Qt::OtherFocusReason);  // out of any text field, so WASD drive
    }
    refresh();
  });
  // The filter is always on so the key caps light up even with fly mode off
  // (preview only: nothing is published and the keys are not consumed).
  qApp->installEventFilter(this);
  m_filtering = true;
  connect(m_ctx.drone, &Drone::flyEnabledChanged, this, [this](bool) {
    m_keys.clear();
    m_view->setPressed(m_keys);
    refresh();
  });
  connect(m_ctx.drone, &Drone::flyCommandChanged, this, &FlyPanel::refresh);
  connect(m_ctx.drone, &Drone::safetyChanged, this, &FlyPanel::refresh);
  connect(m_ctx.drone, &Drone::connectionChanged, this, &FlyPanel::refresh);
  for (QDoubleSpinBox* s : {m_maxXY, m_maxYaw}) {
    connect(s, &QDoubleSpinBox::valueChanged, this, &FlyPanel::applyKeys);
  }
  setFocusPolicy(Qt::StrongFocus);
  refresh();
}

FlyPanel::~FlyPanel()
{
  if (m_filtering) {
    qApp->removeEventFilter(this);
  }
  m_ctx.drone->setFlyEnabled(false);
}

void FlyPanel::refresh()
{
  const bool on = m_ctx.drone->flyEnabled();
  const bool canArm = m_ctx.drone->isConnected() && m_ctx.drone->safety().armed();
  m_toggle->setChecked(on);
  m_toggle->setEnabled(on || canArm);
  m_toggle->setText(on ? tr("Fly mode ON  (WASD · Q/E)") : canArm ? tr("Enable fly mode")
                                                                   : tr("Enable fly mode (needs ARMED)"));
  m_toggle->setStyleSheet(on ? QStringLiteral("QPushButton { background: %1; border-color: %1; color: white; "
                                              "font-weight: 600; }").arg(theme::css(theme::kAccent))
                             : QString());
  const Drone::FlyCmd c = m_ctx.drone->flyCommand();
  m_view->setActive(on);
  m_view->setCommand(c.vx, c.vy, c.wz);
  m_view->setPressed(m_keys);
}

void FlyPanel::showHint(const QString& text)
{
  m_status->setText(text);
  m_status->setStyleSheet(QStringLiteral("color: %1;").arg(theme::css(theme::kWarn)));
  m_hintTimer.start(3000);
}

void FlyPanel::applyKeys()
{
  auto k = [this](int key) {return m_keys.contains(key) ? 1.0 : 0.0;};
  double x = k(Qt::Key_W) - k(Qt::Key_S);
  double y = k(Qt::Key_A) - k(Qt::Key_D);
  const double n = std::hypot(x, y);
  if (n > 1.0) {
    x /= n;  // diagonal: same speed as straight
    y /= n;
  }
  const double wz = k(Qt::Key_Q) - k(Qt::Key_E);
  m_ctx.drone->setFlyCommand(x * m_maxXY->value(), y * m_maxXY->value(), wz * m_maxYaw->value());
  m_view->setPressed(m_keys);
}

void FlyPanel::releaseAll()
{
  if (!m_keys.isEmpty()) {
    m_keys.clear();
    applyKeys();
  }
}

bool FlyPanel::eventFilter(QObject* o, QEvent* e)
{
  switch (e->type()) {
    case QEvent::KeyPress:
    case QEvent::KeyRelease: {
      auto* ke = static_cast<QKeyEvent*>(e);
      if (!kFlyKeys.contains(ke->key()) ||
          (ke->modifiers() & (Qt::ControlModifier | Qt::MetaModifier | Qt::AltModifier))) {
        break;
      }
      if (typingInTextField()) {
        if (e->type() == QEvent::KeyPress && window()->isActiveWindow()) {
          showHint(tr("W A S D Q E are going to a text field. Press Esc or click this panel to drive."));
        }
        break;
      }
      const bool flying = m_ctx.drone->flyEnabled();
      // One physical key press reaches this filter several times (first the
      // QWindow, then the focus widget and its parents). Act on the first
      // sighting only; when flying, consume it right there.
      const KeySeen seen{e->type(), ke->key(), ke->timestamp(), ke->isAutoRepeat()};
      if (seen == m_lastKey) {
        return flying;
      }
      m_lastKey = seen;
      Q_UNUSED(o);
      if (!flying) {
        // Preview: light the key caps only.
        if (!ke->isAutoRepeat()) {
          if (e->type() == QEvent::KeyPress) {
            m_keys.insert(ke->key());
          } else {
            m_keys.remove(ke->key());
          }
          m_view->setPressed(m_keys);
        }
        break;
      }
      if (!ke->isAutoRepeat()) {
        if (e->type() == QEvent::KeyPress) {
          m_keys.insert(ke->key());
        } else {
          m_keys.remove(ke->key());
        }
        applyKeys();
      }
      return true;  // WASD are ours while fly mode is on
    }
    case QEvent::ApplicationDeactivate:
    case QEvent::WindowDeactivate:
      if (!m_ctx.drone->flyEnabled()) {
        m_keys.clear();
        m_view->setPressed(m_keys);
        break;
      }
      releaseAll();  // never keep a key "held" when focus goes elsewhere
      break;
    default:
      break;
  }
  return Panel::eventFilter(o, e);
}

QJsonObject FlyPanel::saveConfig() const
{
  return {{"maxXY", m_maxXY->value()}, {"maxYaw", m_maxYaw->value()}};
}

void FlyPanel::loadConfig(const QJsonObject& cfg)
{
  m_maxXY->setValue(cfg.value("maxXY").toDouble(0.3));
  m_maxYaw->setValue(cfg.value("maxYaw").toDouble(0.3));
}

}  // namespace magcs
