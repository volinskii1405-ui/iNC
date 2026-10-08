#pragma once

#include "app/Workspace.h"
#include "imageui/ImageDialogs.h"
#include "imageui/Tools.h"

#include <QHash>
#include <QLabel>

#include <memory>

class QAction;
class QActionGroup;

namespace mf {

class CanvasView;
class Document;
class LayersPanel;
class ToolOptionsBar;

class ImageEditor : public Workspace {
    Q_OBJECT
public:
    explicit ImageEditor(QWidget* parent = nullptr);
    ~ImageEditor() override;

    QList<QMenu*> menus() const override { return m_menus; }
    bool confirmClose() override;
    void openFiles(const QStringList& paths) override;

    Document* document() const { return m_doc; }
    CanvasView* canvas() const { return m_canvas; }
    void openImage(const QImage& image, const QString& name);
    bool openPath(const QString& path);
    void selectTool(ToolId id);
    QString documentTitle() const;
    QAction* action(const QString& id) const { return m_a.value(id); }
    ToolSettings* toolSettings() { return &m_settings; }

private:
    void buildActions();
    void buildMenus();
    void buildLayout();
    QAction* act(const QString& text, const QString& iconName, const QKeySequence& key, std::function<void()> fn);

    void newImage();
    void openDialog();
    void placeAsLayer();
    bool saveProject(bool saveAs);
    void exportImage();
    void copy(bool cut);
    void paste();
    void clearSelection();
    void fillSelection();
    void imageSize();
    void canvasSize();
    void rotate(double degrees);
    void rotateArbitrary();
    void flip(bool horizontal);
    void cropToSelection();
    void flipLayer(bool horizontal);
    void transformLayer();
    void runFilter(const QString& title, std::function<QImage(const QImage&)> fn);
    void filterDialog(const QString& title, const QVector<FilterParam>& params,
                      std::function<QImage(const QImage&, const QVariantMap&)> fn, bool respectSelection = true);
    void editText(QPoint pos, const std::optional<TextInfo>& existing, int layer);
    void updateActions();
    void updateStatus();
    bool requireDocument();

    Document* m_doc;
    CanvasView* m_canvas;
    ToolSettings m_settings;
    ToolContext m_ctx;
    std::unique_ptr<Tool> m_tool;
    ToolOptionsBar* m_options = nullptr;
    LayersPanel* m_layers = nullptr;
    QActionGroup* m_toolGroup = nullptr;
    QHash<int, QAction*> m_toolActions;
    QList<QMenu*> m_menus;
    QHash<QString, QAction*> m_a;
    QLabel* m_zoomLabel;
    QLabel* m_posLabel;
    QLabel* m_sizeLabel;
    QString m_sourceName;
    QRect m_lastCopyRect;
};

} // namespace mf
