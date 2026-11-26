#pragma once
#include <QString>

namespace magcs {

// How MAgcs reaches the drone's rosbridge. Every mode ends in the same
// ws://host:port connection; the mode decides defaults, help text and which
// diagnostics are shown (Tailscale path, ...).
struct ConnectionTarget {
  enum class Mode { Tailscale, Manual, Simulator };

  Mode mode = Mode::Tailscale;
  QString host;          // ignored in Simulator mode (always localhost)
  quint16 port = 9090;

  QString effectiveHost() const;
  QString url() const;   // ws://host:port

  static QString modeName(Mode m);   // for the UI
  static QString modeKey(Mode m);    // for QSettings / command line
  static bool modeFromKey(const QString& key, Mode* out);
  static QString defaultHost(Mode m);

  // QSettings ("connection/..."). The host is remembered per mode.
  static ConnectionTarget load();
  static QString savedHost(Mode m);
  void save() const;
};

}  // namespace magcs
