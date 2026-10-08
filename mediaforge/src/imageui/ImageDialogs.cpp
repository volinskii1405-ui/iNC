#include "imageui/ImageDialogs.h"

#include "app/Busy.h"
#include "app/Theme.h"
#include "image/Document.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFontComboBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

#include <cmath>

namespace mf {

// ============================================================ PreviewRunner

PreviewRunner::PreviewRunner(Document* doc, bool respectSelection, QObject* parent)
    : QObject(parent), m_doc(doc), m_layer(doc->activeIndex())
{
    m_src = doc->layer(m_layer).image;
    if (respectSelection)
        m_mask = doc->selectionMask();
    doc->beginPixelEdit(m_layer);
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(90);
    connect(&m_debounce, &QTimer::timeout, this, &PreviewRunner::startJob);
    connect(&m_watcher, &QFutureWatcher<QImage>::finished, this, &PreviewRunner::onJobDone);
}

PreviewRunner::~PreviewRunner()
{
    cancel();
    m_watcher.disconnect(this);
}

void PreviewRunner::request(Fn fn)
{
    m_latest = std::move(fn);
    ++m_requested;
    m_debounce.start();
}

void PreviewRunner::showOriginal()
{
    m_debounce.stop();
    ++m_requested;
    m_latest = nullptr;
    if (m_doc->isEditing()) {
        m_doc->editImage() = m_src;
        m_doc->notifyEdit(m_doc->rect());
    }
}

void PreviewRunner::startJob()
{
    if (!m_latest || m_finished)
        return;
    if (m_watcher.isRunning())
        return; // onJobDone restarts with the newest request
    m_running = m_requested;
    emit busyChanged(true);
    const QImage src = m_src, mask = m_mask;
    const Fn fn = m_latest;
    m_watcher.setFuture(QtConcurrent::run([src, mask, fn] {
        QImage r = fn(src);
        return mask.isNull() ? r : filters::blendWithMask(src, r, mask);
    }));
}

void PreviewRunner::onJobDone()
{
    if (m_finished)
        return;
    const QImage r = m_watcher.result();
    if (m_doc->isEditing() && m_latest) {
        m_doc->editImage() = r;
        m_doc->notifyEdit(m_doc->rect());
        m_shown = m_running;
    }
    if (m_running != m_requested && m_latest)
        startJob();
    else
        emit busyChanged(false);
}

bool PreviewRunner::commit(const QString& title, QWidget* busyParent)
{
    if (m_finished)
        return false;
    m_debounce.stop();
    if (m_watcher.isRunning())
        m_watcher.waitForFinished();
    if (!m_latest) {
        cancel();
        return false;
    }
    if (m_shown != m_requested) {
        const QImage src = m_src, mask = m_mask;
        const Fn fn = m_latest;
        const QImage r = runBusy<QImage>(busyParent, QStringLiteral("Применение: %1…").arg(title), [=] {
            QImage out = fn(src);
            return mask.isNull() ? out : filters::blendWithMask(src, out, mask);
        });
        m_doc->editImage() = r;
    }
    m_finished = true;
    m_doc->endPixelEdit(title, m_doc->rect());
    return true;
}

void PreviewRunner::cancel()
{
    if (m_finished)
        return;
    m_finished = true;
    m_debounce.stop();
    if (m_doc->isEditing())
        m_doc->cancelPixelEdit();
}

// ============================================================ FilterDialog

FilterDialog::FilterDialog(Document* doc, const QString& title, const QVector<FilterParam>& params, Fn fn,
                           QWidget* parent, bool respectSelection)
    : QDialog(parent), m_doc(doc), m_title(title), m_params(params), m_fn(std::move(fn))
{
    setWindowTitle(title);
    m_runner = new PreviewRunner(doc, respectSelection, this);
    auto* lay = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    for (const FilterParam& p : m_params) {
        if (p.isBool) {
            auto* cb = new QCheckBox;
            cb->setChecked(p.value != 0);
            connect(cb, &QCheckBox::toggled, this, &FilterDialog::schedule);
            form->addRow(p.label, cb);
            m_editors << cb;
        } else if (!p.choices.isEmpty()) {
            auto* combo = new QComboBox;
            combo->addItems(p.choices);
            combo->setCurrentIndex(int(p.value));
            connect(combo, &QComboBox::currentIndexChanged, this, &FilterDialog::schedule);
            form->addRow(p.label, combo);
            m_editors << combo;
        } else {
            auto* row = new QWidget;
            auto* h = new QHBoxLayout(row);
            h->setContentsMargins(0, 0, 0, 0);
            const double scale = std::pow(10.0, p.decimals);
            auto* slider = new QSlider(Qt::Horizontal);
            slider->setRange(int(std::lround(p.min * scale)), int(std::lround(p.max * scale)));
            slider->setMinimumWidth(220);
            auto* spin = new QDoubleSpinBox;
            spin->setRange(p.min, p.max);
            spin->setDecimals(p.decimals);
            spin->setSingleStep(p.decimals ? 1.0 / scale : 1.0);
            spin->setSuffix(p.suffix);
            spin->setValue(p.value);
            slider->setValue(int(std::lround(p.value * scale)));
            connect(slider, &QSlider::valueChanged, spin, [spin, scale](int v) {
                if (std::lround(spin->value() * scale) != v)
                    spin->setValue(v / scale);
            });
            connect(spin, &QDoubleSpinBox::valueChanged, this, [this, slider, scale](double v) {
                slider->blockSignals(true);
                slider->setValue(int(std::lround(v * scale)));
                slider->blockSignals(false);
                schedule();
            });
            h->addWidget(slider, 1);
            h->addWidget(spin);
            form->addRow(p.label, row);
            m_editors << spin;
        }
    }
    lay->addLayout(form);

    auto* bottom = new QHBoxLayout;
    m_preview = new QCheckBox(QStringLiteral("Просмотр"));
    m_preview->setChecked(true);
    connect(m_preview, &QCheckBox::toggled, this, &FilterDialog::schedule);
    m_busy = new QLabel(QStringLiteral("Обработка…"));
    m_busy->setVisible(false);
    connect(m_runner, &PreviewRunner::busyChanged, m_busy, &QLabel::setVisible);
    bottom->addWidget(m_preview);
    bottom->addWidget(m_busy);
    bottom->addStretch();
    auto* reset = new QPushButton(QStringLiteral("Сбросить"));
    connect(reset, &QPushButton::clicked, this, [this] {
        for (int i = 0; i < m_params.size(); ++i) {
            QWidget* w = m_editors[i];
            if (auto* s = qobject_cast<QDoubleSpinBox*>(w))
                s->setValue(m_params[i].value);
            else if (auto* c = qobject_cast<QComboBox*>(w))
                c->setCurrentIndex(int(m_params[i].value));
            else if (auto* b = qobject_cast<QCheckBox*>(w))
                b->setChecked(m_params[i].value != 0);
        }
    });
    bottom->addWidget(reset);
    lay->addLayout(bottom);

    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(box, &QDialogButtonBox::accepted, this, &FilterDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &FilterDialog::reject);
    lay->addWidget(box);
    schedule();
}

QVariantMap FilterDialog::values() const
{
    QVariantMap m;
    for (int i = 0; i < m_params.size(); ++i) {
        QWidget* w = m_editors[i];
        if (auto* s = qobject_cast<QDoubleSpinBox*>(w))
            m[m_params[i].key] = s->value();
        else if (auto* c = qobject_cast<QComboBox*>(w))
            m[m_params[i].key] = c->currentIndex();
        else if (auto* b = qobject_cast<QCheckBox*>(w))
            m[m_params[i].key] = b->isChecked();
    }
    return m;
}

void FilterDialog::schedule()
{
    if (!m_preview->isChecked()) {
        m_runner->showOriginal();
        return;
    }
    const QVariantMap v = values();
    const Fn fn = m_fn;
    m_runner->request([fn, v](const QImage& src) { return fn(src, v); });
}

void FilterDialog::accept()
{
    if (!m_preview->isChecked()) {
        m_preview->blockSignals(true);
        m_preview->setChecked(true);
        m_preview->blockSignals(false);
        schedule();
    }
    m_runner->commit(m_title, this);
    QDialog::accept();
}

void FilterDialog::reject()
{
    m_runner->cancel();
    QDialog::reject();
}

// ============================================================ Curves

CurvesWidget::CurvesWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(260, 260);
    setMouseTracking(true);
    m_points = {QPointF(0, 0), QPointF(255, 255)};
}

