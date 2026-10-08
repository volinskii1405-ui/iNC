#include "app/Theme.h"

#include <QApplication>
#include <QHash>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPixmap>
#include <QStyleFactory>
#include <QtMath>

namespace mf {

void applyDarkTheme(QApplication& app)
{
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QPalette p;
    const QColor window(43, 43, 46), base(30, 30, 32), alt(38, 38, 41), button(53, 53, 57);
    p.setColor(QPalette::Window, window);
    p.setColor(QPalette::WindowText, theme::text());
    p.setColor(QPalette::Base, base);
    p.setColor(QPalette::AlternateBase, alt);
    p.setColor(QPalette::ToolTipBase, QColor(25, 25, 27));
    p.setColor(QPalette::ToolTipText, theme::text());
    p.setColor(QPalette::PlaceholderText, theme::dimText());
    p.setColor(QPalette::Text, theme::text());
    p.setColor(QPalette::Button, button);
    p.setColor(QPalette::ButtonText, theme::text());
    p.setColor(QPalette::BrightText, QColor(255, 90, 90));
    p.setColor(QPalette::Highlight, theme::accent());
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::Link, theme::accent().lighter(120));
    p.setColor(QPalette::Light, QColor(70, 70, 75));
    p.setColor(QPalette::Midlight, QColor(60, 60, 64));
    p.setColor(QPalette::Mid, QColor(40, 40, 43));
    p.setColor(QPalette::Dark, QColor(24, 24, 26));
    p.setColor(QPalette::Shadow, QColor(10, 10, 10));
    for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        p.setColor(QPalette::Disabled, role, QColor(110, 110, 116));
    p.setColor(QPalette::Disabled, QPalette::Highlight, QColor(70, 70, 75));
    app.setPalette(p);
}

