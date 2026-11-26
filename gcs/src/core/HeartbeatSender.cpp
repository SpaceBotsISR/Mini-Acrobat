#include "core/HeartbeatSender.hpp"

#include "core/RosbridgeClient.hpp"
#include "core/Topics.hpp"

namespace magcs {

HeartbeatSender::HeartbeatSender(RosbridgeClient* client, QObject* parent)
: QObject(parent), m_client(client)
{
  m_client->advertise(topics::kHeartbeat, topics::kHeartbeatType);
  m_timer.setTimerType(Qt::PreciseTimer);
  m_timer.setInterval(50);  // 20 Hz
  connect(&m_timer, &QTimer::timeout, this, &HeartbeatSender::tick);
  m_timer.start();
}

void HeartbeatSender::tick()
{
  if (m_paused || !m_client->isConnected()) {
    return;
  }
  const quint32 seq = m_seq + 1;
  if (m_client->publish(topics::kHeartbeat, {{"seq", static_cast<qint64>(seq)}})) {
    m_seq = seq;
    emit sent(seq);
  }
}

}  // namespace magcs
