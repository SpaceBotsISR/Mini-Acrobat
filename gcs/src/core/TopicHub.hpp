#pragma once
#include <QElapsedTimer>
#include <QHash>
#include <QJsonValue>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <functional>

namespace magcs {

class RosbridgeClient;

// Everything the panels need about the drone's topics, on top of RosbridgeClient:
//  - the topic list (name + type) from rosapi (/rosapi/topics), refreshed every
//    5 s while connected;
//  - shared, reference-counted subscriptions: several panels can watch the same
//    topic, it is unsubscribed when the last one lets go (topics the Drone class
//    subscribed itself are never unsubscribed);
//  - the last message, its arrival time and the rate (Hz) of every topic that
//    receives messages;
//  - message layouts (/rosapi/message_details), cached, for publish templates.
// All times are seconds on one monotonic clock (now()).
class TopicHub : public QObject {
  Q_OBJECT
public:
  struct Topic {
    QString name;
    QString type;
  };

  explicit TopicHub(RosbridgeClient* client, QObject* parent = nullptr);

  double now() const { return m_clock.nsecsElapsed() / 1e9; }

  // ---- topic list
  const QList<Topic>& topics() const { return m_topics; }
  QString typeOf(const QString& topic) const;
  bool rosapiAvailable() const { return m_rosapiOk; }
  QString rosapiError() const { return m_rosapiError; }
  void refreshTopics();

  // Large messages (images, point clouds...) are not decoded by panels.
  static bool isHeavyType(const QString& type);

  // ---- subscriptions
  void acquire(const QString& topic);
  void release(const QString& topic);

  // ---- data
  bool hasMessage(const QString& topic) const { return m_last.contains(topic); }
  QJsonObject lastMessage(const QString& topic) const { return m_last.value(topic).msg; }
  double lastTime(const QString& topic) const { return m_last.value(topic).t; }  // -1: none
  double rateHz(const QString& topic) const { return m_rate.value(topic); }
  // Numeric field paths seen in the topic's last message ("linear.x", ...).
  QStringList numericFields(const QString& topic) const;

  // ---- message layout (rosapi/message_details), cached per type
  using DetailsCallback = std::function<void(bool ok, const QJsonValue& typedefs, const QString& error)>;
  void messageDetails(const QString& type, DetailsCallback cb);

signals:
  void topicsChanged();
  // Every message of every subscribed topic. t = now() at arrival.
  void message(const QString& topic, const QJsonObject& msg, double t);
  void ratesUpdated();  // ~1 Hz

private:
  void onMessage(const QString& topic, const QJsonObject& msg);
  void onConnection(bool connected);
  void updateRates();

  struct Last {
    QJsonObject msg;
    double t = -1.0;
  };

  RosbridgeClient* m_client;
  QElapsedTimer m_clock;
  QTimer m_refreshTimer;
  QTimer m_rateTimer;
  QList<Topic> m_topics;
  bool m_rosapiOk = false;
  QString m_rosapiError;
  bool m_refreshing = false;

  QHash<QString, int> m_refs;       // our subscriptions -> users
  QSet<QString> m_ownSubs;          // topics WE subscribed (others are the Drone's)
  QHash<QString, Last> m_last;
  QHash<QString, int> m_counts;     // messages since the last rate update
  QHash<QString, double> m_rate;
  double m_lastRateT = 0.0;
  QHash<QString, QJsonValue> m_details;
};

}  // namespace magcs
