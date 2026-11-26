#include <QApplication>
#include <QCommandLineParser>
#include <QIcon>
#include <QStyleFactory>

#include "MainWindow.hpp"
#include "widgets/ConnectionWidget.hpp"

int main(int argc, char* argv[])
{
  QApplication app(argc, argv);
  QCoreApplication::setOrganizationName("ISR");
  QCoreApplication::setApplicationName("MAgcs");
  QCoreApplication::setApplicationVersion(QStringLiteral(MAGCS_VERSION));
  QApplication::setStyle(QStyleFactory::create("Fusion"));
  QApplication::setWindowIcon(QIcon(QStringLiteral(":/magcs.png")));

  QCommandLineParser parser;
  parser.setApplicationDescription("Mini-Acrobat ground control station");
  parser.addHelpOption();
  parser.addVersionOption();
  QCommandLineOption modeOpt({"m", "mode"}, "tailscale | manual | simulator (default: last used)",
                             "mode");
  QCommandLineOption hostOpt({"H", "host"}, "rosbridge host (default: last used)", "host");
  QCommandLineOption portOpt({"p", "port"}, "rosbridge port (default: last used)", "port");
  QCommandLineOption connectOpt("connect", "connect on start-up, skipping the connect window");
  parser.addOption(modeOpt);
  parser.addOption(hostOpt);
  parser.addOption(portOpt);
  parser.addOption(connectOpt);
  parser.process(app);

  magcs::MainWindow w;

  magcs::ConnectionTarget t = w.drone()->target();
  if (parser.isSet(modeOpt)) {
    if (!magcs::ConnectionTarget::modeFromKey(parser.value(modeOpt), &t.mode)) {
      qCritical("unknown --mode '%s'", qPrintable(parser.value(modeOpt)));
      return 2;
    }
    t.host = magcs::ConnectionTarget::savedHost(t.mode);
  } else if (parser.isSet(hostOpt)) {
    t.mode = magcs::ConnectionTarget::Mode::Manual;
  }
  if (parser.isSet(hostOpt)) {
    t.host = parser.value(hostOpt);
  }
  if (parser.isSet(portOpt)) {
    t.port = static_cast<quint16>(parser.value(portOpt).toUInt());
  }
  w.drone()->setTarget(t);

  if (parser.isSet(connectOpt)) {
    w.show();
    w.connection()->connectNow();
  } else {
    if (!w.runConnectDialog(true)) {
      return 0;
    }
    w.show();
  }
  return app.exec();
}
