#pragma once

#include "image/Layer.h"

#include <QObject>
#include <QUndoStack>

namespace mf {

class Document : public QObject {
    Q_OBJECT
public:
    explicit Document(QObject* parent = nullptr);

    // These replace the whole document and clear the history.
    void reset(QSize size, QColor background);
    void resetFromImage(const QImage& image, const QString& layerName);
    void resetFromState(const DocState& state);

    const DocState& state() const { return m_state; }
    QSize size() const { return m_state.size; }
    QRect rect() const { return QRect(QPoint(0, 0), m_state.size); }
    int layerCount() const { return m_state.layers.size(); }
    const Layer& layer(int i) const { return m_state.layers.at(i); }
    int activeIndex() const { return m_state.active; }
    bool isEmpty() const { return m_state.layers.isEmpty(); }

    QImage composite() const;
    void compositeInto(QImage& target, const QRect& r) const;
    QColor sampleComposite(QPoint p) const;
    QColor sampleLayer(int layer, QPoint p) const;

    bool hasSelection() const { return !m_state.selection.isEmpty(); }
    const QPainterPath& selection() const { return m_state.selection; }
    QImage selectionMask() const; // Grayscale8, null when nothing is selected
    QRect selectionBounds() const;
    void setSelection(const QPainterPath& path, const QString& undoText);

    // Selecting a layer is UI state, not an undo step.
    void setActiveLayer(int i);

    void addLayer(const QString& name = QString(), const QImage& image = QImage());
    void addTextLayer(const TextInfo& info);
    void updateTextLayer(int i, const TextInfo& info);
    void deleteLayer(int i);
    void duplicateLayer(int i);
    void moveLayer(int from, int to);
    void mergeDown(int i);
    void flatten();
    void setLayerVisible(int i, bool visible);
    void setLayerOpacity(int i, qreal opacity);
    void setLayerBlendMode(int i, BlendMode mode);
    void renameLayer(int i, const QString& name);

    // Commits an arbitrary structural change as one undo step. mergeId >= 0
    // lets consecutive changes with the same id collapse (e.g. slider drags).
    void commitState(const DocState& newState, const QString& text, int mergeId = -1);

    // Pixel edits on a single layer record only the changed rectangle.
    void beginPixelEdit(int layer);
    bool isEditing() const { return m_editLayer >= 0; }
    int editLayer() const { return m_editLayer; }
    QImage& editImage();
    const QImage& editOriginal() const { return m_editOriginal; }
    void notifyEdit(const QRect& dirty);
    void endPixelEdit(const QString& text, const QRect& dirty);
    void cancelPixelEdit();

    void applyLayerImage(int layer, const QImage& image, const QString& text);

    QUndoStack* undoStack() { return &m_undo; }
    bool isModified() const { return !m_undo.isClean(); }
    void markClean() { m_undo.setClean(); }

    QString filePath() const { return m_filePath; }
    void setFilePath(const QString& path) { m_filePath = path; }

    QString nextLayerName() const;

    // Used by undo commands.
    void restoreState(const DocState& state);
    void writePixels(int layer, const QRect& r, const QImage& pixels, const std::optional<TextInfo>& text);

signals:
    void contentChanged(const QRect& rect);
    void structureChanged();
    void selectionChanged();

private:
    void invalidateMask() { m_maskValid = false; }

    DocState m_state;
    QUndoStack m_undo;
    QString m_filePath;

    int m_editLayer = -1;
    QImage m_editOriginal;
    std::optional<TextInfo> m_editTextBefore;

    mutable QImage m_maskCache;
    mutable bool m_maskValid = false;
};

} // namespace mf
