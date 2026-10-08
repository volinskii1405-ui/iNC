#pragma once

#include "imageui/Tools.h"

#include <QHash>
#include <QWidget>

class QStackedWidget;

namespace mf {

// Foreground/background color squares under the tool bar.
class ColorSwatch : public QWidget {
    Q_OBJECT
public:
    explicit ColorSwatch(ToolSettings* settings, QWidget* parent = nullptr);
    QSize sizeHint() const override { return QSize(56, 60); }

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;

private:
    QRect fgRect() const { return QRect(4, 4, 30, 30); }
    QRect bgRect() const { return QRect(20, 20, 30, 30); }
    QRect swapRect() const { return QRect(38, 2, 16, 16); }
    QRect resetRect() const { return QRect(2, 40, 16, 16); }
    ToolSettings* m_settings;
};

// Context-sensitive options for the current tool.
class ToolOptionsBar : public QWidget {
    Q_OBJECT
public:
    ToolOptionsBar(ToolSettings* settings, QWidget* parent = nullptr);
    void showTool(ToolId id);
    void syncFromSettings();

signals:
    void cropApply();
    void cropCancel();

private:
    QWidget* brushPage(bool eraser);
    QWidget* fillPage();
    QWidget* pickerPage();
    QWidget* selectPage();
    QWidget* gradientPage();
    QWidget* hintPage(const QString& text);
    QWidget* cropPage();

    ToolSettings* m_settings;
    QStackedWidget* m_stack;
    QHash<int, int> m_pageForTool;
    QList<std::function<void()>> m_syncers;
};

} // namespace mf