void CurvesWidget::setPoints(const QVector<QPointF>& pts)
{
    m_points = pts;
    update();
}

void CurvesWidget::setHistogram(const QVector<int>& hist, QColor color)
{
    m_hist = hist;
    m_color = color;
    update();
}

QRectF CurvesWidget::plot() const
{
    const double s = std::min(width(), height()) - 16.0;
    return QRectF(8, 8, s, s);
}

QPointF CurvesWidget::toWidget(QPointF v) const
{
    const QRectF r = plot();
    return QPointF(r.left() + v.x() / 255.0 * r.width(), r.bottom() - v.y() / 255.0 * r.height());
}

QPointF CurvesWidget::toValue(QPointF w) const
{
    const QRectF r = plot();
    return QPointF(std::clamp((w.x() - r.left()) / r.width() * 255.0, 0.0, 255.0),
                   std::clamp((r.bottom() - w.y()) / r.height() * 255.0, 0.0, 255.0));
}

void CurvesWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = plot();
    p.fillRect(r, QColor(24, 24, 26));
    if (!m_hist.isEmpty()) {
        const int mx = std::max(1, *std::max_element(m_hist.begin(), m_hist.end()));
        QPainterPath h(r.bottomLeft());
        for (int i = 0; i < 256; ++i)
            h.lineTo(r.left() + (i + 0.5) / 256.0 * r.width(), r.bottom() - std::sqrt(double(m_hist[i]) / mx) * r.height());
        h.lineTo(r.bottomRight());
        QColor c = m_color;
        c.setAlpha(60);
        p.fillPath(h, c);
    }
    p.setPen(QPen(QColor(80, 80, 86), 1));
    for (int i = 1; i < 4; ++i) {
        p.drawLine(QPointF(r.left() + r.width() * i / 4, r.top()), QPointF(r.left() + r.width() * i / 4, r.bottom()));
        p.drawLine(QPointF(r.left(), r.top() + r.height() * i / 4), QPointF(r.right(), r.top() + r.height() * i / 4));
    }
    p.drawLine(r.bottomLeft(), r.topRight());
    p.drawRect(r);
    const filters::Lut lut = filters::curveLut(m_points);
    QPainterPath curve;
    for (int i = 0; i < 256; ++i) {
        const QPointF pt = toWidget(QPointF(i, lut[i]));
        if (i == 0)
            curve.moveTo(pt);
        else
            curve.lineTo(pt);
    }
    p.setPen(QPen(m_color, 2));
    p.drawPath(curve);
    p.setPen(QPen(Qt::white, 1.5));
    p.setBrush(QColor(30, 30, 30));
    for (const QPointF& pt : m_points)
        p.drawEllipse(toWidget(pt), 5, 5);
}

