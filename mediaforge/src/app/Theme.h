#pragma once

#include <QColor>
#include <QIcon>
#include <QString>

class QApplication;

namespace mf {

void applyDarkTheme(QApplication& app);

namespace theme {
inline QColor accent() { return QColor(61, 142, 232); }
inline QColor panel() { return QColor(43, 43, 46); }
inline QColor canvasBackdrop() { return QColor(30, 30, 32); }
inline QColor text() { return QColor(220, 220, 224); }
inline QColor dimText() { return QColor(140, 140, 148); }
} // namespace theme

// Icons are drawn with QPainter so the bundle doesn't depend on an icon theme.
QIcon icon(const QString& name);

} // namespace mf
