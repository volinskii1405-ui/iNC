#include "imageui/ImageEditor.h"

#include "app/Busy.h"
#include "app/Theme.h"
#include "image/Document.h"
#include "image/Filters.h"
#include "image/ImageIO.h"
#include "image/Transform.h"
#include "imageui/CanvasView.h"
#include "imageui/LayersPanel.h"
#include "imageui/ToolPanels.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QKeyEvent>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStatusBar>
#include <QToolBar>
#include <QUndoView>
#include <QVBoxLayout>

#include <cmath>

namespace mf {

ImageEditor::ImageEditor(QWidget* parent)
    : Workspace(parent)
{
    m_doc = new Document(this);
    m_ctx.doc = m_doc;
    m_ctx.settings = &m_settings;
    m_ctx.repaint = [this] { m_canvas->update(); };
    m_ctx.status = [this](const QString& s) { statusBar()->showMessage(s, 4000); };
    m_ctx.editText = [this](QPoint pos, const std::optional<TextInfo>& t, int layer) { editText(pos, t, layer); };

    m_canvas = new CanvasView(m_doc);
    setCentralWidget(m_canvas);
    buildActions();
    buildLayout();
    buildMenus();

    m_zoomLabel = new QLabel;
    m_posLabel = new QLabel;
    m_sizeLabel = new QLabel;
    for (QLabel* l : {m_posLabel, m_sizeLabel, m_zoomLabel}) {
        l->setMinimumWidth(110);
        statusBar()->addPermanentWidget(l);
    }

    connect(m_canvas, &CanvasView::zoomChanged, this, [this](double z) {
        m_zoomLabel->setText(QStringLiteral("Масштаб: %1 %").arg(z * 100, 0, 'f', z < 0.1 ? 1 : 0));
    });
    connect(m_canvas, &CanvasView::cursorMoved, this, [this](QPoint p, bool inside) {
        if (!inside) {
            m_posLabel->clear();
            return;
        }
        const QColor c = m_doc->sampleComposite(p);
        m_posLabel->setText(QStringLiteral("%1, %2  %3").arg(p.x()).arg(p.y()).arg(c.isValid() ? c.name() : QString()));
    });
    connect(m_canvas, &CanvasView::filesDropped, this, &ImageEditor::openFiles);
    connect(m_doc, &Document::structureChanged, this, &ImageEditor::updateActions);
    connect(m_doc, &Document::selectionChanged, this, &ImageEditor::updateActions);
    connect(m_doc->undoStack(), &QUndoStack::cleanChanged, this, &ImageEditor::titleChanged);
    connect(&m_settings, &ToolSettings::changed, this, [this] { m_canvas->setBrushSize(m_settings.brushSize); });
    m_canvas->setBrushSize(m_settings.brushSize);

    m_doc->reset(QSize(1280, 720), Qt::white);
    m_doc->setFilePath(QString());
    m_sourceName = QStringLiteral("Без имени");
    selectTool(ToolId::Brush);
    updateActions();
}

ImageEditor::~ImageEditor()
{
    m_doc->undoStack()->disconnect(this);
    if (m_tool)
        m_tool->cancel();
    m_canvas->setTool(nullptr);
}

QAction* ImageEditor::act(const QString& text, const QString& iconName, const QKeySequence& key, std::function<void()> fn)
{
    auto* a = new QAction(text, this);
    if (!iconName.isEmpty())
        a->setIcon(icon(iconName));
    if (!key.isEmpty())
        a->setShortcut(key);
    connect(a, &QAction::triggered, this, [fn] { fn(); });
    return a;
}

void ImageEditor::buildActions()
{
    auto& a = m_a;
    a["new"] = act(QStringLiteral("Новое изображение…"), "new", QKeySequence::New, [this] { newImage(); });
    a["open"] = act(QStringLiteral("Открыть…"), "open", QKeySequence::Open, [this] { openDialog(); });
    a["place"] = act(QStringLiteral("Поместить как слой…"), "import", QKeySequence(QStringLiteral("Ctrl+Shift+P")),
                     [this] { placeAsLayer(); });
    a["save"] = act(QStringLiteral("Сохранить проект"), "save", QKeySequence::Save, [this] { saveProject(false); });
    a["saveAs"] = act(QStringLiteral("Сохранить проект как…"), QString(), QKeySequence(QStringLiteral("Ctrl+Shift+S")),
                      [this] { saveProject(true); });
    a["export"] = act(QStringLiteral("Экспорт изображения (PNG, JPG, WEBP, BMP, TIFF)…"), "export",
                      QKeySequence(QStringLiteral("Ctrl+Shift+E")), [this] { exportImage(); });

    a["undo"] = m_doc->undoStack()->createUndoAction(this, QStringLiteral("Отменить"));
    a["undo"]->setShortcut(QKeySequence::Undo);
    a["undo"]->setIcon(icon("undo"));
    a["redo"] = m_doc->undoStack()->createRedoAction(this, QStringLiteral("Повторить"));
    a["redo"]->setShortcuts({QKeySequence(QStringLiteral("Ctrl+Shift+Z")), QKeySequence(QStringLiteral("Ctrl+Y"))});
    a["redo"]->setIcon(icon("redo"));
    a["cut"] = act(QStringLiteral("Вырезать"), QString(), QKeySequence::Cut, [this] { copy(true); });
    a["copy"] = act(QStringLiteral("Копировать"), QString(), QKeySequence::Copy, [this] { copy(false); });
    a["paste"] = act(QStringLiteral("Вставить как слой"), QString(), QKeySequence::Paste, [this] { paste(); });
    a["clear"] = act(QStringLiteral("Очистить выделенное"), "delete", QKeySequence::Delete, [this] { clearSelection(); });
    a["fill"] = act(QStringLiteral("Залить выделенное основным цветом"), "bucket", QKeySequence(QStringLiteral("Alt+Backspace")),
                    [this] { fillSelection(); });

    a["selAll"] = act(QStringLiteral("Выделить всё"), QString(), QKeySequence::SelectAll, [this] {
        QPainterPath p;
        p.addRect(QRectF(m_doc->rect()));
        m_doc->setSelection(p, QStringLiteral("Выделить всё"));
    });
    a["deselect"] = act(QStringLiteral("Снять выделение"), QString(), QKeySequence(QStringLiteral("Ctrl+D")),
                        [this] { m_doc->setSelection(QPainterPath(), QStringLiteral("Снять выделение")); });
    a["invertSel"] = act(QStringLiteral("Инвертировать выделение"), QString(), QKeySequence(QStringLiteral("Ctrl+Shift+I")), [this] {
        QPainterPath all;
        all.addRect(QRectF(m_doc->rect()));
        m_doc->setSelection(m_doc->hasSelection() ? all.subtracted(m_doc->selection()) : QPainterPath(),
                            QStringLiteral("Инверсия выделения"));
    });

    a["imageSize"] = act(QStringLiteral("Размер изображения…"), QString(), QKeySequence(QStringLiteral("Ctrl+Alt+I")),
                         [this] { imageSize(); });
    a["canvasSize"] = act(QStringLiteral("Размер холста…"), QString(), QKeySequence(QStringLiteral("Ctrl+Alt+C")),
                          [this] { canvasSize(); });
    a["rotCW"] = act(QStringLiteral("Повернуть на 90° по часовой"), QString(), QKeySequence(), [this] { rotate(90); });
    a["rotCCW"] = act(QStringLiteral("Повернуть на 90° против часовой"), QString(), QKeySequence(), [this] { rotate(-90); });
    a["rot180"] = act(QStringLiteral("Повернуть на 180°"), QString(), QKeySequence(), [this] { rotate(180); });
    a["rotArb"] = act(QStringLiteral("Повернуть на произвольный угол…"), QString(), QKeySequence(), [this] { rotateArbitrary(); });
    a["flipH"] = act(QStringLiteral("Отразить по горизонтали"), QString(), QKeySequence(), [this] { flip(true); });
    a["flipV"] = act(QStringLiteral("Отразить по вертикали"), QString(), QKeySequence(), [this] { flip(false); });
    a["cropSel"] = act(QStringLiteral("Кадрировать по выделению"), "crop", QKeySequence(), [this] { cropToSelection(); });
    a["flatten"] = act(QStringLiteral("Свести изображение"), QString(), QKeySequence(), [this] { m_doc->flatten(); });

    a["newLayer"] = act(QStringLiteral("Новый слой"), "add", QKeySequence(QStringLiteral("Ctrl+Shift+N")), [this] { m_doc->addLayer(); });
    a["dupLayer"] = act(QStringLiteral("Дублировать слой"), "duplicate", QKeySequence(QStringLiteral("Ctrl+J")),
                        [this] { m_doc->duplicateLayer(m_doc->activeIndex()); });
    a["delLayer"] = act(QStringLiteral("Удалить слой"), "delete", QKeySequence(), [this] { m_doc->deleteLayer(m_doc->activeIndex()); });
    a["layerUp"] = act(QStringLiteral("Поднять слой"), "up", QKeySequence(QStringLiteral("Ctrl+]")),
                       [this] { m_doc->moveLayer(m_doc->activeIndex(), m_doc->activeIndex() + 1); });
    a["layerDown"] = act(QStringLiteral("Опустить слой"), "down", QKeySequence(QStringLiteral("Ctrl+[")),
                         [this] { m_doc->moveLayer(m_doc->activeIndex(), m_doc->activeIndex() - 1); });
    a["merge"] = act(QStringLiteral("Объединить с нижним"), "merge", QKeySequence(QStringLiteral("Ctrl+E")),
                     [this] { m_doc->mergeDown(m_doc->activeIndex()); });
    a["layerFlipH"] = act(QStringLiteral("Отразить слой по горизонтали"), QString(), QKeySequence(), [this] { flipLayer(true); });
    a["layerFlipV"] = act(QStringLiteral("Отразить слой по вертикали"), QString(), QKeySequence(), [this] { flipLayer(false); });
    a["layerTransform"] = act(QStringLiteral("Масштаб и поворот слоя…"), QString(), QKeySequence(QStringLiteral("Ctrl+T")),
                              [this] { transformLayer(); });

    using namespace filters;
    a["brightness"] = act(QStringLiteral("Яркость/контраст…"), QString(), QKeySequence(), [this] {
        filterDialog(QStringLiteral("Яркость/контраст"),
                     {{"b", QStringLiteral("Яркость"), -100, 100, 0}, {"c", QStringLiteral("Контраст"), -100, 100, 0}},
                     [](const QImage& s, const QVariantMap& v) {
                         return brightnessContrast(s, v["b"].toInt(), v["c"].toInt());
                     });
    });
    a["hueSat"] = act(QStringLiteral("Цветовой тон/насыщенность…"), QString(), QKeySequence(QStringLiteral("Ctrl+U")), [this] {
        filterDialog(QStringLiteral("Цветовой тон/насыщенность"),
                     {{"h", QStringLiteral("Цветовой тон"), -180, 180, 0, 0, QStringLiteral("°")},
                      {"s", QStringLiteral("Насыщенность"), -100, 100, 0},
                      {"l", QStringLiteral("Яркость"), -100, 100, 0}},
                     [](const QImage& s, const QVariantMap& v) {
                         return hueSaturation(s, v["h"].toInt(), v["s"].toInt(), v["l"].toInt());
                     });
    });
    a["levels"] = act(QStringLiteral("Уровни…"), QString(), QKeySequence(QStringLiteral("Ctrl+L")), [this] {
        FilterParam ch{"ch", QStringLiteral("Канал"), 0, 3, 0};
        ch.choices = {QStringLiteral("RGB"), QStringLiteral("Красный"), QStringLiteral("Зелёный"), QStringLiteral("Синий")};
        filterDialog(QStringLiteral("Уровни"),
                     {ch,
                      {"ib", QStringLiteral("Вход: чёрная точка"), 0, 254, 0},
                      {"iw", QStringLiteral("Вход: белая точка"), 1, 255, 255},
                      {"g", QStringLiteral("Гамма"), 0.1, 9.99, 1.0, 2},
                      {"ob", QStringLiteral("Выход: чёрный"), 0, 255, 0},
                      {"ow", QStringLiteral("Выход: белый"), 0, 255, 255}},
                     [](const QImage& s, const QVariantMap& v) {
                         Levels lv;
                         lv.inBlack = v["ib"].toInt();
                         lv.inWhite = std::max(lv.inBlack + 1, v["iw"].toInt());
                         lv.gamma = v["g"].toDouble();
                         lv.outBlack = v["ob"].toInt();
                         lv.outWhite = v["ow"].toInt();
                         return levels(s, lv, Channel(v["ch"].toInt()));
                     });
    });
    a["curves"] = act(QStringLiteral("Кривые…"), QString(), QKeySequence(QStringLiteral("Ctrl+M")), [this] {
        if (!requireDocument())
            return;
        CurvesDialog dlg(m_doc, this);
        dlg.exec();
    });
    a["desaturate"] = act(QStringLiteral("Обесцветить (Ч/Б)"), QString(), QKeySequence(QStringLiteral("Ctrl+Shift+U")),
                          [this] { runFilter(QStringLiteral("Чёрно-белое"), grayscale); });
    a["sepia"] = act(QStringLiteral("Сепия"), QString(), QKeySequence(), [this] { runFilter(QStringLiteral("Сепия"), sepia); });
    a["invert"] = act(QStringLiteral("Инверсия"), QString(), QKeySequence(QStringLiteral("Ctrl+I")),
                      [this] { runFilter(QStringLiteral("Инверсия"), invert); });
    a["blur"] = act(QStringLiteral("Размытие по Гауссу…"), QString(), QKeySequence(), [this] {
        filterDialog(QStringLiteral("Размытие по Гауссу"), {{"r", QStringLiteral("Радиус"), 0.5, 150, 4, 1, QStringLiteral(" px")}},
                     [](const QImage& s, const QVariantMap& v) { return gaussianBlur(s, v["r"].toDouble()); });
    });
    a["sharpen"] = act(QStringLiteral("Резкость (нерезкая маска)…"), QString(), QKeySequence(), [this] {
        filterDialog(QStringLiteral("Резкость"),
                     {{"a", QStringLiteral("Эффект"), 0, 5, 1.0, 2}, {"r", QStringLiteral("Радиус"), 0.5, 30, 1.5, 1, QStringLiteral(" px")}},
                     [](const QImage& s, const QVariantMap& v) { return sharpen(s, v["a"].toDouble(), v["r"].toDouble()); });
    });
    a["noise"] = act(QStringLiteral("Добавить шум…"), QString(), QKeySequence(), [this] {
        FilterParam mono{"m", QStringLiteral("Монохромный"), 0, 1, 0};
        mono.isBool = true;
        filterDialog(QStringLiteral("Шум"), {{"a", QStringLiteral("Количество"), 0, 100, 20, 0, QStringLiteral(" %")}, mono},
                     [](const QImage& s, const QVariantMap& v) { return addNoise(s, v["a"].toInt(), v["m"].toBool(), 12345); });
    });
    a["pixelate"] = act(QStringLiteral("Пикселизация…"), QString(), QKeySequence(), [this] {
        filterDialog(QStringLiteral("Пикселизация"), {{"c", QStringLiteral("Размер ячейки"), 2, 200, 10, 0, QStringLiteral(" px")}},
                     [](const QImage& s, const QVariantMap& v) { return pixelate(s, v["c"].toInt()); });
    });

    a["zoomIn"] = act(QStringLiteral("Увеличить"), "zoom-in", QKeySequence::ZoomIn, [this] { m_canvas->zoomIn(); });
    a["zoomIn"]->setShortcuts({QKeySequence::ZoomIn, QKeySequence(QStringLiteral("Ctrl+="))});
    a["zoomOut"] = act(QStringLiteral("Уменьшить"), "zoom-out", QKeySequence::ZoomOut, [this] { m_canvas->zoomOut(); });
    a["fit"] = act(QStringLiteral("По размеру окна"), "fit", QKeySequence(QStringLiteral("Ctrl+0")), [this] { m_canvas->fitToWindow(); });
    a["actual"] = act(QStringLiteral("Реальный размер (100 %)"), QString(), QKeySequence(QStringLiteral("Ctrl+1")),
                      [this] { m_canvas->actualSize(); });

    a["swap"] = act(QStringLiteral("Поменять цвета местами"), "swap", QKeySequence(QStringLiteral("X")), [this] { m_settings.swapColors(); });
    a["resetColors"] = act(QStringLiteral("Цвета по умолчанию"), QString(), QKeySequence(QStringLiteral("D")),
                           [this] { m_settings.resetColors(); });
    a["brushBigger"] = act(QStringLiteral("Увеличить кисть"), QString(), QKeySequence(QStringLiteral("]")), [this] {
        m_settings.brushSize = std::min(2000, int(std::ceil(m_settings.brushSize * 1.2 + 1)));
        m_settings.notify();
    });
    a["brushSmaller"] = act(QStringLiteral("Уменьшить кисть"), QString(), QKeySequence(QStringLiteral("[")), [this] {
        m_settings.brushSize = std::max(1, int(std::floor(m_settings.brushSize / 1.2)));
        m_settings.notify();
    });

    m_toolGroup = new QActionGroup(this);
    for (ToolId id : {ToolId::Move, ToolId::RectSelect, ToolId::EllipseSelect, ToolId::Lasso, ToolId::Crop, ToolId::Brush,
                      ToolId::Eraser, ToolId::Fill, ToolId::Gradient, ToolId::Text, ToolId::Picker, ToolId::Hand}) {
        auto* ta = new QAction(icon(toolIcon(id)), QStringLiteral("%1 (%2)").arg(toolName(id), toolShortcut(id)), this);
        ta->setCheckable(true);
        ta->setShortcut(QKeySequence(toolShortcut(id)));
        m_toolGroup->addAction(ta);
        connect(ta, &QAction::triggered, this, [this, id] { selectTool(id); });
        m_toolActions[int(id)] = ta;
    }
}

void ImageEditor::buildMenus()
{
    auto& a = m_a;
    auto* file = new QMenu(QStringLiteral("&Файл"), this);
    file->addActions({a["new"], a["open"], a["place"]});
    file->addSeparator();
    file->addActions({a["save"], a["saveAs"], a["export"]});

    auto* edit = new QMenu(QStringLiteral("&Правка"), this);
    edit->addActions({a["undo"], a["redo"]});
    edit->addSeparator();
    edit->addActions({a["cut"], a["copy"], a["paste"]});
    edit->addSeparator();
    edit->addActions({a["clear"], a["fill"]});
    edit->addSeparator();
    edit->addActions({a["swap"], a["resetColors"], a["brushBigger"], a["brushSmaller"]});

    auto* select = new QMenu(QStringLiteral("В&ыделение"), this);
    select->addActions({a["selAll"], a["deselect"], a["invertSel"]});

    auto* image = new QMenu(QStringLiteral("&Изображение"), this);
    image->addActions({a["imageSize"], a["canvasSize"]});
    image->addSeparator();
    image->addActions({a["rotCW"], a["rotCCW"], a["rot180"], a["rotArb"]});
    image->addSeparator();
    image->addActions({a["flipH"], a["flipV"]});
    image->addSeparator();
    image->addActions({a["cropSel"], a["flatten"]});
    auto* adj = image->addMenu(QStringLiteral("Коррекция"));
    adj->addActions({a["brightness"], a["hueSat"], a["levels"], a["curves"]});
    adj->addSeparator();
    adj->addActions({a["desaturate"], a["sepia"], a["invert"]});

    auto* layer = new QMenu(QStringLiteral("&Слой"), this);
    layer->addActions({a["newLayer"], a["dupLayer"], a["delLayer"]});
    layer->addSeparator();
    layer->addActions({a["layerUp"], a["layerDown"], a["merge"]});
    layer->addSeparator();
    layer->addActions({a["layerTransform"], a["layerFlipH"], a["layerFlipV"]});

    auto* filt = new QMenu(QStringLiteral("Фи&льтры"), this);
    filt->addActions({a["blur"], a["sharpen"], a["noise"], a["pixelate"]});
    filt->addSeparator();
    filt->addActions({a["desaturate"], a["sepia"], a["invert"]});

    auto* view = new QMenu(QStringLiteral("&Вид"), this);
    view->addActions({a["zoomIn"], a["zoomOut"], a["fit"], a["actual"]});

    auto* tools = new QMenu(QStringLiteral("&Инструменты"), this);
    tools->addActions(m_toolGroup->actions());

    m_menus = {file, edit, image, layer, select, filt, tools, view};
}

void ImageEditor::buildLayout()
{
    auto* toolbar = new QToolBar(QStringLiteral("Инструменты"), this);
    toolbar->setObjectName("imageTools");
    toolbar->setOrientation(Qt::Vertical);
    toolbar->setMovable(false);
    toolbar->setIconSize(QSize(24, 24));
    toolbar->addActions(m_toolGroup->actions());
    toolbar->addSeparator();
    toolbar->addWidget(new ColorSwatch(&m_settings));
    addToolBar(Qt::LeftToolBarArea, toolbar);

    auto* top = new QToolBar(QStringLiteral("Параметры"), this);
    top->setObjectName("imageOptions");
    top->setMovable(false);
    top->setIconSize(QSize(20, 20));
    top->addActions({m_a["new"], m_a["open"], m_a["save"], m_a["export"]});
    top->addSeparator();
    top->addActions({m_a["undo"], m_a["redo"]});
    top->addSeparator();
    m_options = new ToolOptionsBar(&m_settings);
    top->addWidget(m_options);
    addToolBar(Qt::TopToolBarArea, top);
    connect(m_options, &ToolOptionsBar::cropApply, this, [this] {
        QKeyEvent e(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
        if (m_tool)
            m_tool->key(&e);
    });
    connect(m_options, &ToolOptionsBar::cropCancel, this, [this] {
        QKeyEvent e(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        if (m_tool)
            m_tool->key(&e);
    });

    m_layers = new LayersPanel(m_doc);
    m_layers->setActions(m_a["newLayer"], m_a["dupLayer"], m_a["delLayer"], m_a["layerUp"], m_a["layerDown"], m_a["merge"]);
    auto* layersDock = new QDockWidget(QStringLiteral("Слои"), this);
    layersDock->setObjectName("layersDock");
    layersDock->setWidget(m_layers);
    layersDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    addDockWidget(Qt::RightDockWidgetArea, layersDock);

    auto* history = new QUndoView(m_doc->undoStack());
    history->setEmptyLabel(QStringLiteral("Исходное состояние"));
    auto* historyDock = new QDockWidget(QStringLiteral("История"), this);
    historyDock->setObjectName("historyDock");
    historyDock->setWidget(history);
    historyDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    addDockWidget(Qt::RightDockWidgetArea, historyDock);
    resizeDocks({layersDock, historyDock}, {320, 180}, Qt::Vertical);
    resizeDocks({layersDock}, {270}, Qt::Horizontal);
}

void ImageEditor::selectTool(ToolId id)
{
    if (m_tool)
        m_tool->cancel();
    m_tool = createTool(id, &m_ctx);
    m_canvas->setTool(m_tool.get());
    m_options->showTool(id);
    if (QAction* a = m_toolActions.value(int(id)))
        a->setChecked(true);
}

bool ImageEditor::requireDocument()
{
    return !m_doc->isEmpty() && !m_doc->isEditing();
}

void ImageEditor::updateActions()
{
    const bool has = !m_doc->isEmpty();
    const int n = m_doc->layerCount(), i = m_doc->activeIndex();
    m_a["delLayer"]->setEnabled(n > 1);
    m_a["layerUp"]->setEnabled(has && i < n - 1);
    m_a["layerDown"]->setEnabled(has && i > 0);
    m_a["merge"]->setEnabled(has && i > 0);
    m_a["cropSel"]->setEnabled(m_doc->hasSelection());
    m_a["deselect"]->setEnabled(m_doc->hasSelection());
    m_a["fill"]->setEnabled(has);
    m_sizeLabel->setText(QStringLiteral("%1 × %2 px").arg(m_doc->size().width()).arg(m_doc->size().height()));
}

QString ImageEditor::documentTitle() const
{
    const QString name = m_doc->filePath().isEmpty() ? m_sourceName : QFileInfo(m_doc->filePath()).fileName();
    return m_doc->isModified() ? name + QStringLiteral(" *") : name;
}

bool ImageEditor::confirmClose()
{
    if (!m_doc->isModified())
        return true;
    const auto r = QMessageBox::question(this, QStringLiteral("Несохранённые изменения"),
                                         QStringLiteral("Изображение «%1» изменено. Сохранить проект?").arg(m_sourceName),
                                         QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (r == QMessageBox::Cancel)
        return false;
    if (r == QMessageBox::Save)
        return saveProject(false);
    return true;
}

void ImageEditor::newImage()
{
    if (!confirmClose())
        return;
    NewImageSpec spec;
    if (!askNewImage(this, m_settings.background, m_settings.foreground, &spec))
        return;
    if (qint64(spec.size.width()) * spec.size.height() > 400LL * 1000 * 1000) {
        QMessageBox::warning(this, QStringLiteral("Слишком большое изображение"),
                             QStringLiteral("Максимум — 400 мегапикселей."));
        return;
    }
    if (m_tool)
        m_tool->cancel();
    m_doc->reset(spec.size, spec.background);
    m_doc->setFilePath(QString());
    m_sourceName = QStringLiteral("Без имени");
    emit titleChanged();
}

void ImageEditor::openImage(const QImage& image, const QString& name)
{
    if (!confirmClose())
        return;
    if (m_tool)
        m_tool->cancel();
    m_doc->resetFromImage(image, QStringLiteral("Фон"));
    m_doc->setFilePath(QString());
    m_sourceName = name;
    emit titleChanged();
}

bool ImageEditor::openPath(const QString& path)
{
    if (QFileInfo(path).suffix().compare(QLatin1String(project::kSuffix), Qt::CaseInsensitive) == 0) {
        DocState st;
        QString err;
        const bool ok = runBusy<bool>(this, QStringLiteral("Открытие проекта…"), [&] { return project::load(path, &st, &err); });
        if (!ok) {
            QMessageBox::critical(this, QStringLiteral("Не удалось открыть проект"), err);
            return false;
        }
        if (m_tool)
            m_tool->cancel();
        m_doc->resetFromState(st);
        m_doc->setFilePath(path);
        m_sourceName = QFileInfo(path).completeBaseName();
        emit titleChanged();
        return true;
    }
    const auto res = runBusy<imageio::LoadResult>(this, QStringLiteral("Открытие изображения…"), [path] { return imageio::loadImage(path); });
    if (!res.error.isEmpty()) {
        QMessageBox::critical(this, QStringLiteral("Не удалось открыть изображение"),
                              QStringLiteral("%1\n\n%2").arg(QFileInfo(path).fileName(), res.error));
        return false;
    }
    if (m_tool)
        m_tool->cancel();
    m_doc->resetFromImage(res.image, QStringLiteral("Фон"));
    m_doc->setFilePath(QString());
    m_sourceName = QFileInfo(path).fileName();
    emit titleChanged();
    statusBar()->showMessage(QStringLiteral("Открыто: %1").arg(path), 4000);
    return true;
}

void ImageEditor::openDialog()
{
    if (!confirmClose())
        return;
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Открыть изображение или проект"),
                                                      QStandardPaths::writableLocation(QStandardPaths::PicturesLocation),
                                                      imageio::openFilter());
    if (!path.isEmpty())
        openPath(path);
}

void ImageEditor::openFiles(const QStringList& paths)
{
    QStringList rest = paths;
    if (rest.isEmpty())
        return;
    if (!m_doc->isModified() || m_doc->isEmpty()) {
        if (!openPath(rest.takeFirst()))
            return;
    }
    for (const QString& p : rest) {
        const auto res = imageio::loadImage(p);
        if (!res.error.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("Файл пропущен"), QStringLiteral("%1\n\n%2").arg(p, res.error));
            continue;
        }
        m_doc->addLayer(QFileInfo(p).completeBaseName(), res.image);
    }
}

void ImageEditor::placeAsLayer()
{
    if (!requireDocument())
        return;
    const QStringList paths = QFileDialog::getOpenFileNames(this, QStringLiteral("Поместить изображения как слои"),
                                                            QString(), imageio::openFilter());
    for (const QString& p : paths) {
        const auto res = imageio::loadImage(p);
        if (!res.error.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("Файл пропущен"), QStringLiteral("%1\n\n%2").arg(p, res.error));
            continue;
        }
        m_doc->addLayer(QFileInfo(p).completeBaseName(), res.image);
    }
}

bool ImageEditor::saveProject(bool saveAs)
{
    if (m_doc->isEmpty())
        return false;
    QString path = m_doc->filePath();
    if (path.isEmpty() || saveAs) {
        QString base = QFileInfo(m_sourceName).completeBaseName();
        if (base.isEmpty())
            base = QStringLiteral("project");
        path = QFileDialog::getSaveFileName(this, QStringLiteral("Сохранить проект"),
                                            QDir(QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)).filePath(base + ".mfp"),
                                            QStringLiteral("Проект MediaForge (*.mfp)"));
        if (path.isEmpty())
            return false;
        if (!path.endsWith(QLatin1String(".mfp"), Qt::CaseInsensitive))
            path += QStringLiteral(".mfp");
    }
    QString err;
    const DocState st = m_doc->state();
    const bool ok = runBusy<bool>(this, QStringLiteral("Сохранение проекта…"), [&] { return project::save(st, path, &err); });
    if (!ok) {
        QMessageBox::critical(this, QStringLiteral("Ошибка сохранения"), err);
        return false;
    }
    m_doc->setFilePath(path);
    m_sourceName = QFileInfo(path).completeBaseName();
    m_doc->markClean();
    emit titleChanged();
    statusBar()->showMessage(QStringLiteral("Проект сохранён: %1").arg(path), 4000);
    return true;
}

