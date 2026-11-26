#include "widgets/ConsoleWidget.hpp"

#include <QTime>

#include "core/Drone.hpp"

namespace magcs {

ConsoleWidget::ConsoleWidget(Drone* drone, QWidget* parent)
: QPlainTextEdit(parent)
{
  setReadOnly(true);
  setMaximumBlockCount(2000);
  setStyleSheet("font-family: monospace;");
  connect(drone, &Drone::log, this, &ConsoleWidget::append);
}

void ConsoleWidget::append(const QString& line)
{
  appendPlainText(QStringLiteral("%1  %2").arg(QTime::currentTime().toString("HH:mm:ss.zzz"), line));
}

}  // namespace magcs
