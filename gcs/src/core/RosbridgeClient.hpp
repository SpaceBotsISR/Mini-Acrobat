#pragma once
#include <QHash>
#include <QHostAddress>
#include <QJsonObject>
#include <QJsonValue>
#include <QMap>
#include <QObject>
#include <QTimer>
#include <QUrl>
#include <QWebSocket>
#include <functional>

namespace magcs {

// WebSocket client for rosbridge_server (protocol v2).
//  - re-sends advertise/subscribe after every reconnect
//  - optional auto-reconnect
//  - pings once per second and reports the round-trip time
//  - service calls with timeout
class RosbridgeClient : public QObject {
  Q_OBJECT
public:
  enum class State { Disconnected, Connecting, Connected };
  Q_ENUM(State)

  // ok == false: the call failed (`error` says why). values is the service response.
  using ServiceCallback =
    std::function<void(bool ok, const QJsonValue& values, const QString& error)>;

  explicit RosbridgeClient(QObject* parent = nullptr);
  ~RosbridgeClient() override;

  void open(const QUrl& url);
  void close();
  State state() const { return m_state; }
  bool isConnected() const { return m_state == State::Connected; }
  QUrl url() const { return m_url; }
  void setAutoReconnect(bool on) { m_autoReconnect = on; }

  // Registered topics survive reconnects.
  void advertise(const QString& topic, const QString& type);
  void subscribe(const QString& topic, const QString& type, int throttleMs = 0);

  // Returns false (and drops the message) when not connected.
  bool publish(const QString& topic, const QJsonObject& msg);
  void callService(const QString& service, const QJsonObject& args, ServiceCallback cb,
                   int timeoutMs = 2000);

  // Addresses of the open socket: which drone IP the host resolved to, and
  // which local IP (i.e. interface: Wi-Fi, Tailscale, ...) the Mac uses.
  QHostAddress peerAddress() const { return m_socket.peerAddress(); }
  QHostAddress localAddress() const { return m_socket.localAddress(); }

  quint64 messagesReceived() const { return m_rx; }
  quint64 messagesSent() const { return m_tx; }

signals:
  void stateChanged(magcs::RosbridgeClient::State state);
  void topicMessage(const QString& topic, const QJsonObject& msg);
  void rttMeasured(int ms);
  void logMessage(const QString& text);

private:
  struct Pending {
    ServiceCallback cb;
    QTimer* timer = nullptr;
  };

  void setState(State s);
  void scheduleReconnect();
  void onConnected();
  void onDisconnected();
  void onText(const QString& text);
  void send(const QString& text);
  void failPending(const QString& why);
  void finishCall(const QString& id, bool ok, const QJsonValue& values, const QString& error);

  QWebSocket m_socket;
  QTimer m_reconnectTimer;
  QTimer m_pingTimer;
  QUrl m_url;
  State m_state = State::Disconnected;
  bool m_wantOpen = false;
  bool m_autoReconnect = true;
  QMap<QString, QString> m_advertised;                // topic -> type
  QMap<QString, QPair<QString, int>> m_subscribed;    // topic -> (type, throttle ms)
  QHash<QString, Pending> m_pending;
  quint64 m_nextId = 1;
  quint64 m_rx = 0;
  quint64 m_tx = 0;
};

}  // namespace magcs