void CurvesWidget::mousePressEvent(QMouseEvent* e)
{
    m_drag = -1;
    for (int i = 0; i < m_points.size(); ++i)
        if (QLineF(toWidget(m_points[i]), e->position()).length() < 9)
            m_drag = i;
    if (e->button() == Qt::RightButton) {
        if (m_drag > 0 && m_drag < m_points.size() - 1) {
            m_points.removeAt(m_drag);
            emit changed();
            update();
        }
        m_drag = -1;
        return;
    }
    if (m_drag < 0) {
        const QPointF v = toValue(e->position());
        int idx = 0;
        while (idx < m_points.size() && m_points[idx].x() < v.x())
            ++idx;
        m_points.insert(idx, v);
        m_drag = idx;
        emit changed();
    }
    update();
}

void CurvesWidget::mouseMoveEvent(QMouseEvent* e)
{
    if (m_drag < 0 || !(e->buttons() & Qt::LeftButton))
        return;
    QPointF v = toValue(e->position());
    const double lo = m_drag > 0 ? m_points[m_drag - 1].x() + 1 : 0;
    const double hi = m_drag < m_points.size() - 1 ? m_points[m_drag + 1].x() - 1 : 255;
    v.setX(std::clamp(v.x(), lo, hi));
    m_points[m_drag] = v;
    emit changed();
    update();
}

void CurvesWidget::mouseReleaseEvent(QMouseEvent*)
{
    m_drag = -1;
}

