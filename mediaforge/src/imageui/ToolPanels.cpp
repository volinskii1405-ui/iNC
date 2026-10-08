#include "imageui/ToolPanels.h"

#include "app/Theme.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>

namespace mf {

// ============================================================ ColorSwatch

ColorSwatch::ColorSwatch(ToolSettings* settings, QWidget* parent)
    : QWidget(parent), m_settings(settings)
{
    setFixedSize(56, 60);
    setToolTip(QStringLiteral("Основной и фоновый цвета. Щелчок — выбрать, X — поменять местами, D — по умолчанию"));
    connect(m_settings, &ToolSettings::colorsChanged, this, qOverload<>(&QWidget::update));
}

void ColorSwatch::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    auto square = [&](QRect r, QColor c) {
        p.fillRect(r, Qt::black);
        p.fillRect(r.adjusted(1, 1, -1, -1), Qt::white);
        p.fillRect(r.adjusted(2, 2, -2, -2), c);
    };
    square(bgRect(), m_settings->background);
    square(fgRect(), m_settings->foreground);
    icon(QStringLiteral("swap")).paint(&p, swapRect());
    p.fillRect(resetRect().adjusted(5, 5, 0, 0), Qt::white);
    p.fillRect(QRect(resetRect().topLeft(), QSize(10, 10)), Qt::black);
    p.setPen(QColor(200, 200, 200));
    p.drawRect(QRect(resetRect().topLeft(), QSize(10, 10)));
}

void ColorSwatch::mousePressEvent(QMouseEvent* e)
{
    const QPoint pt = e->position().toPoint();
    if (swapRect().contains(pt)) {
        m_settings->swapColors();
    } else if (resetRect().contains(pt)) {
        m_settings->resetColors();
    } else if (fgRect().contains(pt)) {
        const QColor c = QColorDialog::getColor(m_settings->foreground, this, QStringLiteral("Основной цвет"),
                                                QColorDialog::DontUseNativeDialog);
        m_settings->setForeground(c);
    } else if (bgRect().contains(pt)) {
        const QColor c = QColorDialog::getColor(m_settings->background, this, QStringLiteral("Фоновый цвет"),
                                                QColorDialog::DontUseNativeDialog);
        m_settings->setBackground(c);
    }
}

// ============================================================ ToolOptionsBar

namespace {
QHBoxLayout* row(QWidget* w)
{
    auto* l = new QHBoxLayout(w);
    l->setContentsMargins(6, 0, 6, 0);
    l->setSpacing(8);
    return l;
}
QSpinBox* spin(int min, int max, const QString& suffix)
{
    auto* s = new QSpinBox;
    s->setRange(min, max);
    s->setSuffix(suffix);
    s->setKeyboardTracking(false);
    return s;
}
} // namespace

ToolOptionsBar::ToolOptionsBar(ToolSettings* settings, QWidget* parent)
    : QWidget(parent), m_settings(settings)
{
    auto* l = new QHBoxLayout(this);
    l->setContentsMargins(0, 2, 0, 2);
    m_stack = new QStackedWidget;
    l->addWidget(m_stack);
    l->addStretch();

    auto add = [this](QWidget* page, std::initializer_list<ToolId> tools) {
        const int idx = m_stack->addWidget(page);
        for (ToolId t : tools)
            m_pageForTool[int(t)] = idx;
    };
    add(brushPage(false), {ToolId::Brush});
    add(brushPage(true), {ToolId::Eraser});
    add(fillPage(), {ToolId::Fill});
    add(pickerPage(), {ToolId::Picker});
    add(selectPage(), {ToolId::RectSelect, ToolId::EllipseSelect, ToolId::Lasso});
    add(gradientPage(), {ToolId::Gradient});
    add(cropPage(), {ToolId::Crop});
    add(hintPage(QStringLiteral("Перетащите, чтобы сдвинуть слой или выделенную область. Стрелки — на 1 px, "
                                "Shift+стрелки — на 10 px, Shift при перетаскивании — по оси.")),
        {ToolId::Move});
    add(hintPage(QStringLiteral("Щелчок по холсту — новый текстовый слой; щелчок по существующему тексту — "
                                "редактирование.")),
        {ToolId::Text});
    add(hintPage(QStringLiteral("Перетаскивайте холст. Пробел — временная рука в любом инструменте, "
                                "колесо — прокрутка, Ctrl+колесо — масштаб.")),
        {ToolId::Hand});
    connect(m_settings, &ToolSettings::changed, this, &ToolOptionsBar::syncFromSettings);
}

void ToolOptionsBar::showTool(ToolId id)
{
    m_stack->setCurrentIndex(m_pageForTool.value(int(id), 0));
}

void ToolOptionsBar::syncFromSettings()
{
    for (auto& f : m_syncers)
        f();
}

QWidget* ToolOptionsBar::brushPage(bool eraser)
{
    auto* w = new QWidget;
    auto* l = row(w);
    auto* size = spin(1, 2000, QStringLiteral(" px"));
    auto* hard = spin(0, 100, QStringLiteral(" %"));
    auto* op = spin(1, 100, QStringLiteral(" %"));
    l->addWidget(new QLabel(eraser ? QStringLiteral("Ластик — размер:") : QStringLiteral("Кисть — размер:")));
    l->addWidget(size);
    l->addWidget(new QLabel(QStringLiteral("Жёсткость:")));
    l->addWidget(hard);
    l->addWidget(new QLabel(QStringLiteral("Непрозрачность:")));
    l->addWidget(op);
    l->addWidget(new QLabel(QStringLiteral("  [ и ] — размер кисти")));
    connect(size, &QSpinBox::valueChanged, this, [this](int v) {
        m_settings->brushSize = v;
        m_settings->notify();
    });
    connect(hard, &QSpinBox::valueChanged, this, [this](int v) { m_settings->hardness = v; });
    connect(op, &QSpinBox::valueChanged, this, [this](int v) { m_settings->opacity = v; });
    m_syncers << [=] {
        for (auto [s, v] : {std::pair{size, m_settings->brushSize}, {hard, m_settings->hardness}, {op, m_settings->opacity}}) {
            s->blockSignals(true);
            s->setValue(v);
            s->blockSignals(false);
        }
    };
    m_syncers.last()();
    return w;
}

