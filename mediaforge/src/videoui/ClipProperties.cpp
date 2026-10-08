#include "videoui/ClipProperties.h"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <cmath>

namespace mf {

namespace {
QString fmtTime(double t)
{
    const int ms = int(std::lround(t * 1000));
    return QStringLiteral("%1:%2.%3")
        .arg(ms / 60000)
        .arg((ms / 1000) % 60, 2, 10, QLatin1Char('0'))
        .arg(ms % 1000, 3, 10, QLatin1Char('0'));
}
} // namespace

QDoubleSpinBox* ClipProperties::dspin(double min, double max, int decimals, const QString& suffix, double step)
{
    auto* s = new QDoubleSpinBox;
    s->setRange(min, max);
    s->setDecimals(decimals);
    s->setSuffix(suffix);
    s->setSingleStep(step);
    s->setKeyboardTracking(false);
    return s;
}

ClipProperties::ClipProperties(Timeline* tl, QWidget* parent)
    : QWidget(parent), m_tl(tl)
{
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(6, 6, 6, 6);
    m_stack = new QStackedWidget;
    outer->addWidget(m_stack);
    outer->addStretch();

    // ---- project page
    auto* proj = new QWidget;
    auto* pf = new QFormLayout(proj);
    auto* title = new QLabel(QStringLiteral("Проект (ничего не выбрано)"));
    QFont bold = title->font();
    bold.setBold(true);
    title->setFont(bold);
    pf->addRow(title);
    m_projW = new QSpinBox;
    m_projH = new QSpinBox;
    for (QSpinBox* s : {m_projW, m_projH}) {
        s->setRange(16, 7680);
        s->setSingleStep(2);
        s->setSuffix(QStringLiteral(" px"));
        s->setKeyboardTracking(false);
    }
    m_projFps = dspin(1, 120, 3, QStringLiteral(" к/с"), 1);
    pf->addRow(QStringLiteral("Ширина:"), m_projW);
    pf->addRow(QStringLiteral("Высота:"), m_projH);
    pf->addRow(QStringLiteral("Частота:"), m_projFps);
    m_muteOriginal = new QCheckBox(QStringLiteral("Заглушить исходный звук видео"));
    m_muteOriginal->setToolTip(QStringLiteral("Вместе с аудиодорожкой — замена звука в видео"));
    pf->addRow(m_muteOriginal);
    m_projInfo = new QLabel;
    m_projInfo->setWordWrap(true);
    m_projInfo->setForegroundRole(QPalette::PlaceholderText);
    pf->addRow(m_projInfo);
    auto applyFormat = [this] {
        if (!m_updating)
            m_tl->setFormat(QSize(m_projW->value(), m_projH->value()), m_projFps->value());
    };
    connect(m_projW, &QSpinBox::valueChanged, this, applyFormat);
    connect(m_projH, &QSpinBox::valueChanged, this, applyFormat);
    connect(m_projFps, &QDoubleSpinBox::valueChanged, this, applyFormat);
    connect(m_muteOriginal, &QCheckBox::toggled, this, [this](bool v) {
        if (!m_updating)
            m_tl->setMuteOriginal(v);
    });
    m_stack->addWidget(proj);

    // ---- clip page
    auto* clip = new QWidget;
    m_grid = new QGridLayout(clip);
    m_grid->setColumnStretch(1, 1);
    m_name = new QLabel;
    m_name->setFont(bold);
    m_name->setWordWrap(true);
    m_kind = new QLabel;
    m_kind->setWordWrap(true);
    m_length = new QLabel;
    m_grid->addWidget(m_name, 0, 0, 1, 2);
    addRow(QStringLiteral("Тип:"), m_kind);
    addRow(QStringLiteral("Длина:"), m_length);
    m_start = dspin(0, 1e6, 3, QStringLiteral(" с"), 0.1);
    m_in = dspin(0, 1e6, 3, QStringLiteral(" с"), 0.1);
    m_out = dspin(0, 1e6, 3, QStringLiteral(" с"), 0.1);
    m_duration = dspin(kMinClipLength, 1e5, 2, QStringLiteral(" с"), 0.5);
    m_speed = dspin(0.25, 4.0, 2, QStringLiteral("×"), 0.05);
    addRow(QStringLiteral("Начало на шкале:"), m_start);
    addRow(QStringLiteral("Вход (In):"), m_in);
    addRow(QStringLiteral("Выход (Out):"), m_out);
    addRow(QStringLiteral("Длительность:"), m_duration);
    addRow(QStringLiteral("Скорость:"), m_speed);
    m_speedButtons = new QWidget;
    auto* sb = new QHBoxLayout(m_speedButtons);
    sb->setContentsMargins(0, 0, 0, 0);
    sb->setSpacing(2);
    for (double v : {0.25, 0.5, 1.0, 2.0, 4.0}) {
        auto* b = new QPushButton(QStringLiteral("%1×").arg(v));
        b->setMaximumWidth(48);
        connect(b, &QPushButton::clicked, this, [this, v] { m_speed->setValue(v); });
        sb->addWidget(b);
    }
    addRow(QString(), m_speedButtons);
    m_volume = new QSpinBox;
    m_volume->setRange(0, 400);
    m_volume->setSuffix(QStringLiteral(" %"));
    m_volume->setSingleStep(5);
    m_volume->setKeyboardTracking(false);
    m_mute = new QCheckBox(QStringLiteral("Без звука"));
    m_fadeIn = dspin(0, 600, 2, QStringLiteral(" с"), 0.1);
    m_fadeOut = dspin(0, 600, 2, QStringLiteral(" с"), 0.1);
    addRow(QStringLiteral("Громкость:"), m_volume);
    addRow(QString(), m_mute);
    addRow(QStringLiteral("Нарастание:"), m_fadeIn);
    addRow(QStringLiteral("Затухание:"), m_fadeOut);
    m_x = new QSpinBox;
    m_y = new QSpinBox;
    m_width = new QSpinBox;
    m_opacity = new QSpinBox;
    for (QSpinBox* s : {m_x, m_y}) {
        s->setRange(-100, 200);
        s->setSuffix(QStringLiteral(" %"));
    }
    m_width->setRange(1, 400);
    m_width->setSuffix(QStringLiteral(" % ширины кадра"));
    m_opacity->setRange(0, 100);
    m_opacity->setSuffix(QStringLiteral(" %"));
    for (QSpinBox* s : {m_x, m_y, m_width, m_opacity})
        s->setKeyboardTracking(false);
    addRow(QStringLiteral("Позиция X:"), m_x);
    addRow(QStringLiteral("Позиция Y:"), m_y);
    addRow(QStringLiteral("Размер:"), m_width);
    addRow(QStringLiteral("Непрозрачность:"), m_opacity);
    m_grid->setRowStretch(m_grid->rowCount(), 1);
    m_stack->addWidget(clip);

    connect(m_in, &QDoubleSpinBox::valueChanged, this, [this] { apply(In); });
    connect(m_out, &QDoubleSpinBox::valueChanged, this, [this] { apply(Out); });
    connect(m_duration, &QDoubleSpinBox::valueChanged, this, [this] { apply(Duration); });
    connect(m_start, &QDoubleSpinBox::valueChanged, this, [this] { apply(Start); });
    connect(m_speed, &QDoubleSpinBox::valueChanged, this, [this] { apply(Speed); });
    connect(m_volume, &QSpinBox::valueChanged, this, [this] { apply(Volume); });
    connect(m_mute, &QCheckBox::toggled, this, [this] { apply(Mute); });
    connect(m_fadeIn, &QDoubleSpinBox::valueChanged, this, [this] { apply(FadeIn); });
    connect(m_fadeOut, &QDoubleSpinBox::valueChanged, this, [this] { apply(FadeOut); });
    connect(m_x, &QSpinBox::valueChanged, this, [this] { apply(X); });
    connect(m_y, &QSpinBox::valueChanged, this, [this] { apply(Y); });
    connect(m_width, &QSpinBox::valueChanged, this, [this] { apply(Width); });
    connect(m_opacity, &QSpinBox::valueChanged, this, [this] { apply(Opacity); });
    connect(m_tl, &Timeline::changed, this, &ClipProperties::refresh);
    refresh();
}

void ClipProperties::showClip(Track track, quint64 id)
{
    m_track = track;
    m_id = id;
    refresh();
}

void ClipProperties::addRow(const QString& label, QWidget* field)
{
    const int row = m_grid->rowCount();
    auto* l = new QLabel(label);
    m_grid->addWidget(l, row, 0);
    m_grid->addWidget(field, row, 1);
    m_labels.insert(field, l);
}

void ClipProperties::setRow(QWidget* field, bool visible)
{
    field->setVisible(visible);
    if (QWidget* l = m_labels.value(field))
        l->setVisible(visible);
}

void ClipProperties::refresh()
{
    m_updating = true;
    const TimelineState& s = m_tl->state();
    const int idx = m_id ? s.indexOfId(m_track, m_id) : -1;
    if (idx < 0) {
        m_stack->setCurrentIndex(0);
        m_projW->setValue(s.resolution.width());
        m_projH->setValue(s.resolution.height());
        m_projFps->setValue(s.fps);
        m_muteOriginal->setChecked(s.muteOriginal);
        m_projInfo->setText(QStringLiteral("Длительность: %1\nКлипов: %2 видео/изобр., %3 оверлеев, %4 аудио\n\n"
                                           "Выберите клип на таймлайне, чтобы изменить его параметры.")
                                .arg(fmtTime(s.duration()))
                                .arg(s.main.size())
                                .arg(s.overlays.size())
                                .arg(s.audio.size()));
        m_updating = false;
        return;
    }
    m_stack->setCurrentIndex(1);
    const Clip& c = s.track(m_track)[idx];
    m_name->setText(QFileInfo(c.path).fileName());
    m_name->setToolTip(c.path);
    QString kind;
    switch (c.kind) {
    case ClipKind::Video:
        kind = QStringLiteral("Видео %1×%2, %3 к/с%4")
                   .arg(c.info.width)
                   .arg(c.info.height)
                   .arg(c.info.fps, 0, 'f', 2)
                   .arg(c.info.hasAudio ? QStringLiteral(", со звуком") : QStringLiteral(", без звука"));
        break;
    case ClipKind::Image: kind = QStringLiteral("Изображение-клип"); break;
    case ClipKind::Overlay: kind = QStringLiteral("Оверлей (картинка поверх видео)"); break;
    case ClipKind::Audio: kind = QStringLiteral("Аудио, %1 Гц").arg(c.info.sampleRate); break;
    }
    m_kind->setText(kind);
    m_length->setText(fmtTime(c.length()));
    const bool still = c.isStill();
    const bool positioned = m_track != Track::Main;
    const bool hasAudio = c.kind == ClipKind::Audio || (c.kind == ClipKind::Video && c.info.hasAudio);
    setRow(m_start, positioned);
    setRow(m_in, !still);
    setRow(m_out, !still);
    setRow(m_duration, still);
    setRow(m_speed, !still);
    setRow(m_speedButtons, !still);
    setRow(m_volume, hasAudio);
    setRow(m_mute, hasAudio);
    setRow(m_fadeIn, hasAudio);
    setRow(m_fadeOut, hasAudio);
    for (QWidget* w : {static_cast<QWidget*>(m_x), static_cast<QWidget*>(m_y), static_cast<QWidget*>(m_width),
                       static_cast<QWidget*>(m_opacity)})
        setRow(w, c.kind == ClipKind::Overlay);
    m_in->setMaximum(std::max(0.0, c.info.duration));
    m_out->setMaximum(std::max(0.0, c.info.duration));
    m_start->setValue(c.start);
    m_in->setValue(c.in);
    m_out->setValue(c.out);
    m_duration->setValue(c.length());
    m_speed->setValue(c.speed);
    m_volume->setValue(int(std::lround(c.volume * 100)));
    m_mute->setChecked(c.muted);
    m_fadeIn->setValue(c.fadeIn);
    m_fadeOut->setValue(c.fadeOut);
    m_x->setValue(int(std::lround(c.x * 100)));
    m_y->setValue(int(std::lround(c.y * 100)));
    m_width->setValue(int(std::lround(c.width * 100)));
    m_opacity->setValue(int(std::lround(c.opacity * 100)));
    m_updating = false;
}

void ClipProperties::apply(Field f)
{
    if (m_updating)
        return;
    const TimelineState& s = m_tl->state();
    const int idx = s.indexOfId(m_track, m_id);
    if (idx < 0)
        return;
    Clip c = s.track(m_track)[idx];
    QString text;
    switch (f) {
    case In: c.in = m_in->value(); text = QStringLiteral("Точка входа"); break;
    case Out: c.out = m_out->value(); text = QStringLiteral("Точка выхода"); break;
    case Duration: c.in = 0; c.out = m_duration->value(); text = QStringLiteral("Длительность"); break;
    case Start: c.start = m_start->value(); text = QStringLiteral("Позиция клипа"); break;
    case Speed: c.speed = m_speed->value(); text = QStringLiteral("Скорость"); break;
    case Volume: c.volume = m_volume->value() / 100.0; text = QStringLiteral("Громкость"); break;
    case Mute: c.muted = m_mute->isChecked(); text = QStringLiteral("Звук клипа"); break;
    case FadeIn: c.fadeIn = m_fadeIn->value(); text = QStringLiteral("Нарастание"); break;
    case FadeOut: c.fadeOut = m_fadeOut->value(); text = QStringLiteral("Затухание"); break;
    case X: c.x = m_x->value() / 100.0; text = QStringLiteral("Позиция оверлея"); break;
    case Y: c.y = m_y->value() / 100.0; text = QStringLiteral("Позиция оверлея"); break;
    case Width: c.width = m_width->value() / 100.0; text = QStringLiteral("Размер оверлея"); break;
    case Opacity: c.opacity = m_opacity->value() / 100.0; text = QStringLiteral("Непрозрачность"); break;
    }
    // Consecutive edits of the same field of the same clip merge into one undo step.
    const int mergeId = int((c.id % 1000000) * 16 + int(f));
    m_tl->updateClip(m_track, idx, c, text, mergeId);
}

} // namespace mf