void ImageEditor::exportImage()
{
    if (!requireDocument())
        return;
    QString base = QFileInfo(m_sourceName).completeBaseName();
    if (base.isEmpty())
        base = QStringLiteral("image");
    QString selected;
    QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Экспорт изображения"),
                                                QDir(QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)).filePath(base + ".png"),
                                                imageio::exportFilter(), &selected);
    if (path.isEmpty())
        return;
    if (QFileInfo(path).suffix().isEmpty()) {
        const QRegularExpression rx(QStringLiteral("\\*\\.(\\w+)"));
        const auto m = rx.match(selected);
        path += "." + (m.hasMatch() ? m.captured(1) : QStringLiteral("png"));
    }
    const QString suffix = QFileInfo(path).suffix().toLower();
    int quality = 92;
    if (suffix == "jpg" || suffix == "jpeg" || suffix == "webp") {
        bool ok = false;
        quality = QInputDialog::getInt(this, QStringLiteral("Качество"), QStringLiteral("Качество (1–100):"), 92, 1, 100, 1, &ok);
        if (!ok)
            return;
    }
    const QImage img = m_doc->composite();
    QString err;
    const bool ok = runBusy<bool>(this, QStringLiteral("Экспорт…"), [&] { return imageio::saveImage(img, path, quality, &err); });
    if (!ok) {
        QMessageBox::critical(this, QStringLiteral("Ошибка экспорта"), err);
        return;
    }
    statusBar()->showMessage(QStringLiteral("Экспортировано: %1").arg(path), 5000);
}

