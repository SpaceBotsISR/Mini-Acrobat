#include "panels/PublishPanel.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include "core/Drone.hpp"
#include "core/Topics.hpp"
#include "core/TopicHub.hpp"
#include "widgets/Theme.hpp"

namespace magcs {

namespace {

bool isReserved(const QString& topic)
{
  return topic == topics::kHeartbeat || topic == topics::kMotorSetpoint || topic == topics::kFlyCmd;
}

QString normType(QString t)
{
  return t.replace(QStringLiteral("/msg/"), QStringLiteral("/"));
}

QJsonValue primitiveDefault(const QString& t)
{
  if (t == QLatin1String("bool")) {
    return false;
  }
  if (t == QLatin1String("string") || t == QLatin1String("wstring")) {
    return QString();
  }
  static const QStringList numbers{"float32", "float64", "double", "float", "int8", "uint8", "int16",
                                   "uint16", "int32", "uint32", "int64", "uint64", "byte", "char",
                                   "octet"};
  if (numbers.contains(t)) {
    return 0;
  }
  return QJsonValue(QJsonValue::Undefined);
}

QJsonObject build(const QJsonArray& defs, const QString& type, int depth)
{
  QJsonObject out;
  if (depth > 10) {
    return out;
  }
  for (const QJsonValue& dv : defs) {
    const QJsonObject d = dv.toObject();
    if (normType(d.value("type").toString()) != normType(type)) {
      continue;
    }
    const QJsonArray names = d.value("fieldnames").toArray();
    const QJsonArray types = d.value("fieldtypes").toArray();
    const QJsonArray lens = d.value("fieldarraylen").toArray();
    for (int i = 0; i < names.size(); ++i) {
      const QString ft = types.at(i).toString();
      const int len = lens.at(i).toInt(-1);
      QJsonValue one = primitiveDefault(ft);
      if (one.isUndefined()) {
        one = build(defs, ft, depth + 1);
      }
      if (len < 0) {
        out.insert(names.at(i).toString(), one);
      } else {
        QJsonArray a;
        for (int k = 0; k < len; ++k) {
          a.append(one);
        }
        out.insert(names.at(i).toString(), a);  // len 0: dynamic array, empty
      }
    }
    break;
  }
  return out;
}

}  // namespace

QJsonObject PublishPanel::defaultMessage(const QJsonValue& typedefs, const QString& type)
{
  const QJsonArray defs = typedefs.toArray();
  QString root = type;
  if (!defs.isEmpty()) {
    root = defs.first().toObject().value("type").toString(type);
  }
  return build(defs, root, 0);
}

PublishPanel::PublishPanel(const PanelContext& ctx, QWidget* parent)
: Panel(ctx, parent)
{
  m_topic = new QComboBox;
  m_topic->setEditable(true);
  m_topic->setInsertPolicy(QComboBox::NoInsert);
  m_topic->lineEdit()->setPlaceholderText(tr("/topic"));
  m_type = new QLineEdit;
  m_type->setPlaceholderText(tr("pkg/msg/Type"));
  auto* tmpl = new QPushButton(tr("Template"));
  tmpl->setToolTip(tr("Fill the message with the type's fields (rosapi)"));

  m_json = new QPlainTextEdit;
  m_json->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
  m_json->setPlaceholderText(QStringLiteral("{\n  \"data\": 1.0\n}"));
  m_json->setTabChangesFocus(false);

  m_publish = new QPushButton(tr("Publish"));
  m_publish->setStyleSheet(QStringLiteral("QPushButton { background: %1; border-color: %1; color: white; }"
                                          "QPushButton:disabled { background: %2; border-color: %2; color: %3; }")
                             .arg(theme::css(theme::kAccent), theme::css(theme::kPanel),
                                  theme::css(theme::kMuted)));
  m_repeat = new QCheckBox(tr("Repeat at"));
  m_rate = new QDoubleSpinBox;
  m_rate->setRange(0.1, 50.0);
  m_rate->setValue(1.0);
  m_rate->setSuffix(tr(" Hz"));
  m_rate->setDecimals(1);
  m_status = new QLabel;
  m_status->setWordWrap(true);
  m_status->setStyleSheet(QStringLiteral("color: %1;").arg(theme::css(theme::kMuted)));

  auto* top = new QHBoxLayout;
  top->addWidget(m_topic, 3);
  top->addWidget(m_type, 2);
  top->addWidget(tmpl);
  auto* bottom = new QHBoxLayout;
  bottom->addWidget(m_status, 1);
  bottom->addWidget(m_repeat);
  bottom->addWidget(m_rate);
  bottom->addWidget(m_publish);
  auto* v = new QVBoxLayout(this);
  v->setContentsMargins(6, 6, 6, 6);
  v->addLayout(top);
  v->addWidget(m_json, 1);
  v->addLayout(bottom);

  connect(m_topic, &QComboBox::activated, this, &PublishPanel::onTopicChosen);
  connect(m_topic->lineEdit(), &QLineEdit::editingFinished, this, &PublishPanel::onTopicChosen);
  connect(tmpl, &QPushButton::clicked, this, &PublishPanel::loadTemplate);
  connect(m_publish, &QPushButton::clicked, this, [this]() {
    if (m_repeat->isChecked()) {
      if (m_timer.isActive()) {
        m_timer.stop();
        m_publish->setText(tr("Start"));
      } else if (publishOnce()) {
        m_timer.start(static_cast<int>(1000.0 / m_rate->value()));
        m_publish->setText(tr("Stop"));
      }
    } else {
      publishOnce();
    }
  });
  connect(m_repeat, &QCheckBox::toggled, this, [this](bool on) {
    m_timer.stop();
    m_publish->setText(on ? tr("Start") : tr("Publish"));
  });
  connect(m_rate, &QDoubleSpinBox::valueChanged, this, [this](double hz) {
    if (m_timer.isActive()) {
      m_timer.setInterval(static_cast<int>(1000.0 / hz));
    }
  });
  connect(&m_timer, &QTimer::timeout, this, [this]() {
    if (!publishOnce()) {
      m_timer.stop();
      m_publish->setText(tr("Start"));
    }
  });
  connect(m_ctx.drone, &Drone::connectionChanged, this, [this](bool on) {
    m_publish->setEnabled(on);
    if (!on && m_timer.isActive()) {
      m_timer.stop();
      m_publish->setText(tr("Start"));
    }
  });
  connect(m_ctx.hub, &TopicHub::topicsChanged, this, &PublishPanel::fillTopics);
  m_publish->setEnabled(m_ctx.drone->isConnected());
  fillTopics();
}

void PublishPanel::setStatus(const QString& html)
{
  m_status->setText(html);
}

void PublishPanel::fillTopics()
{
  const QString cur = m_topic->currentText();
  QSignalBlocker b(m_topic);
  m_topic->clear();
  for (const TopicHub::Topic& t : m_ctx.hub->topics()) {
    if (!isReserved(t.name)) {
      m_topic->addItem(t.name, t.type);
    }
  }
  m_topic->setEditText(cur);
}

void PublishPanel::onTopicChosen()
{
  const QString t = m_ctx.hub->typeOf(m_topic->currentText().trimmed());
  if (!t.isEmpty()) {
    m_type->setText(t);
  }
}

void PublishPanel::loadTemplate()
{
  const QString type = m_type->text().trimmed();
  if (type.isEmpty()) {
    setStatus(tr("Set the message type first."));
    return;
  }
  setStatus(tr("Loading %1 ...").arg(type));
  m_ctx.hub->messageDetails(type, [this, type](bool ok, const QJsonValue& defs, const QString& err) {
    if (!ok) {
      setStatus(tr("<span style='color:%1'>No layout for %2: %3</span>")
                  .arg(theme::css(theme::kWarn), type.toHtmlEscaped(), err.toHtmlEscaped()));
      return;
    }
    const QJsonObject msg = defaultMessage(defs, type);
    m_json->setPlainText(QString::fromUtf8(QJsonDocument(msg).toJson(QJsonDocument::Indented)));
    setStatus(tr("Template for %1.").arg(type));
  });
}

bool PublishPanel::publishOnce()
{
  const QString topic = m_topic->currentText().trimmed();
  const QString type = m_type->text().trimmed();
  if (!topic.startsWith(QLatin1Char('/'))) {
    setStatus(tr("<span style='color:%1'>Topic must start with '/'.</span>").arg(theme::css(theme::kBad)));
    return false;
  }
  if (isReserved(topic)) {
    setStatus(tr("<span style='color:%1'>%2 is driven by MAgcs itself: use the Safety / Motors / Fly "
                 "mode panels.</span>").arg(theme::css(theme::kBad), topic.toHtmlEscaped()));
    return false;
  }
  if (type.isEmpty()) {
    setStatus(tr("<span style='color:%1'>Set the message type.</span>").arg(theme::css(theme::kBad)));
    return false;
  }
  QJsonParseError err;
  const QString text = m_json->toPlainText().trimmed();
  const QJsonDocument doc = QJsonDocument::fromJson((text.isEmpty() ? QStringLiteral("{}") : text).toUtf8(), &err);
  if (err.error != QJsonParseError::NoError || !doc.isObject()) {
    setStatus(tr("<span style='color:%1'>Invalid JSON: %2</span>")
                .arg(theme::css(theme::kBad),
                     err.error != QJsonParseError::NoError ? err.errorString() : tr("not an object")));
    return false;
  }
  RosbridgeClient* c = m_ctx.drone->client();
  if (!c->isAdvertised(topic)) {
    c->advertise(topic, type);
  }
  if (!c->publish(topic, doc.object())) {
    setStatus(tr("<span style='color:%1'>Not connected.</span>").arg(theme::css(theme::kBad)));
    return false;
  }
  ++m_sent;
  setStatus(tr("Published %1 message(s) on %2").arg(m_sent).arg(topic.toHtmlEscaped()));
  return true;
}

QJsonObject PublishPanel::saveConfig() const
{
  return {{"topic", m_topic->currentText()},
          {"type", m_type->text()},
          {"message", m_json->toPlainText()},
          {"rateHz", m_rate->value()}};
}

void PublishPanel::loadConfig(const QJsonObject& cfg)
{
  m_topic->setEditText(cfg.value("topic").toString());
  m_type->setText(cfg.value("type").toString());
  m_json->setPlainText(cfg.value("message").toString());
  m_rate->setValue(cfg.value("rateHz").toDouble(1.0));
}

}  // namespace magcs