CurvesDialog::CurvesDialog(Document* doc, QWidget* parent)
    : QDialog(parent), m_doc(doc)
{
    setWindowTitle(QStringLiteral("Кривые"));
    // Histograms come from the layer before the dialog starts previewing.
    const QImage img = doc->layer(doc->activeIndex()).image.convertToFormat(QImage::Format_ARGB32);
    for (auto& h : m_hist)
        h = QVector<int>(256, 0);
    for (int y = 0; y < img.height(); y += std::max(1, img.height() / 400)) {
        const QRgb* row = reinterpret_cast<const QRgb*>(img.constScanLine(y));
        for (int x = 0; x < img.width(); x += std::max(1, img.width() / 400)) {
            if (!qAlpha(row[x]))
                continue;
            m_hist[0][qGray(row[x])]++;
            m_hist[1][qRed(row[x])]++;
            m_hist[2][qGreen(row[x])]++;
            m_hist[3][qBlue(row[x])]++;
        }
    }
    for (auto& pts : m_pts)
        pts = {QPointF(0, 0), QPointF(255, 255)};

    m_runner = new PreviewRunner(doc, true, this);
    auto* lay = new QVBoxLayout(this);
    auto* top = new QHBoxLayout;
    top->addWidget(new QLabel(QStringLiteral("Канал:")));
    m_channel = new QComboBox;
    m_channel->addItems({QStringLiteral("RGB"), QStringLiteral("Красный"), QStringLiteral("Зелёный"), QStringLiteral("Синий")});
    top->addWidget(m_channel);
    top->addStretch();
    auto* reset = new QPushButton(QStringLiteral("Сбросить канал"));
    top->addWidget(reset);
    lay->addLayout(top);
    m_curve = new CurvesWidget;
    lay->addWidget(m_curve, 1);
    auto* hint = new QLabel(QStringLiteral("Щелчок — добавить точку, перетаскивание — изменить, правый щелчок — удалить"));
    hint->setWordWrap(true);
    hint->setForegroundRole(QPalette::PlaceholderText);
    lay->addWidget(hint);
    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(box, &QDialogButtonBox::accepted, this, &CurvesDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &CurvesDialog::reject);
    lay->addWidget(box);

    connect(m_channel, &QComboBox::currentIndexChanged, this, &CurvesDialog::switchChannel);
    connect(m_curve, &CurvesWidget::changed, this, [this] {
        m_pts[m_current] = m_curve->points();
        schedule();
    });
    connect(reset, &QPushButton::clicked, this, [this] {
        m_pts[m_current] = {QPointF(0, 0), QPointF(255, 255)};
        m_curve->setPoints(m_pts[m_current]);
        schedule();
    });
    switchChannel(0);
    resize(380, 460);
}

void CurvesDialog::switchChannel(int ch)
{
    m_current = ch;
    static const QColor colors[4] = {QColor(230, 230, 230), QColor(235, 80, 80), QColor(90, 210, 90), QColor(90, 140, 240)};
    m_curve->setPoints(m_pts[ch]);
    m_curve->setHistogram(m_hist[ch], colors[ch]);
}

void CurvesDialog::schedule()
{
    using namespace filters;
    const Lut master = curveLut(m_pts[0]);
    const Lut r = composeLut(master, curveLut(m_pts[1]));
    const Lut g = composeLut(master, curveLut(m_pts[2]));
    const Lut b = composeLut(master, curveLut(m_pts[3]));
    m_runner->request([r, g, b](const QImage& src) { return applyLuts(src, r, g, b); });
}

void CurvesDialog::accept()
{
    schedule();
    m_runner->commit(QStringLiteral("Кривые"), this);
    QDialog::accept();
}

void CurvesDialog::reject()
{
    m_runner->cancel();
    QDialog::reject();
}

// ============================================================ Text