void ImageEditor::copy(bool cut)
{
    if (!requireDocument())
        return;
    const QImage layer = m_doc->layer(m_doc->activeIndex()).image;
    QImage out;
    QRect r = m_doc->rect();
    if (m_doc->hasSelection()) {
        r = m_doc->selectionBounds();
        if (r.isEmpty())
            return;
        out = filters::blendWithMask(makeLayerImage(layer.size()), layer, m_doc->selectionMask()).copy(r);
    } else {
        out = layer;
    }
    m_lastCopyRect = r;
    QApplication::clipboard()->setImage(out);
    if (cut)
        clearSelection();
    statusBar()->showMessage(cut ? QStringLiteral("Вырезано в буфер обмена") : QStringLiteral("Скопировано в буфер обмена"), 3000);
}

void ImageEditor::paste()
{
    const QImage img = QApplication::clipboard()->image();
    if (img.isNull()) {
        statusBar()->showMessage(QStringLiteral("В буфере обмена нет изображения"), 3000);
        return;
    }
    if (m_doc->isEmpty()) {
        openImage(img, QStringLiteral("Вставка"));
        return;
    }
    QImage placed = img.convertToFormat(kLayerFormat);
    if (img.size() == m_lastCopyRect.size() && img.size() != m_doc->size()) {
        placed = makeLayerImage(m_doc->size());
        QPainter p(&placed);
        p.drawImage(m_lastCopyRect.topLeft(), img);
    }
    m_doc->addLayer(QStringLiteral("Вставленный слой"), placed);
}

