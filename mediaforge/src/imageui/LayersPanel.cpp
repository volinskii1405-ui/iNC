#include "imageui/LayersPanel.h"

#include "app/Theme.h"
#include "image/Document.h"

#include <QAction>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QSlider>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

namespace mf {

namespace {
constexpr int kThumb = 44;

QIcon thumbnail(const QImage& img)
{
    QPixmap pm(kThumb, kThumb);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    const QSize s = img.size().scaled(kThumb - 4, kThumb - 4, Qt::KeepAspectRatio);
    const QRect r((kThumb - s.width()) / 2, (kThumb - s.height()) / 2, s.width(), s.height());
    for (int y = r.top(); y <= r.bottom(); y += 6)
        for (int x = r.left(); x <= r.right(); x += 6)
            p.fillRect(QRect(x, y, 6, 6).intersected(r), ((x - r.left()) / 6 + (y - r.top()) / 6) % 2 ? QColor(150, 150, 150)
                                                                                                         : QColor(205, 205, 205));
    // Nearest-neighbour downscale keeps this cheap even for very large layers.
    p.drawImage(r, img.scaled(s, Qt::KeepAspectRatio, Qt::FastTransformation));
    p.setPen(QColor(0, 0, 0, 120));
    p.drawRect(r.adjusted(0, 0, -1, -1));
    return QIcon(pm);
}
} // namespace

LayersPanel::LayersPanel(Document* doc, QWidget* parent)
    : QWidget(parent), m_doc(doc)
{
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(6, 6, 6, 6);
    lay->setSpacing(6);

    auto* top = new QHBoxLayout;
    m_mode = new QComboBox;
    for (int i = 0; i < kBlendModeCount; ++i)
        m_mode->addItem(blendModeName(BlendMode(i)));
    m_mode->setToolTip(QStringLiteral("Режим наложения слоя"));
    top->addWidget(m_mode, 1);
    lay->addLayout(top);

    auto* op = new QHBoxLayout;
    op->addWidget(new QLabel(QStringLiteral("Непрозр.:")));
    m_opacity = new QSlider(Qt::Horizontal);
    m_opacity->setRange(0, 100);
    m_opacitySpin = new QSpinBox;
    m_opacitySpin->setRange(0, 100);
    m_opacitySpin->setSuffix(QStringLiteral(" %"));
    op->addWidget(m_opacity, 1);
    op->addWidget(m_opacitySpin);
    lay->addLayout(op);

    m_list = new QListWidget;
    m_list->setIconSize(QSize(kThumb, kThumb));
    m_list->setSpacing(1);
    m_list->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    m_list->setToolTip(QStringLiteral("Флажок — видимость, двойной щелчок — переименовать"));
    lay->addWidget(m_list, 1);

    m_buttons = new QWidget;
    auto* bl = new QHBoxLayout(m_buttons);
    bl->setContentsMargins(0, 0, 0, 0);
    bl->setSpacing(2);
    lay->addWidget(m_buttons);

    m_rebuildTimer.setSingleShot(true);
    m_rebuildTimer.setInterval(60);
    connect(&m_rebuildTimer, &QTimer::timeout, this, &LayersPanel::rebuild);
    connect(m_doc, &Document::structureChanged, &m_rebuildTimer, qOverload<>(&QTimer::start));

    connect(m_list, &QListWidget::currentRowChanged, this, [this](int row) {
        if (!m_updating && row >= 0)
            m_doc->setActiveLayer(rowToLayer(row));
    });
    connect(m_list, &QListWidget::itemChanged, this, &LayersPanel::onItemChanged);
    connect(m_mode, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (!m_updating && !m_doc->isEmpty())
            m_doc->setLayerBlendMode(m_doc->activeIndex(), BlendMode(i));
    });
    connect(m_opacity, &QSlider::valueChanged, this, [this](int v) {
        m_opacitySpin->blockSignals(true);
        m_opacitySpin->setValue(v);
        m_opacitySpin->blockSignals(false);
        if (!m_updating && !m_doc->isEmpty())
            m_doc->setLayerOpacity(m_doc->activeIndex(), v / 100.0);
    });
    connect(m_opacitySpin, &QSpinBox::valueChanged, m_opacity, &QSlider::setValue);
    rebuild();
}

void LayersPanel::setActions(QAction* add, QAction* dup, QAction* del, QAction* up, QAction* down, QAction* merge)
{
    auto* bl = static_cast<QHBoxLayout*>(m_buttons->layout());
    for (QAction* a : {add, dup, del, up, down, merge}) {
        auto* b = new QToolButton;
        b->setDefaultAction(a);
        b->setAutoRaise(true);
        b->setIconSize(QSize(20, 20));
        bl->addWidget(b);
    }
    bl->addStretch();
}

int LayersPanel::rowToLayer(int row) const
{
    return m_doc->layerCount() - 1 - row;
}

void LayersPanel::rebuild()
{
    m_updating = true;
    const int n = m_doc->layerCount();
    while (m_list->count() > n)
        delete m_list->takeItem(m_list->count() - 1);
    while (m_list->count() < n) {
        auto* item = new QListWidgetItem;
        item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsEditable | Qt::ItemIsUserCheckable);
        m_list->addItem(item);
    }
    for (int row = 0; row < n; ++row) {
        const Layer& l = m_doc->layer(rowToLayer(row));
        QListWidgetItem* item = m_list->item(row);
        if (item->text() != l.name)
            item->setText(l.name);
        item->setCheckState(l.visible ? Qt::Checked : Qt::Unchecked);
        item->setIcon(thumbnail(l.image));
        QString tip = QStringLiteral("%1\nРежим: %2, непрозрачность %3 %")
                          .arg(l.name, blendModeName(l.mode))
                          .arg(int(std::lround(l.opacity * 100)));
        if (l.text)
            tip += QStringLiteral("\nТекстовый слой (щёлкните инструментом «Текст», чтобы изменить)");
        item->setToolTip(tip);
    }
    if (n > 0)
        m_list->setCurrentRow(n - 1 - m_doc->activeIndex());
    syncProperties();
    m_updating = false;
}

void LayersPanel::syncProperties()
{
    const bool has = !m_doc->isEmpty();
    m_mode->setEnabled(has);
    m_opacity->setEnabled(has);
    m_opacitySpin->setEnabled(has);
    if (!has)
        return;
    const Layer& l = m_doc->layer(m_doc->activeIndex());
    m_mode->setCurrentIndex(int(l.mode));
    m_opacity->setValue(int(std::lround(l.opacity * 100)));
    m_opacitySpin->setValue(int(std::lround(l.opacity * 100)));
}

void LayersPanel::onItemChanged(QListWidgetItem* item)
{
    if (m_updating)
        return;
    const int layer = rowToLayer(m_list->row(item));
    if (layer < 0 || layer >= m_doc->layerCount())
        return;
    const Layer& l = m_doc->layer(layer);
    const bool visible = item->checkState() == Qt::Checked;
    if (visible != l.visible)
        m_doc->setLayerVisible(layer, visible);
    else if (item->text() != l.name)
        m_doc->renameLayer(layer, item->text());
}

} // namespace mf
