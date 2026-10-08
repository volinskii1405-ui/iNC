#pragma once

#include <QPainter>
#include <QString>
#include <QStringList>

namespace mf {

enum class BlendMode {
    Normal,
    Multiply,
    Screen,
    Overlay,
    Darken,
    Lighten,
    ColorDodge,
    ColorBurn,
    HardLight,
    SoftLight,
    Difference,
    Exclusion,
};

constexpr int kBlendModeCount = 12;

QString blendModeName(BlendMode mode);   // локализованное имя для интерфейса
QString blendModeId(BlendMode mode);     // стабильный идентификатор для файла проекта
BlendMode blendModeFromId(const QString& id, bool* ok = nullptr);
QPainter::CompositionMode toCompositionMode(BlendMode mode);

} // namespace mf
