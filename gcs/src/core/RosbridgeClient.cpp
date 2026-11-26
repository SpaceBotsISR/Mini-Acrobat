#include "core/RosbridgeClient.hpp"

#include "core/RosbridgeProtocol.hpp"

namespace magcs {

RosbridgeClient::RosbridgeClient(QObject* parent)
: QObject(parent)
{
  m_reconnectTimer.setSingleShot(true);
  m_reconnectTimer.setInterval(1000);
  connect(&m_reconnectTimer, &QTimer::timeout, this, [this]() {
    if (m_wantOpen && m_state == State::Disconnected) {
      setState(State::Connecting);
      m_socket.open(m_url);
    }
  });

  m_pingTimer.setInterval(1000);
  connect(&m_pingTimer, &QTimer::timeout, this, [this]() {
    if (isConnected()) {
      m_socket.ping();
    }
  });

  connect(&m_socket, &QWebSocket::connected, this, &RosbridgeClient::onConnected);
  connect(&m_socket, &QWebSocket::disconnected, this, &RosbridgeClient::onDisconnected);
  connect(&m_socket, &QWebSocket::textMessageReceived, this, &RosbridgeClient::onText);
  connect(&m_socket, &QWebSocket::pong, this,
          [this](quint64 elapsedMs, const QByteArray&) {emit rttMeasured(static_cast<int>(elapsedMs));});
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
  connect(&m_socket, &QWebSocket::errorOccurred, this,
#else
  connect(&m_socket, QOverload<QAbstractSocket::SocketError>::of(&QWebSocket::error), this,
#endif
          [this](QAbstractSocket::SocketError) {
            emit logMessage(QStringLiteral("socket error: %1").arg(m_socket.errorString()));
            if (m_state != State::Connected) {
              setState(State::Disconnected);
              scheduleReconnect();
            }
          });
}

RosbridgeClient::~RosbridgeClient()
{
  // The socket member emits disconnected() while it is destroyed, after the
  // members declared below it are already gone: cut the connections first.
  m_wantOpen = false;
  QObject::disconnect(&m_socket, nullptr, nullptr, nullptr);
}

void RosbridgeClient::open(const QUrl& url)
{
  m_url = url;
  m_wantOpen = true;
  m_reconnectTimer.stop();
  if (m_socket.state() != QAbstractSocket::UnconnectedState) {
    m_socket.abort();
  }
  setState(State::Connecting);
  emit logMessage(QStringLiteral("connecting to %1").arg(url.toString()));
  m_socket.open(url);
}

void RosbridgeClient::close()
{
  m_wantOpen = false;
  m_reconnectTimer.stop();
  if (m_socket.state() == QAbstractSocket::ConnectedState) {
    m_socket.close();  // flushes frames already queued (e.g. a last disarm)
  } else {
    m_socket.abort();
    setState(State::Disconnected);
  }
}

void RosbridgeClient::advertise(const QString& topic, const QString& type)
{
  m_advertised.insert(topic, type);
  if (isConnected()) {
    send(rosbridge::advertise(topic, type));
  }
}

void RosbridgeClient::subscribe(const QString& topic, const QString& type, int throttleMs)
{
  m_subscribed.insert(topic, {type, throttleMs});
  if (isConnected()) {
    send(rosbridge::subscribe(topic, type, throttleMs));
  }
}

bool RosbridgeClient::publish(const QString& topic, const QJsonObject& msg)
{
  if (!isConnected()) {
    return false;
  }
  send(rosbridge::publish(topic, msg));
  return true;
}

void RosbridgeClient::callService(const QString& service, const QJsonObject& args,
                                  ServiceCallback cb, int timeoutMs)
{
  if (!isConnected()) {
    if (cb) {
      cb(false, QJsonValue(), QStringLiteral("not connected"));
    }
    return;
  }
  const QString id = QStringLiteral("magcs-%1").arg(m_nextId++);
  Pending p;
  p.cb = std::move(cb);
  p.timer = new QTimer(this);
  p.timer->setSingleShot(true);
  connect(p.timer, &QTimer::timeout, this, [this, id]() {
    finishCall(id, false, QJsonValue(), QStringLiteral("timeout"));
  });
  p.timer->start(timeoutMs);
  m_pending.insert(id, std::move(p));
  send(rosbridge::callService(service, id, args));
}

void RosbridgeClient::setState(State s)
{
  if (s == m_state) {
    return;
  }
  m_state = s;
  if (s == State::Connected) {
    m_pingTimer.start();
  } else {
    m_pingTimer.stop();
  }
  emit stateChanged(s);
}

void RosbridgeClient::scheduleReconnect()
{
  if (m_wantOpen && m_autoReconnect && !m_reconnectTimer.isActive()) {
    m_reconnectTimer.start();
  }
}

void RosbridgeClient::onConnected()
{
  emit logMessage(QStringLiteral("connected"));
  // Set the state first so that publish()/send() work, then (re)register topics.
  setState(State::Connected);
  for (auto it = m_advertised.cbegin(); it != m_advertised.cend(); ++it) {
    send(rosbridge::advertise(it.key(), it.value()));
  }
  for (auto it = m_subscribed.cbegin(); it != m_subscribed.cend(); ++it) {
    send(rosbridge::subscribe(it.key(), it.value().first, it.value().second));
  }
}

void RosbridgeClient::onDisconnected()
{
  emit logMessage(QStringLiteral("disconnected"));
  failPending(QStringLiteral("disconnected"));
  setState(State::Disconnected);
  scheduleReconnect();
}

void RosbridgeClient::onText(const QString& text)
{
  ++m_rx;
  const rosbridge::Incoming in = rosbridge::parse(text);
  switch (in.kind) {
    case rosbridge::Incoming::Kind::Publish:
      emit topicMessage(in.topic, in.msg);
      break;
    case rosbridge::Incoming::Kind::ServiceResponse:
      if (in.result) {
        finishCall(in.id, true, in.values, QString());
      } else {
        finishCall(in.id, false, in.values, in.values.toString(QStringLiteral("service call failed")));
      }
      break;
    case rosbridge::Incoming::Kind::Status:
      emit logMessage(QStringLiteral("rosbridge %1: %2").arg(in.level, in.text));
      break;
    case rosbridge::Incoming::Kind::Unknown:
      break;
  }
}

void RosbridgeClient::send(const QString& text)
{
  ++m_tx;
  m_socket.sendTextMessage(text);
}

void RosbridgeClient::failPending(const QString& why)
{
  const QStringList ids = m_pending.keys();
  for (const QString& id : ids) {
    finishCall(id, false, QJsonValue(), why);
  }
}

void RosbridgeClient::finishCall(const QString& id, bool ok, const QJsonValue& values,
                                 const QString& error)
{
  auto it = m_pending.find(id);
  if (it == m_pending.end()) {
    return;
  }
  Pending p = std::move(it.value());
  m_pending.erase(it);
  p.timer->stop();
  p.timer->deleteLater();
  if (p.cb) {
    p.cb(ok, values, error);
  }
}

}  // namespace magcs