void ImageEditor::clearSelection()
{
    if (!requireDocument())
        return;
    const int l = m_doc->activeIndex();
    const QImage mask = m_doc->selectionMask();
    if (mask.isNull()) {
        m_doc->applyLayerImage(l, makeLayerImage(m_doc->size()), QStringLiteral("Очистка слоя"));
        return;
    }
    m_doc->applyLayerImage(l, filters::clearWithMask(m_doc->layer(l).image, mask), QStringLiteral("Очистка"));
}

void ImageEditor::fillSelection()
{
    if (!requireDocument())
        return;
    const int l = m_doc->activeIndex();
    m_doc->applyLayerImage(l, filters::fillWithMask(m_doc->layer(l).image, m_doc->selectionMask(), m_settings.foreground, 1.0),
                           QStringLiteral("Заливка выделения"));
}

void ImageEditor::imageSize()
{
    if (!requireDocument())
        return;
    QSize s;
    bool smooth = true;
    if (!askImageSize(this, m_doc->size(), &s, &smooth) || s == m_doc->size())
        return;
    const DocState st = m_doc->state();
    const DocState res = runBusy<DocState>(this, QStringLiteral("Масштабирование…"),
                                           [=] { return transform::scaleImage(st, s, smooth); });
    m_doc->commitState(res, QStringLiteral("Размер изображения"));
}

