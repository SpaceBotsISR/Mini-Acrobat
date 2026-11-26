#include "widgets/Theme.hpp"

#include <QApplication>
#include <QPalette>
#include <QStyleFactory>

namespace magcs::theme {

const QList<QColor>& seriesPalette()
{
  // Distinct on a dark background (Foxglove-like).
  static const QList<QColor> p{
    QColor(0x4c, 0x8d, 0xff), QColor(0xf5, 0x9f, 0x3a), QColor(0x3d, 0xd6, 0x8c),
    QColor(0xe8, 0x5d, 0x75), QColor(0xb1, 0x8c, 0xff), QColor(0x3c, 0xc8, 0xd9),
    QColor(0xf2, 0xd3, 0x4b), QColor(0xff, 0x7a, 0xc6), QColor(0x9a, 0xd1, 0x4b),
    QColor(0xc9, 0xcd, 0xd3),
  };
  return p;
}

QColor seriesColor(int index)
{
  const auto& p = seriesPalette();
  return p.at(((index % p.size()) + p.size()) % p.size());
}

void apply(QApplication& app)
{
  app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

  QPalette pal;
  pal.setColor(QPalette::Window, kBg);
  pal.setColor(QPalette::WindowText, kText);
  pal.setColor(QPalette::Base, kRaised.darker(115));
  pal.setColor(QPalette::AlternateBase, kPanel);
  pal.setColor(QPalette::ToolTipBase, kRaised);
  pal.setColor(QPalette::ToolTipText, kText);
  pal.setColor(QPalette::PlaceholderText, kMuted);
  pal.setColor(QPalette::Text, kText);
  pal.setColor(QPalette::Button, kRaised);
  pal.setColor(QPalette::ButtonText, kText);
  pal.setColor(QPalette::BrightText, Qt::white);
  pal.setColor(QPalette::Highlight, kAccent);
  pal.setColor(QPalette::HighlightedText, Qt::white);
  pal.setColor(QPalette::Link, kAccent.lighter(115));
  pal.setColor(QPalette::Light, kBorder.lighter(130));
  pal.setColor(QPalette::Midlight, kBorder.lighter(115));
  pal.setColor(QPalette::Mid, kBorder);
  pal.setColor(QPalette::Dark, kBg.darker(120));
  pal.setColor(QPalette::Shadow, Qt::black);
  for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText}) {
    pal.setColor(QPalette::Disabled, role, kMuted.darker(130));
  }
  pal.setColor(QPalette::Disabled, QPalette::Button, kPanel);
  app.setPalette(pal);

  const QString qss = QStringLiteral(R"(
QMainWindow, QDialog { background: %1; }
QMainWindow::separator { background: %1; width: 4px; height: 4px; }
QMainWindow::separator:hover { background: %5; }
QDockWidget { color: %4; font-size: 11px; }
QDockWidget::title { background: %1; padding: 5px 8px; text-align: left; }
QDockWidget > QWidget { background: %2; }
QTabBar::tab { background: %1; color: %4; padding: 5px 12px; border: none; }
QTabBar::tab:selected { color: %6; border-bottom: 2px solid %5; }
QTabBar::tab:hover { color: %6; }
QToolBar { background: %1; border: none; spacing: 6px; padding: 3px 6px; }
QStatusBar { background: %1; color: %4; }
QMenuBar { background: %1; }
QGroupBox { border: 1px solid %3; border-radius: 6px; margin-top: 14px; padding-top: 6px; }
QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; color: %4; }
QPushButton { background: %7; border: 1px solid %3; border-radius: 5px; padding: 4px 12px; }
QPushButton:hover { border-color: %5; }
QPushButton:pressed { background: %3; }
QPushButton:disabled { color: %4; background: %2; border-color: %2; }
QPushButton:checked { background: %5; border-color: %5; color: white; }
QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox, QPlainTextEdit {
  background: %8; border: 1px solid %3; border-radius: 5px; padding: 3px 6px;
  selection-background-color: %5; }
QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus, QPlainTextEdit:focus {
  border-color: %5; }
QTreeView, QTableView, QListView { background: %2; border: none; alternate-background-color: %1; }
QHeaderView::section { background: %1; color: %4; border: none; padding: 4px 6px; }
QScrollBar:vertical { background: transparent; width: 10px; margin: 0; }
QScrollBar::handle:vertical { background: %3; border-radius: 4px; min-height: 24px; margin: 2px; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 0; }
QScrollBar::handle:horizontal { background: %3; border-radius: 4px; min-width: 24px; margin: 2px; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QToolTip { background: %7; color: %6; border: 1px solid %3; }
QCheckBox::indicator { width: 14px; height: 14px; border-radius: 3px; border: 1px solid %3; background: %8; }
QCheckBox::indicator:checked { background: %5; border-color: %5; }
QCheckBox::indicator:disabled { background: %2; }
)")
    .arg(css(kBg), css(kPanel), css(kBorder), css(kMuted), css(kAccent), css(kText),
         css(kRaised), css(kRaised.darker(115)));
  app.setStyleSheet(qss);
}

}  // namespace magcs::theme
