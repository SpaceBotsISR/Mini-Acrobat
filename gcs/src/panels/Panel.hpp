#pragma once
// Base of the Foxglove-style panels that can be added to a layout (Plot, Raw
// messages, Gauge, Publish, Topics, Fly mode). Each one saves its own settings
// as JSON, stored with the layout.
#include <QJsonObject>
#include <QMimeData>
#include <QString>
#include <QWidget>

namespace magcs {

class Drone;
class NetworkMonitor;
class TopicHub;

struct PanelContext {
  Drone* drone = nullptr;
  NetworkMonitor* network = nullptr;
  TopicHub* hub = nullptr;
};

// Drag & drop of a message path ("/topic.field[0]") between panels.
inline const QString kPathMime = QStringLiteral("application/x-magcs-path");

inline QString pathFromMime(const QMimeData* m)
{
  if (m->hasFormat(kPathMime)) {
    return QString::fromUtf8(m->data(kPathMime));
  }
  const QString t = m->text().trimmed();
  return t.startsWith(QLatin1Char('/')) ? t : QString();
}

class Panel : public QWidget {
  Q_OBJECT
public:
  explicit Panel(const PanelContext& ctx, QWidget* parent = nullptr) : QWidget(parent), m_ctx(ctx) {}

  virtual QString type() const = 0;
  virtual QJsonObject saveConfig() const { return {}; }
  virtual void loadConfig(const QJsonObject&) {}

signals:
  void activated(magcs::Panel* self);  // clicked / focused (e.g. "the current plot")

protected:
  PanelContext m_ctx;
};

}  // namespace magcs