TextDialog::TextDialog(const TextInfo& initial, QWidget* parent)
    : QDialog(parent), m_info(initial)
{
    setWindowTitle(QStringLiteral("Текст"));
    auto* lay = new QVBoxLayout(this);
    m_text = new QPlainTextEdit(initial.text);
    m_text->setPlaceholderText(QStringLiteral("Введите текст…"));
    m_text->setMinimumHeight(110);
    lay->addWidget(m_text);
    auto* form = new QFormLayout;
    m_font = new QFontComboBox;
    m_font->setCurrentFont(initial.font);
    form->addRow(QStringLiteral("Шрифт:"), m_font);
    m_size = new QSpinBox;
    m_size->setRange(4, 2000);
    m_size->setSuffix(QStringLiteral(" px"));
    m_size->setValue(initial.font.pixelSize() > 0 ? initial.font.pixelSize() : 48);
    form->addRow(QStringLiteral("Размер:"), m_size);
    auto* style = new QHBoxLayout;
    m_bold = new QCheckBox(QStringLiteral("Жирный"));
    m_bold->setChecked(initial.font.bold());
    m_italic = new QCheckBox(QStringLiteral("Курсив"));
    m_italic->setChecked(initial.font.italic());
    style->addWidget(m_bold);
    style->addWidget(m_italic);
    style->addStretch();
    form->addRow(QStringLiteral("Начертание:"), style);
    m_color = new QToolButton;
    m_color->setFixedSize(60, 24);
    connect(m_color, &QToolButton::clicked, this, [this] {
        const QColor c = QColorDialog::getColor(m_info.color, this, QStringLiteral("Цвет текста"),
                                                QColorDialog::DontUseNativeDialog | QColorDialog::ShowAlphaChannel);
        if (c.isValid()) {
            m_info.color = c;
            updateColorButton();
        }
    });
    updateColorButton();
    form->addRow(QStringLiteral("Цвет:"), m_color);
    lay->addLayout(form);
    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(box);
    m_text->setFocus();
    resize(420, 320);
}

void TextDialog::updateColorButton()
{
    QPixmap pm(48, 16);
    pm.fill(m_info.color);
    m_color->setIcon(QIcon(pm));
    m_color->setIconSize(pm.size());
}

TextInfo TextDialog::result() const
{
    TextInfo t = m_info;
    t.text = m_text->toPlainText();
    QFont f = m_font->currentFont();
    f.setPixelSize(m_size->value());
    f.setBold(m_bold->isChecked());
    f.setItalic(m_italic->isChecked());
    t.font = f;
    return t;
}

// ============================================================ size dialogs