void ImageEditor::canvasSize()
{
    if (!requireDocument())
        return;
    QSize s;
    int anchor = 4;
    bool fill = false;
    if (!askCanvasSize(this, m_doc->size(), &s, &anchor, &fill) || s == m_doc->size())
        return;
    const QPoint off = transform::anchorOffset(m_doc->size(), s, anchor);
    m_doc->commitState(transform::resizeCanvas(m_doc->state(), s, off, fill ? m_settings.background : QColor(Qt::transparent)),
                       QStringLiteral("Размер холста"));
}

void ImageEditor::rotate(double degrees)
{
    if (!requireDocument())
        return;
    const DocState st = m_doc->state();
    const DocState res = runBusy<DocState>(this, QStringLiteral("Поворот…"), [=] { return transform::rotate(st, degrees); });
    m_doc->commitState(res, QStringLiteral("Поворот холста"));
}

void ImageEditor::rotateArbitrary()
{
    bool ok = false;
    const double deg = QInputDialog::getDouble(this, QStringLiteral("Поворот"), QStringLiteral("Угол (по часовой, градусы):"),
                                               15, -360, 360, 1, &ok);
    if (ok)
        rotate(deg);
}

void ImageEditor::flip(bool horizontal)
{
    if (!requireDocument())
        return;
    m_doc->commitState(transform::flip(m_doc->state(), horizontal), QStringLiteral("Отражение"));
}

