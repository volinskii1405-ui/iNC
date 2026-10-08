#include "image/Document.h"

#include <QFontMetrics>
#include <QPainter>
#include <QUndoCommand>

namespace mf {

QImage makeLayerImage(QSize size, QColor fill)
{
    QImage img(size, kLayerFormat);
    img.fill(fill);
    return img;
}

QRect textBounds(const TextInfo& info)
{
    QFontMetrics fm(info.font);
    QRect r = fm.boundingRect(QRect(info.pos, QSize(100000, 100000)), Qt::AlignLeft | Qt::AlignTop, info.text);
    return r.adjusted(-2, -2, 2, 2);
}

QImage renderTextImage(const TextInfo& info, QSize canvas)
{
    QImage img = makeLayerImage(canvas);
    QPainter p(&img);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.setRenderHint(QPainter::Antialiasing);
    p.setFont(info.font);
    p.setPen(info.color);
    p.drawText(QRect(info.pos, QSize(100000, 100000)), Qt::AlignLeft | Qt::AlignTop, info.text);
    return img;
}

namespace {

class StateCommand : public QUndoCommand {
public:
    StateCommand(Document* doc, DocState before, DocState after, const QString& text, int mergeId)
        : QUndoCommand(text), m_doc(doc), m_before(std::move(before)), m_after(std::move(after)), m_mergeId(mergeId)
    {
    }
    void undo() override { m_doc->restoreState(m_before); }
    void redo() override { m_doc->restoreState(m_after); }
    int id() const override { return m_mergeId; }
    bool mergeWith(const QUndoCommand* other) override
    {
        if (other->id() != m_mergeId || m_mergeId < 0)
            return false;
        m_after = static_cast<const StateCommand*>(other)->m_after;
        return true;
    }

private:
    Document* m_doc;
    DocState m_before;
    DocState m_after;
    int m_mergeId;
};

class PixelCommand : public QUndoCommand {
public:
    PixelCommand(Document* doc, int layer, QRect rect, QImage before, QImage after,
                 std::optional<TextInfo> textBefore, std::optional<TextInfo> textAfter, const QString& text)
        : QUndoCommand(text), m_doc(doc), m_layer(layer), m_rect(rect), m_before(std::move(before)),
          m_after(std::move(after)), m_textBefore(std::move(textBefore)), m_textAfter(std::move(textAfter))
    {
    }
    void undo() override { m_doc->writePixels(m_layer, m_rect, m_before, m_textBefore); }
    void redo() override
    {
        // The pixels are already in place when the command is first pushed.
        if (m_first) {
            m_first = false;
            return;
        }
        m_doc->writePixels(m_layer, m_rect, m_after, m_textAfter);
    }

private:
    Document* m_doc;
    int m_layer;
    QRect m_rect;
    QImage m_before;
    QImage m_after;
    std::optional<TextInfo> m_textBefore;
    std::optional<TextInfo> m_textAfter;
    bool m_first = true;
};

} // namespace

Document::Document(QObject* parent)
    : QObject(parent)
{
    m_undo.setUndoLimit(100);
}

void Document::reset(QSize size, QColor background)
{
    DocState s;
    s.size = size;
    Layer l;
    l.name = background.alpha() == 0 ? QStringLiteral("Слой 1") : QStringLiteral("Фон");
    l.image = makeLayerImage(size, background);
    s.layers.push_back(l);
    resetFromState(s);
}

void Document::resetFromImage(const QImage& image, const QString& layerName)
{
    DocState s;
    s.size = image.size();
    Layer l;
    l.name = layerName;
    l.image = image.convertToFormat(kLayerFormat);
    s.layers.push_back(l);
    resetFromState(s);
}

void Document::resetFromState(const DocState& state)
{
    cancelPixelEdit();
    m_undo.clear();
    m_state = state;
    if (m_state.active < 0 || m_state.active >= m_state.layers.size())
        m_state.active = m_state.layers.size() - 1;
    invalidateMask();
    m_undo.setClean();
    emit structureChanged();
    emit selectionChanged();
    emit contentChanged(rect());
}

QImage Document::composite() const
{
    QImage out = makeLayerImage(size());
    compositeInto(out, rect());
    return out;
}

void Document::compositeInto(QImage& target, const QRect& area) const
{
    const QRect r = area.intersected(rect());
    if (r.isEmpty())
        return;
    QPainter p(&target);
    p.setCompositionMode(QPainter::CompositionMode_Source);
    p.fillRect(r, Qt::transparent);
    for (const Layer& l : m_state.layers) {
        if (!l.visible || l.opacity <= 0.0)
            continue;
        p.setCompositionMode(toCompositionMode(l.mode));
        p.setOpacity(l.opacity);
        p.drawImage(r.topLeft(), l.image, r);
    }
}

static QColor unpremultiplied(QRgb px)
{
    return QColor::fromRgba(qUnpremultiply(px));
}

QColor Document::sampleComposite(QPoint pt) const
{
    if (!rect().contains(pt))
        return QColor();
    QImage one = makeLayerImage(QSize(1, 1));
    QPainter p(&one);
    for (const Layer& l : m_state.layers) {
        if (!l.visible || l.opacity <= 0.0)
            continue;
        p.setCompositionMode(toCompositionMode(l.mode));
        p.setOpacity(l.opacity);
        p.drawImage(QPoint(0, 0), l.image, QRect(pt, QSize(1, 1)));
    }
    p.end();
    return unpremultiplied(one.pixel(0, 0));
}

QColor Document::sampleLayer(int layer, QPoint pt) const
{
    if (layer < 0 || layer >= layerCount() || !rect().contains(pt))
        return QColor();
    return unpremultiplied(m_state.layers[layer].image.pixel(pt));
}

QImage Document::selectionMask() const
{
    if (!hasSelection())
        return QImage();
    if (!m_maskValid || m_maskCache.size() != size()) {
        QImage mask(size(), QImage::Format_Grayscale8);
        mask.fill(0);
        QPainter p(&mask);
        p.setRenderHint(QPainter::Antialiasing);
        p.fillPath(m_state.selection, Qt::white);
        p.end();
        m_maskCache = mask;
        m_maskValid = true;
    }
    return m_maskCache;
}

QRect Document::selectionBounds() const
{
    if (!hasSelection())
        return rect();
    return m_state.selection.boundingRect().toAlignedRect().intersected(rect());
}

void Document::setSelection(const QPainterPath& path, const QString& undoText)
{
    DocState s = m_state;
    s.selection = path;
    commitState(s, undoText);
}

void Document::setActiveLayer(int i)
{
    if (i < 0 || i >= layerCount() || i == m_state.active)
        return;
    m_state.active = i;
    emit structureChanged();
}

QString Document::nextLayerName() const
{
    int n = layerCount() + 1;
    for (;;) {
        const QString name = QStringLiteral("Слой %1").arg(n);
        bool used = false;
        for (const Layer& l : m_state.layers)
            used |= l.name == name;
        if (!used)
            return name;
        ++n;
    }
}

void Document::addLayer(const QString& name, const QImage& image)
{
    DocState s = m_state;
    Layer l;
    l.name = name.isEmpty() ? nextLayerName() : name;
    if (image.isNull()) {
        l.image = makeLayerImage(s.size);
    } else if (image.size() == s.size) {
        l.image = image.convertToFormat(kLayerFormat);
    } else {
        l.image = makeLayerImage(s.size);
        QPainter p(&l.image);
        p.drawImage(QPoint((s.size.width() - image.width()) / 2, (s.size.height() - image.height()) / 2), image);
    }
    const int idx = s.layers.isEmpty() ? 0 : s.active + 1;
    s.layers.insert(idx, l);
    s.active = idx;
    commitState(s, QStringLiteral("Новый слой"));
}

void Document::addTextLayer(const TextInfo& info)
{
    DocState s = m_state;
    Layer l;
    l.name = QStringLiteral("Текст: %1").arg(info.text.left(20).simplified());
    l.image = renderTextImage(info, s.size);
    l.text = info;
    const int idx = s.layers.isEmpty() ? 0 : s.active + 1;
    s.layers.insert(idx, l);
    s.active = idx;
    commitState(s, QStringLiteral("Текст"));
}

void Document::updateTextLayer(int i, const TextInfo& info)
{
    if (i < 0 || i >= layerCount())
        return;
    DocState s = m_state;
    Layer& l = s.layers[i];
    l.text = info;
    l.image = renderTextImage(info, s.size);
    l.name = QStringLiteral("Текст: %1").arg(info.text.left(20).simplified());
    commitState(s, QStringLiteral("Изменение текста"));
}

void Document::deleteLayer(int i)
{
    if (i < 0 || i >= layerCount() || layerCount() <= 1)
        return;
    DocState s = m_state;
    s.layers.removeAt(i);
    s.active = qBound(0, i - 1 < 0 ? 0 : i - 1, s.layers.size() - 1);
    commitState(s, QStringLiteral("Удаление слоя"));
}

void Document::duplicateLayer(int i)
{
    if (i < 0 || i >= layerCount())
        return;
    DocState s = m_state;
    Layer copy = s.layers[i];
    copy.name = copy.name + QStringLiteral(" копия");
    s.layers.insert(i + 1, copy);
    s.active = i + 1;
    commitState(s, QStringLiteral("Дублирование слоя"));
}

void Document::moveLayer(int from, int to)
{
    if (from < 0 || from >= layerCount() || to < 0 || to >= layerCount() || from == to)
        return;
    DocState s = m_state;
    s.layers.move(from, to);
    s.active = to;
    commitState(s, QStringLiteral("Порядок слоёв"));
}

void Document::mergeDown(int i)
{
    if (i <= 0 || i >= layerCount())
        return;
    DocState s = m_state;
    Layer& below = s.layers[i - 1];
    const Layer& above = s.layers[i];
    if (!below.visible) {
        // Merging into a hidden layer would make the result invisible; show it.
        below.visible = true;
    }
    QImage merged = below.image;
    {
        QPainter p(&merged);
        if (above.visible) {
            p.setCompositionMode(toCompositionMode(above.mode));
            p.setOpacity(above.opacity);
            p.drawImage(0, 0, above.image);
        }
    }
    below.image = merged;
    below.text.reset();
    s.layers.removeAt(i);
    s.active = i - 1;
    commitState(s, QStringLiteral("Объединение слоёв"));
}

void Document::flatten()
{
    if (layerCount() <= 1 && m_state.layers[0].opacity == 1.0 && m_state.layers[0].mode == BlendMode::Normal)
        return;
    DocState s = m_state;
    Layer l;
    l.name = QStringLiteral("Фон");
    l.image = composite();
    s.layers = {l};
    s.active = 0;
    commitState(s, QStringLiteral("Свести изображение"));
}

void Document::setLayerVisible(int i, bool visible)
{
    if (i < 0 || i >= layerCount() || m_state.layers[i].visible == visible)
        return;
    DocState s = m_state;
    s.layers[i].visible = visible;
    commitState(s, visible ? QStringLiteral("Показать слой") : QStringLiteral("Скрыть слой"));
}

void Document::setLayerOpacity(int i, qreal opacity)
{
    if (i < 0 || i >= layerCount())
        return;
    opacity = qBound(0.0, opacity, 1.0);
    if (qFuzzyCompare(m_state.layers[i].opacity, opacity))
        return;
    DocState s = m_state;
    s.layers[i].opacity = opacity;
    commitState(s, QStringLiteral("Непрозрачность слоя"), 1000 + i);
}

void Document::setLayerBlendMode(int i, BlendMode mode)
{
    if (i < 0 || i >= layerCount() || m_state.layers[i].mode == mode)
        return;
    DocState s = m_state;
    s.layers[i].mode = mode;
    commitState(s, QStringLiteral("Режим наложения"));
}

void Document::renameLayer(int i, const QString& name)
{
    if (i < 0 || i >= layerCount() || name.trimmed().isEmpty() || m_state.layers[i].name == name)
        return;
    DocState s = m_state;
    s.layers[i].name = name.trimmed();
    commitState(s, QStringLiteral("Переименование слоя"));
}

void Document::commitState(const DocState& newState, const QString& text, int mergeId)
{
    cancelPixelEdit();
    m_undo.push(new StateCommand(this, m_state, newState, text, mergeId));
}

void Document::restoreState(const DocState& state)
{
    const bool sizeChanged = state.size != m_state.size;
    const bool selChanged = !(state.selection == m_state.selection) || sizeChanged;
    m_state = state;
    if (selChanged)
        invalidateMask();
    emit structureChanged();
    if (selChanged)
        emit selectionChanged();
    emit contentChanged(rect());
}

void Document::beginPixelEdit(int layer)
{
    cancelPixelEdit();
    if (layer < 0 || layer >= layerCount())
        return;
    m_editLayer = layer;
    m_editOriginal = m_state.layers[layer].image;
    m_editTextBefore = m_state.layers[layer].text;
}

QImage& Document::editImage()
{
    Q_ASSERT(m_editLayer >= 0);
    return m_state.layers[m_editLayer].image;
}

void Document::notifyEdit(const QRect& dirty)
{
    emit contentChanged(dirty.intersected(rect()));
}

void Document::endPixelEdit(const QString& text, const QRect& dirty)
{
    if (m_editLayer < 0)
        return;
    const int layer = m_editLayer;
    const QRect r = dirty.intersected(rect());
    m_editLayer = -1;
    if (r.isEmpty()) {
        m_state.layers[layer].image = m_editOriginal;
        m_editOriginal = QImage();
        return;
    }
    const QImage before = m_editOriginal.copy(r);
    const QImage after = m_state.layers[layer].image.copy(r);
    m_editOriginal = QImage();
    // Painting over a text layer turns it into an ordinary raster layer.
    m_state.layers[layer].text.reset();
    m_undo.push(new PixelCommand(this, layer, r, before, after, m_editTextBefore, std::nullopt, text));
    emit contentChanged(r);
    emit structureChanged();
}

void Document::cancelPixelEdit()
{
    if (m_editLayer < 0)
        return;
    m_state.layers[m_editLayer].image = m_editOriginal;
    m_editLayer = -1;
    m_editOriginal = QImage();
    emit contentChanged(rect());
}

void Document::applyLayerImage(int layer, const QImage& image, const QString& text)
{
    if (layer < 0 || layer >= layerCount() || image.size() != size())
        return;
    beginPixelEdit(layer);
    editImage() = image.convertToFormat(kLayerFormat);
    endPixelEdit(text, rect());
}

void Document::writePixels(int layer, const QRect& r, const QImage& pixels, const std::optional<TextInfo>& text)
{
    if (layer < 0 || layer >= layerCount())
        return;
    QImage& img = m_state.layers[layer].image;
    QPainter p(&img);
    p.setCompositionMode(QPainter::CompositionMode_Source);
    p.drawImage(r.topLeft(), pixels);
    p.end();
    m_state.layers[layer].text = text;
    emit contentChanged(r);
    emit structureChanged();
}

} // namespace mf
