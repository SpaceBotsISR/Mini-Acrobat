#pragma once
// One dark colour scheme for the whole app (window, docks, plots, buttons).
#include <QColor>
#include <QList>
#include <QString>

class QApplication;

namespace magcs::theme {

// Surfaces
inline const QColor kBg{0x16, 0x18, 0x1d};        // window / dock background
inline const QColor kPanel{0x1d, 0x20, 0x26};     // panel content
inline const QColor kRaised{0x26, 0x2a, 0x31};    // inputs, buttons
inline const QColor kBorder{0x33, 0x38, 0x41};
inline const QColor kGrid{0x2a, 0x2e, 0x36};
inline const QColor kText{0xe6, 0xe8, 0xeb};
inline const QColor kMuted{0x8b, 0x93, 0x9e};

// Meaning
inline const QColor kAccent{0x4c, 0x8d, 0xff};
inline const QColor kOk{0x2f, 0x9e, 0x44};
inline const QColor kWarn{0xe0, 0xa8, 0x00};
inline const QColor kBad{0xd6, 0x39, 0x39};

// Default series colours (cycled).
const QList<QColor>& seriesPalette();
QColor seriesColor(int index);

// Fusion + dark palette + app style sheet.
void apply(QApplication& app);

// "#rrggbb"
inline QString css(const QColor& c) { return c.name(QColor::HexRgb); }

}  // namespace magcs::theme