void ImageEditor::cropToSelection()
{
    if (!requireDocument() || !m_doc->hasSelection())
        return;
    DocState st = transform::crop(m_doc->state(), m_doc->selectionBounds());
    st.selection = QPainterPath();
    m_doc->commitState(st, QStringLiteral("Кадрирование по выделению"));
}

void ImageEditor::flipLayer(bool horizontal)
{
    if (!requireDocument())
        return;
    const int l = m_doc->activeIndex();
    m_doc->applyLayerImage(l, transform::flipLayer(m_doc->layer(l).image, horizontal), QStringLiteral("Отражение слоя"));
}

void ImageEditor::transformLayer()
{
    FilterParam keep{"k", QStringLiteral("Сохранять пропорции"), 0, 1, 1};
    keep.isBool = true;
    filterDialog(QStringLiteral("Масштаб и поворот слоя"),
                 {{"sx", QStringLiteral("Ширина"), 1, 1000, 100, 0, QStringLiteral(" %")},
                  {"sy", QStringLiteral("Высота"), 1, 1000, 100, 0, QStringLiteral(" %")},
                  keep,
                  {"a", QStringLiteral("Поворот"), -360, 360, 0, 1, QStringLiteral("°")}},
                 [](const QImage& s, const QVariantMap& v) {
                     const double sx = v["sx"].toDouble() / 100.0;
                     const double sy = v["k"].toBool() ? sx : v["sy"].toDouble() / 100.0;
                     return transform::transformLayer(s, sx, sy, v["a"].toDouble());
                 },
                 false);
}