bool askNewImage(QWidget* parent, QColor bg, QColor fg, NewImageSpec* out)
{
    QDialog dlg(parent);
    dlg.setWindowTitle(QStringLiteral("Новое изображение"));
    auto* form = new QFormLayout(&dlg);
    auto* preset = new QComboBox;
    const QList<QPair<QString, QSize>> presets = {
        {QStringLiteral("Full HD 1920 × 1080"), QSize(1920, 1080)},
        {QStringLiteral("HD 1280 × 720"), QSize(1280, 720)},
        {QStringLiteral("4K 3840 × 2160"), QSize(3840, 2160)},
        {QStringLiteral("Квадрат 1080 × 1080"), QSize(1080, 1080)},
        {QStringLiteral("A4, 300 dpi 2480 × 3508"), QSize(2480, 3508)},
        {QStringLiteral("800 × 600"), QSize(800, 600)},
    };
    for (const auto& p : presets)
        preset->addItem(p.first, p.second);
    auto* w = new QSpinBox;
    auto* h = new QSpinBox;
    for (QSpinBox* s : {w, h}) {
        s->setRange(1, 30000);
        s->setSuffix(QStringLiteral(" px"));
    }
    w->setValue(1920);
    h->setValue(1080);
    QObject::connect(preset, &QComboBox::currentIndexChanged, &dlg, [=](int i) {
        const QSize s = preset->itemData(i).toSize();
        w->setValue(s.width());
        h->setValue(s.height());
    });
    auto* fill = new QComboBox;
    fill->addItems({QStringLiteral("Белый"), QStringLiteral("Прозрачный"), QStringLiteral("Фоновый цвет"),
                    QStringLiteral("Основной цвет"), QStringLiteral("Чёрный")});
    form->addRow(QStringLiteral("Шаблон:"), preset);
    form->addRow(QStringLiteral("Ширина:"), w);
    form->addRow(QStringLiteral("Высота:"), h);
    form->addRow(QStringLiteral("Фон:"), fill);
    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    QObject::connect(box, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(box);
    if (dlg.exec() != QDialog::Accepted)
        return false;
    out->size = QSize(w->value(), h->value());
    const QColor fills[] = {Qt::white, Qt::transparent, bg, fg, Qt::black};
    out->background = fills[fill->currentIndex()];
    return true;
}

bool askImageSize(QWidget* parent, QSize current, QSize* out, bool* smooth)
{
    QDialog dlg(parent);
    dlg.setWindowTitle(QStringLiteral("Размер изображения"));
    auto* form = new QFormLayout(&dlg);
    auto* w = new QSpinBox;
    auto* h = new QSpinBox;
    auto* pct = new QDoubleSpinBox;
    for (QSpinBox* s : {w, h}) {
        s->setRange(1, 30000);
        s->setSuffix(QStringLiteral(" px"));
    }
    pct->setRange(1, 1000);
    pct->setSuffix(QStringLiteral(" %"));
    pct->setValue(100);
    w->setValue(current.width());
    h->setValue(current.height());
    auto* keep = new QCheckBox(QStringLiteral("Сохранять пропорции"));
    keep->setChecked(true);
    auto* method = new QComboBox;
    method->addItems({QStringLiteral("Сглаживание (билинейная)"), QStringLiteral("Ближайший сосед (пиксель-арт)")});
    bool guard = false;
    const double aspect = double(current.width()) / current.height();
    QObject::connect(w, &QSpinBox::valueChanged, &dlg, [&](int v) {
        if (guard || !keep->isChecked())
            return;
        guard = true;
        h->setValue(std::max(1, int(std::lround(v / aspect))));
        pct->setValue(100.0 * v / current.width());
        guard = false;
    });
    QObject::connect(h, &QSpinBox::valueChanged, &dlg, [&](int v) {
        if (guard || !keep->isChecked())
            return;
        guard = true;
        w->setValue(std::max(1, int(std::lround(v * aspect))));
        pct->setValue(100.0 * v / current.height());
        guard = false;
    });
    QObject::connect(pct, &QDoubleSpinBox::valueChanged, &dlg, [&](double v) {
        if (guard)
            return;
        guard = true;
        w->setValue(std::max(1, int(std::lround(current.width() * v / 100))));
        h->setValue(std::max(1, int(std::lround(current.height() * v / 100))));
        guard = false;
    });
    form->addRow(QStringLiteral("Масштаб:"), pct);
    form->addRow(QStringLiteral("Ширина:"), w);
    form->addRow(QStringLiteral("Высота:"), h);
    form->addRow(QString(), keep);
    form->addRow(QStringLiteral("Метод:"), method);
    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    QObject::connect(box, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(box);
    if (dlg.exec() != QDialog::Accepted)
        return false;
    *out = QSize(w->value(), h->value());
    *smooth = method->currentIndex() == 0;
    return true;
}

bool askCanvasSize(QWidget* parent, QSize current, QSize* out, int* anchor, bool* fillBackground)
{
    QDialog dlg(parent);
    dlg.setWindowTitle(QStringLiteral("Размер холста"));
    auto* form = new QFormLayout(&dlg);
    auto* w = new QSpinBox;
    auto* h = new QSpinBox;
    for (QSpinBox* s : {w, h}) {
        s->setRange(1, 30000);
        s->setSuffix(QStringLiteral(" px"));
    }
    w->setValue(current.width());
    h->setValue(current.height());
    form->addRow(QStringLiteral("Ширина:"), w);
    form->addRow(QStringLiteral("Высота:"), h);
    auto* grid = new QGridLayout;
    grid->setSpacing(2);
    auto* group = new QButtonGroup(&dlg);
    for (int i = 0; i < 9; ++i) {
        auto* b = new QToolButton;
        b->setCheckable(true);
        b->setFixedSize(28, 28);
        b->setText(i == 4 ? QStringLiteral("●") : QString());
        group->addButton(b, i);
        grid->addWidget(b, i / 3, i % 3);
    }
    group->button(4)->setChecked(true);
    QObject::connect(group, &QButtonGroup::idClicked, &dlg, [group](int id) {
        for (int i = 0; i < 9; ++i)
            group->button(i)->setText(i == id ? QStringLiteral("●") : QString());
    });
    auto* gw = new QWidget;
    gw->setLayout(grid);
    form->addRow(QStringLiteral("Привязка:"), gw);
    auto* fill = new QCheckBox(QStringLiteral("Заполнить новую область фоновым цветом"));
    form->addRow(QString(), fill);
    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    QObject::connect(box, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(box);
    if (dlg.exec() != QDialog::Accepted)
        return false;
    *out = QSize(w->value(), h->value());
    *anchor = group->checkedId();
    *fillBackground = fill->isChecked();
    return true;
}

} // namespace mf
