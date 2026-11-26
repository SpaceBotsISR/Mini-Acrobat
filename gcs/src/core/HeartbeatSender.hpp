#pragma once
#include <QObject>
#include <QTimer>

namespace magcs {

class RosbridgeClient;

// Publishes /gcs/heartbeat at ~20 Hz while the client is connected.
//
// It runs on the GUI thread on purpose: docs/safety.md requires the heartbeat to
// stop when the UI/control loop stops. If the GUI thread blocks, no heartbeat is
// sent and the drone disarms after heartbeat_timeout_ms. The price is that a
// long GUI stall (> timeout) also disarms: keep the GUI thread light.
class HeartbeatSender : public QObject {
  Q_OBJECT
public:
  explicit HeartbeatSender(RosbridgeClient* client, QObject* parent = nullptr);

  void setIntervalMs(int ms) { m_timer.setInterval(ms); }
  // Test hook / "simulate link loss": stops sending without disconnecting.
  void setPaused(bool paused) { m_paused = paused; }
  bool paused() const { return m_paused; }
  quint32 sentCount() const { return m_seq; }

signals:
  void sent(quint32 seq);

private:
  void tick();

  RosbridgeClient* m_client;
  QTimer m_timer;
  quint32 m_seq = 0;
  bool m_paused = false;
};

}  // namespace magcs