void ImageEditor::runFilter(const QString& title, std::function<QImage(const QImage&)> fn)
{
    if (!requireDocument())
        return;
    const int l = m_doc->activeIndex();
    const QImage src = m_doc->layer(l).image;
    const QImage mask = m_doc->selectionMask();
    const QImage res = runBusy<QImage>(this, QStringLiteral("%1…").arg(title), [=] {
        const QImage r = fn(src);
        return mask.isNull() ? r : filters::blendWithMask(src, r, mask);
    });
    m_doc->applyLayerImage(l, res, title);
}

void ImageEditor::filterDialog(const QString& title, const QVector<FilterParam>& params,
                               std::function<QImage(const QImage&, const QVariantMap&)> fn, bool respectSelection)
{
    if (!requireDocument())
        return;
    FilterDialog dlg(m_doc, title, params, std::move(fn), this, respectSelection);
    dlg.exec();
}

void ImageEditor::editText(QPoint pos, const std::optional<TextInfo>& existing, int layer)
{
    TextInfo init;
    if (existing) {
        init = *existing;
    } else {
        init.font = QFont(QStringLiteral("DejaVu Sans"));
        init.font.setPixelSize(std::max(12, m_doc->size().height() / 15));
        init.color = m_settings.foreground;
        init.pos = pos;
    }
    TextDialog dlg(init, this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    TextInfo t = dlg.result();
    t.pos = init.pos;
    if (t.text.trimmed().isEmpty())
        return;
    if (layer >= 0)
        m_doc->updateTextLayer(layer, t);
    else
        m_doc->addTextLayer(t);
}

} // namespace mf
