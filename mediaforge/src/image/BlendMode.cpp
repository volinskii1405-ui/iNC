#include "image/BlendMode.h"

#include <QObject>

namespace mf {

namespace {
struct ModeInfo {
    BlendMode mode;
    const char* id;
    const char* name;
    QPainter::CompositionMode comp;
};

const ModeInfo kModes[kBlendModeCount] = {
    {BlendMode::Normal, "normal", "Обычный", QPainter::CompositionMode_SourceOver},
    {BlendMode::Multiply, "multiply", "Умножение", QPainter::CompositionMode_Multiply},
    {BlendMode::Screen, "screen", "Экран", QPainter::CompositionMode_Screen},
    {BlendMode::Overlay, "overlay", "Перекрытие", QPainter::CompositionMode_Overlay},
    {BlendMode::Darken, "darken", "Затемнение", QPainter::CompositionMode_Darken},
    {BlendMode::Lighten, "lighten", "Замена светлым", QPainter::CompositionMode_Lighten},
    {BlendMode::ColorDodge, "color-dodge", "Осветление основы", QPainter::CompositionMode_ColorDodge},
    {BlendMode::ColorBurn, "color-burn", "Затемнение основы", QPainter::CompositionMode_ColorBurn},
    {BlendMode::HardLight, "hard-light", "Жёсткий свет", QPainter::CompositionMode_HardLight},
    {BlendMode::SoftLight, "soft-light", "Мягкий свет", QPainter::CompositionMode_SoftLight},
    {BlendMode::Difference, "difference", "Разница", QPainter::CompositionMode_Difference},
    {BlendMode::Exclusion, "exclusion", "Исключение", QPainter::CompositionMode_Exclusion},
};
} // namespace

QString blendModeName(BlendMode mode)
{
    return QString::fromUtf8(kModes[int(mode)].name);
}

QString blendModeId(BlendMode mode)
{
    return QString::fromLatin1(kModes[int(mode)].id);
}

BlendMode blendModeFromId(const QString& id, bool* ok)
{
    for (const auto& m : kModes) {
        if (id == QLatin1String(m.id)) {
            if (ok)
                *ok = true;
            return m.mode;
        }
    }
    if (ok)
        *ok = false;
    return BlendMode::Normal;
}

QPainter::CompositionMode toCompositionMode(BlendMode mode)
{
    return kModes[int(mode)].comp;
}

} // namespace mf