namespace {

using Draw = void (*)(QPainter&);

QPen pen(qreal w = 4.0) { return QPen(theme::text(), w, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin); }

void tri(QPainter& p, QPointF a, QPointF b, QPointF c)
{
    QPainterPath path(a);
    path.lineTo(b);
    path.lineTo(c);
    path.closeSubpath();
    p.fillPath(path, theme::text());
}

const QHash<QString, Draw>& drawers()
{
    static const QHash<QString, Draw> map = {
        {"brush", [](QPainter& p) {
             p.setPen(pen(5));
             p.drawLine(QPointF(46, 10), QPointF(26, 34));
             QPainterPath tip(QPointF(26, 34));
             tip.cubicTo(14, 34, 18, 50, 8, 54);
             tip.cubicTo(24, 56, 34, 48, 30, 38);
             tip.closeSubpath();
             p.fillPath(tip, theme::accent());
         }},
        {"eraser", [](QPainter& p) {
             p.setPen(pen(4));
             p.translate(32, 32);
             p.rotate(-35);
             p.drawRoundedRect(QRectF(-22, -11, 44, 22), 4, 4);
             p.fillRect(QRectF(-22, -11, 16, 22), theme::accent());
         }},
        {"bucket", [](QPainter& p) {
             p.setPen(pen(4));
             QPainterPath b;
             b.moveTo(14, 26);
             b.lineTo(32, 10);
             b.lineTo(50, 28);
             b.lineTo(32, 46);
             b.closeSubpath();
             p.drawPath(b);
             QPainterPath drop(QPointF(52, 36));
             drop.cubicTo(58, 46, 58, 54, 52, 54);
             drop.cubicTo(46, 54, 46, 46, 52, 36);
             p.fillPath(drop, theme::accent());
         }},
        {"picker", [](QPainter& p) {
             p.setPen(pen(5));
             p.drawLine(QPointF(14, 50), QPointF(40, 24));
             p.setBrush(theme::text());
             p.drawEllipse(QPointF(46, 18), 7, 7);
             p.setPen(pen(3));
             p.drawLine(QPointF(10, 54), QPointF(14, 50));
         }},
        {"rect-select", [](QPainter& p) {
             QPen d = pen(3);
             d.setDashPattern({2, 2});
             p.setPen(d);
             p.drawRect(QRectF(10, 14, 44, 36));
         }},
        {"ellipse-select", [](QPainter& p) {
             QPen d = pen(3);
             d.setDashPattern({2, 2});
             p.setPen(d);
             p.drawEllipse(QRectF(8, 14, 48, 36));
         }},
        {"lasso", [](QPainter& p) {
             QPen d = pen(3);
             d.setDashPattern({2, 2});
             p.setPen(d);
             QPainterPath l(QPointF(14, 40));
             l.cubicTo(4, 20, 30, 6, 48, 14);
             l.cubicTo(62, 22, 50, 44, 30, 42);
             l.cubicTo(20, 41, 18, 50, 24, 56);
             p.drawPath(l);
         }},
        {"move", [](QPainter& p) {
             p.setPen(pen(4));
             p.drawLine(QPointF(32, 10), QPointF(32, 54));
             p.drawLine(QPointF(10, 32), QPointF(54, 32));
             tri(p, {32, 4}, {24, 14}, {40, 14});
             tri(p, {32, 60}, {24, 50}, {40, 50});
             tri(p, {4, 32}, {14, 24}, {14, 40});
             tri(p, {60, 32}, {50, 24}, {50, 40});
         }},
        {"crop", [](QPainter& p) {
             p.setPen(pen(5));
             p.drawPolyline(QPolygonF({{16, 6}, {16, 48}, {58, 48}}));
             p.drawPolyline(QPolygonF({{6, 16}, {48, 16}, {48, 58}}));
         }},
        {"text", [](QPainter& p) {
             p.setPen(pen(6));
             p.drawLine(QPointF(14, 14), QPointF(50, 14));
             p.drawLine(QPointF(32, 14), QPointF(32, 52));
             p.drawLine(QPointF(24, 52), QPointF(40, 52));
         }},
        {"gradient", [](QPainter& p) {
             QLinearGradient g(8, 0, 56, 0);
             g.setColorAt(0, theme::text());
             g.setColorAt(1, QColor(0, 0, 0, 0));
             p.setPen(pen(3));
             p.setBrush(g);
             p.drawRoundedRect(QRectF(8, 14, 48, 36), 4, 4);
         }},
        {"hand", [](QPainter& p) {
             p.setPen(pen(4));
             for (int i = 0; i < 4; ++i)
                 p.drawLine(QPointF(20 + i * 8, 12 + (i == 0 || i == 3 ? 6 : 0)), QPointF(20 + i * 8, 36));
             p.drawRoundedRect(QRectF(16, 32, 32, 22), 8, 8);
             p.drawLine(QPointF(10, 30), QPointF(18, 40));
         }},
        {"undo", [](QPainter& p) {
             p.setPen(pen(5));
             QPainterPath a(QPointF(18, 26));
             a.cubicTo(30, 12, 56, 18, 52, 40);
             a.cubicTo(50, 50, 40, 54, 32, 54);
             p.drawPath(a);
             tri(p, {8, 26}, {24, 14}, {24, 36});
         }},
        {"redo", [](QPainter& p) {
             p.setPen(pen(5));
             QPainterPath a(QPointF(46, 26));
             a.cubicTo(34, 12, 8, 18, 12, 40);
             a.cubicTo(14, 50, 24, 54, 32, 54);
             p.drawPath(a);
             tri(p, {56, 26}, {40, 14}, {40, 36});
         }},
        {"play", [](QPainter& p) { tri(p, {18, 10}, {18, 54}, {54, 32}); }},
        {"pause", [](QPainter& p) {
             p.fillRect(QRectF(16, 12, 11, 40), theme::text());
             p.fillRect(QRectF(37, 12, 11, 40), theme::text());
         }},
        {"stop", [](QPainter& p) { p.fillRect(QRectF(16, 16, 32, 32), theme::text()); }},
        {"prev-frame", [](QPainter& p) {
             p.fillRect(QRectF(12, 14, 6, 36), theme::text());
             tri(p, {50, 14}, {50, 50}, {22, 32});
         }},
        {"next-frame", [](QPainter& p) {
             p.fillRect(QRectF(46, 14, 6, 36), theme::text());
             tri(p, {14, 14}, {14, 50}, {42, 32});
         }},
        {"to-start", [](QPainter& p) {
             p.fillRect(QRectF(8, 14, 6, 36), theme::text());
             tri(p, {36, 14}, {36, 50}, {14, 32});
             tri(p, {58, 14}, {58, 50}, {36, 32});
         }},
        {"to-end", [](QPainter& p) {
             p.fillRect(QRectF(50, 14, 6, 36), theme::text());
             tri(p, {6, 14}, {6, 50}, {28, 32});
             tri(p, {28, 14}, {28, 50}, {50, 32});
         }},
        {"split", [](QPainter& p) {
             p.setPen(pen(4));
             p.drawEllipse(QPointF(18, 46), 8, 8);
             p.drawEllipse(QPointF(46, 46), 8, 8);
             p.drawLine(QPointF(24, 40), QPointF(46, 8));
             p.drawLine(QPointF(40, 40), QPointF(18, 8));
         }},
        {"delete", [](QPainter& p) {
             p.setPen(pen(4));
             p.drawLine(QPointF(12, 16), QPointF(52, 16));
             p.drawLine(QPointF(26, 10), QPointF(38, 10));
             p.drawPolyline(QPolygonF({{18, 16}, {22, 56}, {42, 56}, {46, 16}}));
             p.drawLine(QPointF(29, 24), QPointF(29, 48));
             p.drawLine(QPointF(35, 24), QPointF(35, 48));
         }},
        {"add", [](QPainter& p) {
             p.setPen(pen(6));
             p.drawLine(QPointF(32, 12), QPointF(32, 52));
             p.drawLine(QPointF(12, 32), QPointF(52, 32));
         }},
        {"duplicate", [](QPainter& p) {
             p.setPen(pen(4));
             p.drawRect(QRectF(10, 10, 30, 30));
             p.fillRect(QRectF(24, 24, 30, 30), theme::panel());
             p.drawRect(QRectF(24, 24, 30, 30));
         }},
        {"up", [](QPainter& p) {
             p.setPen(pen(5));
             p.drawLine(QPointF(32, 54), QPointF(32, 16));
             tri(p, {32, 6}, {18, 24}, {46, 24});
         }},
        {"down", [](QPainter& p) {
             p.setPen(pen(5));
             p.drawLine(QPointF(32, 10), QPointF(32, 48));
             tri(p, {32, 58}, {18, 40}, {46, 40});
         }},
        {"left", [](QPainter& p) {
             p.setPen(pen(5));
             p.drawLine(QPointF(54, 32), QPointF(16, 32));
             tri(p, {6, 32}, {24, 18}, {24, 46});
         }},
        {"right", [](QPainter& p) {
             p.setPen(pen(5));
             p.drawLine(QPointF(10, 32), QPointF(48, 32));
             tri(p, {58, 32}, {40, 18}, {40, 46});
         }},
        {"merge", [](QPainter& p) {
             p.setPen(pen(4));
             p.drawLine(QPointF(16, 10), QPointF(32, 30));
             p.drawLine(QPointF(48, 10), QPointF(32, 30));
             p.drawLine(QPointF(32, 30), QPointF(32, 50));
             tri(p, {32, 60}, {22, 46}, {42, 46});
         }},
        {"open", [](QPainter& p) {
             p.setPen(pen(4));
             p.drawPolyline(QPolygonF({{8, 50}, {8, 14}, {24, 14}, {30, 20}, {52, 20}, {52, 28}}));
             p.drawPolygon(QPolygonF({{8, 50}, {18, 30}, {58, 30}, {48, 50}}));
         }},
        {"save", [](QPainter& p) {
             p.setPen(pen(4));
             p.drawRoundedRect(QRectF(10, 10, 44, 44), 4, 4);
             p.drawRect(QRectF(20, 10, 22, 14));
             p.fillRect(QRectF(20, 34, 24, 20), theme::accent());
         }},
        {"new", [](QPainter& p) {
             p.setPen(pen(4));
             p.drawPolygon(QPolygonF({{14, 6}, {38, 6}, {50, 18}, {50, 58}, {14, 58}}));
             p.drawPolyline(QPolygonF({{38, 6}, {38, 18}, {50, 18}}));
         }},
        {"export", [](QPainter& p) {
             p.setPen(pen(4));
             p.drawPolyline(QPolygonF({{24, 14}, {10, 14}, {10, 54}, {50, 54}, {50, 40}}));
             p.drawLine(QPointF(26, 38), QPointF(52, 12));
             tri(p, {58, 6}, {38, 10}, {54, 26});
         }},
        {"import", [](QPainter& p) {
             p.setPen(pen(4));
             p.drawPolyline(QPolygonF({{24, 14}, {10, 14}, {10, 54}, {50, 54}, {50, 40}}));
             p.drawLine(QPointF(54, 10), QPointF(32, 32));
             tri(p, {26, 38}, {30, 20}, {44, 34});
         }},
        {"image", [](QPainter& p) {
             p.setPen(pen(4));
             p.drawRoundedRect(QRectF(8, 12, 48, 40), 4, 4);
             tri(p, {14, 46}, {28, 26}, {40, 46});
             tri(p, {30, 46}, {42, 32}, {52, 46});
             p.setBrush(theme::accent());
             p.setPen(Qt::NoPen);
             p.drawEllipse(QPointF(44, 22), 5, 5);
         }},
        {"video", [](QPainter& p) {
             p.setPen(pen(4));
             p.drawRoundedRect(QRectF(6, 16, 38, 32), 4, 4);
             tri(p, {58, 18}, {58, 46}, {44, 32});
         }},
        {"audio", [](QPainter& p) {
             p.setPen(pen(4));
             p.drawLine(QPointF(24, 46), QPointF(24, 12));
             p.drawLine(QPointF(24, 12), QPointF(50, 6));
             p.drawLine(QPointF(50, 6), QPointF(50, 40));
             p.setBrush(theme::text());
             p.drawEllipse(QPointF(18, 48), 7, 6);
             p.drawEllipse(QPointF(44, 42), 7, 6);
         }},
        {"speaker", [](QPainter& p) {
             p.setPen(pen(4));
             p.setBrush(theme::text());
             p.drawPolygon(QPolygonF({{8, 24}, {20, 24}, {34, 10}, {34, 54}, {20, 40}, {8, 40}}));
             p.setBrush(Qt::NoBrush);
             p.drawArc(QRectF(30, 18, 20, 28), -50 * 16, 100 * 16);
             p.drawArc(QRectF(30, 10, 30, 44), -50 * 16, 100 * 16);
         }},
        {"mute", [](QPainter& p) {
             p.setPen(pen(4));
             p.setBrush(theme::text());
             p.drawPolygon(QPolygonF({{8, 24}, {20, 24}, {34, 10}, {34, 54}, {20, 40}, {8, 40}}));
             p.setPen(QPen(QColor(235, 90, 90), 5, Qt::SolidLine, Qt::RoundCap));
             p.drawLine(QPointF(42, 22), QPointF(58, 42));
             p.drawLine(QPointF(58, 22), QPointF(42, 42));
         }},
        {"fade-in", [](QPainter& p) {
             QPainterPath a(QPointF(6, 54));
             a.lineTo(58, 10);
             a.lineTo(58, 54);
             a.closeSubpath();
             p.fillPath(a, theme::accent());
         }},
        {"fade-out", [](QPainter& p) {
             QPainterPath a(QPointF(6, 10));
             a.lineTo(58, 54);
             a.lineTo(6, 54);
             a.closeSubpath();
             p.fillPath(a, theme::accent());
         }},
        {"zoom-in", [](QPainter& p) {
             p.setPen(pen(4));
             p.drawEllipse(QPointF(26, 26), 16, 16);
             p.drawLine(QPointF(38, 38), QPointF(56, 56));
             p.drawLine(QPointF(18, 26), QPointF(34, 26));
             p.drawLine(QPointF(26, 18), QPointF(26, 34));
         }},
        {"zoom-out", [](QPainter& p) {
             p.setPen(pen(4));
             p.drawEllipse(QPointF(26, 26), 16, 16);
             p.drawLine(QPointF(38, 38), QPointF(56, 56));
             p.drawLine(QPointF(18, 26), QPointF(34, 26));
         }},
        {"fit", [](QPainter& p) {
             p.setPen(pen(4));
             p.drawPolyline(QPolygonF({{8, 22}, {8, 8}, {22, 8}}));
             p.drawPolyline(QPolygonF({{42, 8}, {56, 8}, {56, 22}}));
             p.drawPolyline(QPolygonF({{56, 42}, {56, 56}, {42, 56}}));
             p.drawPolyline(QPolygonF({{22, 56}, {8, 56}, {8, 42}}));
         }},
        {"marker-in", [](QPainter& p) {
             p.setPen(pen(5));
             p.drawPolyline(QPolygonF({{40, 10}, {22, 10}, {22, 54}, {40, 54}}));
         }},
        {"marker-out", [](QPainter& p) {
             p.setPen(pen(5));
             p.drawPolyline(QPolygonF({{24, 10}, {42, 10}, {42, 54}, {24, 54}}));
         }},
        {"scissors", [](QPainter& p) {
             p.setPen(pen(4));
             p.drawEllipse(QPointF(16, 46), 8, 8);
             p.drawEllipse(QPointF(16, 18), 8, 8);
             p.drawLine(QPointF(22, 40), QPointF(56, 14));
             p.drawLine(QPointF(22, 24), QPointF(56, 50));
         }},
        {"trim", [](QPainter& p) {
             p.setPen(pen(4));
             p.fillRect(QRectF(20, 16, 24, 32), theme::accent());
             p.drawLine(QPointF(20, 8), QPointF(20, 56));
             p.drawLine(QPointF(44, 8), QPointF(44, 56));
         }},
        {"silence", [](QPainter& p) {
             p.setPen(pen(4));
             p.drawLine(QPointF(6, 32), QPointF(20, 32));
             p.drawLine(QPointF(44, 32), QPointF(58, 32));
             p.setPen(pen(3));
             for (int i = 0; i < 5; ++i)
                 p.drawLine(QPointF(22 + i * 5, 32 - (i % 2 ? 4 : 12)), QPointF(22 + i * 5, 32 + (i % 2 ? 4 : 12)));
         }},
        {"volume", [](QPainter& p) {
             p.setBrush(theme::accent());
             p.setPen(Qt::NoPen);
             p.drawPolygon(QPolygonF({{6, 54}, {58, 10}, {58, 54}}));
         }},
        {"speed", [](QPainter& p) {
             p.setPen(pen(4));
             p.drawArc(QRectF(8, 14, 48, 48), 0, 180 * 16);
             p.drawLine(QPointF(32, 38), QPointF(48, 22));
             p.setBrush(theme::text());
             p.drawEllipse(QPointF(32, 38), 4, 4);
         }},
        {"frame", [](QPainter& p) {
             p.setPen(pen(4));
             p.drawRect(QRectF(8, 12, 48, 40));
             for (int i = 0; i < 4; ++i) {
                 p.fillRect(QRectF(12 + i * 12, 15, 6, 4), theme::text());
                 p.fillRect(QRectF(12 + i * 12, 45, 6, 4), theme::text());
             }
         }},
        {"swap", [](QPainter& p) {
             p.setPen(pen(4));
             QPainterPath a(QPointF(14, 22));
             a.cubicTo(14, 8, 34, 8, 44, 8);
             p.drawPath(a);
             tri(p, {54, 8}, {42, 0}, {42, 16});
             QPainterPath b(QPointF(50, 42));
             b.cubicTo(50, 56, 30, 56, 20, 56);
             p.drawPath(b);
             tri(p, {10, 56}, {22, 48}, {22, 64});
         }},
    };
    return map;
}

QPixmap render(const QString& name, int size, bool disabled)
{
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.scale(size / 64.0, size / 64.0);
    if (disabled)
        p.setOpacity(0.35);
    auto it = drawers().find(name);
    if (it != drawers().end())
        it.value()(p);
    return pm;
}

} // namespace

QIcon icon(const QString& name)
{
    static QHash<QString, QIcon> cache;
    auto it = cache.find(name);
    if (it != cache.end())
        return it.value();
    QIcon ic;
    for (int s : {16, 24, 32, 48, 64}) {
        ic.addPixmap(render(name, s, false), QIcon::Normal);
        ic.addPixmap(render(name, s, true), QIcon::Disabled);
    }
    cache.insert(name, ic);
    return ic;
}

} // namespace mf
