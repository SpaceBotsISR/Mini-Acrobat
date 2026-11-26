#include "core/TopicHub.hpp"

#include <QJsonArray>
#include <algorithm>

#include "core/MessagePath.hpp"
#include "core/RosbridgeClient.hpp"
#include "core/Topics.hpp"

namespace magcs {

TopicHub::TopicHub(RosbridgeClient* client, QObject* parent)
: QObject(parent), m_client(client)
{
  m_clock.start();
  connect(m_client, &RosbridgeClient::topicMessage, this, &TopicHub::onMessage);
  connect(m_client, &RosbridgeClient::stateChanged, this, [this](RosbridgeClient::State s) {
    onConnection(s == RosbridgeClient::State::Connected);
  });

  m_refreshTimer.setInterval(5000);
  connect(&m_refreshTimer, &QTimer::timeout, this, &TopicHub::refreshTopics);
  m_rateTimer.setInterval(1000);
  connect(&m_rateTimer, &QTimer::timeout, this, &TopicHub::updateRates);
  m_rateTimer.start();
  m_lastRateT = now();
}

QString TopicHub::typeOf(const QString& topic) const
{
  for (const Topic& t : m_topics) {
    if (t.name == topic) {
      return t.type;
    }
  }
  return {};
}

bool TopicHub::isHeavyType(const QString& type)
{
  static const QStringList heavy{
    QStringLiteral("sensor_msgs/msg/Image"), QStringLiteral("sensor_msgs/msg/CompressedImage"),
    QStringLiteral("sensor_msgs/msg/PointCloud2"), QStringLiteral("nav_msgs/msg/OccupancyGrid"),
    QStringLiteral("sensor_msgs/msg/LaserScan"),  QStringLiteral("theora_image_transport/msg/Packet"),
  };
  return heavy.contains(type);
}

void TopicHub::refreshTopics()
{
  if (!m_client->isConnected() || m_refreshing) {
    return;
  }
  m_refreshing = true;
  m_client->callService(topics::kRosapiTopics, {}, [this](bool ok, const QJsonValue& v, const QString& err) {
    m_refreshing = false;
    if (!ok) {
      const bool changed = m_rosapiOk || m_rosapiError != err;
      m_rosapiOk = false;
      m_rosapiError = err;
      if (changed) {
        emit topicsChanged();
      }
      return;
    }
    const QJsonObject o = v.toObject();
    const QJsonArray names = o.value(QStringLiteral("topics")).toArray();
    const QJsonArray types = o.value(QStringLiteral("types")).toArray();
    QList<Topic> list;
    for (int i = 0; i < names.size(); ++i) {
      list.append({names.at(i).toString(), i < types.size() ? types.at(i).toString() : QString()});
    }
    std::sort(list.begin(), list.end(), [](const Topic& a, const Topic& b) {return a.name < b.name;});
    bool changed = !m_rosapiOk || list.size() != m_topics.size();
    for (int i = 0; !changed && i < list.size(); ++i) {
      changed = list[i].name != m_topics[i].name || list[i].type != m_topics[i].type;
    }
    m_rosapiOk = true;
    m_rosapiError.clear();
    m_topics = list;
    if (changed) {
      emit topicsChanged();
    }
  }, 3000);
}

void TopicHub::acquire(const QString& topic)
{
  if (topic.isEmpty()) {
    return;
  }
  int& n = m_refs[topic];
  if (n++ == 0 && !m_client->isSubscribed(topic)) {
    m_ownSubs.insert(topic);
    m_client->subscribe(topic, typeOf(topic));
  }
}

void TopicHub::release(const QString& topic)
{
  auto it = m_refs.find(topic);
  if (it == m_refs.end()) {
    return;
  }
  if (--it.value() <= 0) {
    m_refs.erase(it);
    if (m_ownSubs.remove(topic)) {
      m_client->unsubscribe(topic);
    }
  }
}

QStringList TopicHub::numericFields(const QString& topic) const
{
  QStringList out;
  const auto it = m_last.constFind(topic);
  if (it == m_last.constEnd()) {
    return out;
  }
  for (const auto& leaf : msgpath::numericLeaves(it->msg)) {
    out << leaf.first;
  }
  return out;
}

void TopicHub::messageDetails(const QString& type, DetailsCallback cb)
{
  if (type.isEmpty()) {
    cb(false, {}, QStringLiteral("unknown type"));
    return;
  }
  if (m_details.contains(type)) {
    cb(true, m_details.value(type), QString());
    return;
  }
  m_client->callService(topics::kRosapiMessageDetails, {{"type", type}},
                        [this, type, cb](bool ok, const QJsonValue& v, const QString& err) {
    if (!ok) {
      cb(false, {}, err);
      return;
    }
    const QJsonValue defs = v.toObject().value(QStringLiteral("typedefs"));
    m_details.insert(type, defs);
    cb(true, defs, QString());
  }, 3000);
}

void TopicHub::onMessage(const QString& topic, const QJsonObject& msg)
{
  const double t = now();
  Last& l = m_last[topic];
  l.msg = msg;
  l.t = t;
  ++m_counts[topic];
  emit message(topic, msg, t);
}

void TopicHub::onConnection(bool connected)
{
  if (connected) {
    refreshTopics();
    m_refreshTimer.start();
  } else {
    m_refreshTimer.stop();
    m_last.clear();
    m_counts.clear();
    m_rate.clear();
    emit ratesUpdated();
  }
}

void TopicHub::updateRates()
{
  const double t = now();
  const double dt = qMax(1e-3, t - m_lastRateT);
  m_lastRateT = t;
  QHash<QString, double> rates;
  for (auto it = m_last.constBegin(); it != m_last.constEnd(); ++it) {
    const double inst = m_counts.value(it.key()) / dt;
    const double prev = m_rate.value(it.key(), inst);
    // Light smoothing so the number does not jump every second.
    rates.insert(it.key(), inst == 0.0 ? 0.0 : 0.5 * prev + 0.5 * inst);
  }
  m_rate = rates;
  m_counts.clear();
  emit ratesUpdated();
}

}  // namespace magcs