QWidget* ToolOptionsBar::fillPage()
{
    auto* w = new QWidget;
    auto* l = row(w);
    auto* tol = spin(0, 255, QString());
    auto* op = spin(1, 100, QStringLiteral(" %"));
    auto* cont = new QCheckBox(QStringLiteral("Смежные пиксели"));
    auto* merged = new QCheckBox(QStringLiteral("По всем слоям"));
    l->addWidget(new QLabel(QStringLiteral("Допуск:")));
    l->addWidget(tol);
    l->addWidget(new QLabel(QStringLiteral("Непрозрачность:")));
    l->addWidget(op);
    l->addWidget(cont);
    l->addWidget(merged);
    connect(tol, &QSpinBox::valueChanged, this, [this](int v) { m_settings->tolerance = v; });
    connect(op, &QSpinBox::valueChanged, this, [this](int v) { m_settings->opacity = v; });
    connect(cont, &QCheckBox::toggled, this, [this](bool v) { m_settings->contiguous = v; });
    connect(merged, &QCheckBox::toggled, this, [this](bool v) { m_settings->sampleMerged = v; });
    m_syncers << [=] {
        tol->blockSignals(true);
        tol->setValue(m_settings->tolerance);
        tol->blockSignals(false);
        op->blockSignals(true);
        op->setValue(m_settings->opacity);
        op->blockSignals(false);
        cont->setChecked(m_settings->contiguous);
        merged->setChecked(m_settings->sampleMerged);
    };
    m_syncers.last()();
    return w;
}

QWidget* ToolOptionsBar::pickerPage()
{
    auto* w = new QWidget;
    auto* l = row(w);
    auto* merged = new QCheckBox(QStringLiteral("Брать цвет со всех слоёв"));
    l->addWidget(merged);
    l->addWidget(new QLabel(QStringLiteral("Левая кнопка — основной цвет, правая — фоновый")));
    connect(merged, &QCheckBox::toggled, this, [this](bool v) { m_settings->sampleMerged = v; });
    m_syncers << [=] { merged->setChecked(m_settings->sampleMerged); };
    m_syncers.last()();
    return w;
}

QWidget* ToolOptionsBar::selectPage()
{
    auto* w = new QWidget;
    auto* l = row(w);
    auto* mode = new QComboBox;
    mode->addItems({QStringLiteral("Новое выделение"), QStringLiteral("Добавить"), QStringLiteral("Вычесть"),
                    QStringLiteral("Пересечь")});
    l->addWidget(new QLabel(QStringLiteral("Режим:")));
    l->addWidget(mode);
    l->addWidget(new QLabel(QStringLiteral("Shift — добавить, Alt/Ctrl — вычесть, щелчок без перетаскивания — снять")));
    connect(mode, &QComboBox::currentIndexChanged, this, [this](int i) { m_settings->selectMode = SelectMode(i); });
    return w;
}

QWidget* ToolOptionsBar::gradientPage()
{
    auto* w = new QWidget;
    auto* l = row(w);
    auto* type = new QComboBox;
    type->addItems({QStringLiteral("Линейный"), QStringLiteral("Радиальный")});
    auto* colors = new QComboBox;
    colors->addItems({QStringLiteral("Основной → фоновый"), QStringLiteral("Основной → прозрачный")});
    auto* op = spin(1, 100, QStringLiteral(" %"));
    l->addWidget(new QLabel(QStringLiteral("Тип:")));
    l->addWidget(type);
    l->addWidget(new QLabel(QStringLiteral("Цвета:")));
    l->addWidget(colors);
    l->addWidget(new QLabel(QStringLiteral("Непрозрачность:")));
    l->addWidget(op);
    connect(type, &QComboBox::currentIndexChanged, this, [this](int i) { m_settings->radialGradient = i == 1; });
    connect(colors, &QComboBox::currentIndexChanged, this, [this](int i) { m_settings->gradientToTransparent = i == 1; });
    connect(op, &QSpinBox::valueChanged, this, [this](int v) { m_settings->opacity = v; });
    m_syncers << [=] {
        op->blockSignals(true);
        op->setValue(m_settings->opacity);
        op->blockSignals(false);
    };
    m_syncers.last()();
    return w;
}

QWidget* ToolOptionsBar::cropPage()
{
    auto* w = new QWidget;
    auto* l = row(w);
    l->addWidget(new QLabel(QStringLiteral("Обведите область кадра.")));
    auto* apply = new QPushButton(QStringLiteral("Кадрировать (Enter)"));
    auto* cancel = new QPushButton(QStringLiteral("Отмена (Esc)"));
    l->addWidget(apply);
    l->addWidget(cancel);
    connect(apply, &QPushButton::clicked, this, &ToolOptionsBar::cropApply);
    connect(cancel, &QPushButton::clicked, this, &ToolOptionsBar::cropCancel);
    return w;
}

QWidget* ToolOptionsBar::hintPage(const QString& text)
{
    auto* w = new QWidget;
    auto* l = row(w);
    auto* label = new QLabel(text);
    label->setForegroundRole(QPalette::PlaceholderText);
    l->addWidget(label);
    return w;
}

} // namespace mf
