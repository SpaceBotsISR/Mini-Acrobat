#pragma once
#include <QPlainTextEdit>

namespace magcs {

class Drone;

class ConsoleWidget : public QPlainTextEdit {
  Q_OBJECT
public:
  explicit ConsoleWidget(Drone* drone, QWidget* parent = nullptr);

public slots:
  void append(const QString& line);
};

}  // namespace magcs
