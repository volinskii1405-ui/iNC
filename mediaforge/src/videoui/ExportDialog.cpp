#include "videoui/ExportDialog.h"

#include "image/ImageIO.h"
#include "media/FFmpegJob.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStorageInfo>

#include <cmath>

namespace mf {

VideoExportDialog::VideoExportDialog(const TimelineState& st, double rangeIn, double rangeOut, const QString& suggestedName,
                                     QWidget* parent)
    : QDialog(parent), m_state(st), m_in(rangeIn), m_out(rangeOut)
{
    setWindowTitle(QStringLiteral("Экспорт видео"));
    auto* form = new QFormLayout(this);

    m_format = new QComboBox;
    m_format->addItem(QStringLiteral("MP4 (H.264 + AAC)"), int(VideoFormat::MP4));
    m_format->addItem(QStringLiteral("WEBM (VP9 + Opus)"), int(VideoFormat::WEBM));
    form->addRow(QStringLiteral("Формат:"), m_format);

    m_res = new QComboBox;
    m_res->addItem(QStringLiteral("Как в проекте (%1 × %2)").arg(st.resolution.width()).arg(st.resolution.height()), QSize());
    for (int h : {2160, 1440, 1080, 720, 480, 360}) {
        const int w = int(std::lround(h * double(st.resolution.width()) / std::max(1, st.resolution.height()))) & ~1;
        m_res->addItem(QStringLiteral("%1p (%2 × %3)").arg(h).arg(w).arg(h), QSize(w, h));
    }
    m_res->addItem(QStringLiteral("Своё…"), QSize(-1, -1));
    m_w = new QSpinBox;
    m_h = new QSpinBox;
    for (QSpinBox* s : {m_w, m_h}) {
        s->setRange(16, 7680);
        s->setSingleStep(2);
        s->setSuffix(QStringLiteral(" px"));
    }
    m_w->setValue(st.resolution.width());
    m_h->setValue(st.resolution.height());
    auto* custom = new QWidget;
    auto* cl = new QHBoxLayout(custom);
    cl->setContentsMargins(0, 0, 0, 0);
    cl->addWidget(m_w);
    cl->addWidget(new QLabel(QStringLiteral("×")));
    cl->addWidget(m_h);
    form->addRow(QStringLiteral("Разрешение:"), m_res);
    form->addRow(QString(), custom);

    m_fps = new QComboBox;
    m_fps->addItem(QStringLiteral("Как в проекте (%1)").arg(st.fps, 0, 'g', 5), 0.0);
    for (double f : {23.976, 24.0, 25.0, 29.97, 30.0, 50.0, 60.0})
        m_fps->addItem(QString::number(f, 'g', 5), f);
    form->addRow(QStringLiteral("Кадров в секунду:"), m_fps);

    m_quality = new QComboBox;
    m_quality->addItems({QStringLiteral("Высокое"), QStringLiteral("Хорошее"), QStringLiteral("Среднее"),
                         QStringLiteral("Низкое (маленький файл)"), QStringLiteral("Своё (CRF)")});
    m_quality->setCurrentIndex(1);
    m_crf = new QSpinBox;
    m_crf->setRange(0, 63);
    auto* qrow = new QWidget;
    auto* ql = new QHBoxLayout(qrow);
    ql->setContentsMargins(0, 0, 0, 0);
    ql->addWidget(m_quality, 1);
    ql->addWidget(new QLabel(QStringLiteral("CRF:")));
    ql->addWidget(m_crf);
    form->addRow(QStringLiteral("Качество:"), qrow);

    m_preset = new QComboBox;
    m_preset->addItems({"ultrafast", "superfast", "veryfast", "faster", "fast", "medium", "slow", "slower"});
    m_preset->setCurrentText(QStringLiteral("medium"));
    m_preset->setToolTip(QStringLiteral("Скорость кодирования H.264: быстрее — больше файл при том же качестве"));
    form->addRow(QStringLiteral("Скорость кодирования:"), m_preset);

    m_audioBr = new QComboBox;
    for (int b : {96, 128, 160, 192, 256, 320})
        m_audioBr->addItem(QStringLiteral("%1 кбит/с").arg(b), b);
    m_audioBr->setCurrentIndex(3);
    form->addRow(QStringLiteral("Звук:"), m_audioBr);

    m_all = new QRadioButton(QStringLiteral("Весь таймлайн"));
    m_range = new QRadioButton(QStringLiteral("Диапазон In–Out"));
    m_all->setChecked(true);
    const bool hasRange = rangeIn >= 0 && rangeOut > rangeIn;
    m_range->setEnabled(hasRange);
    if (hasRange) {
        m_range->setText(QStringLiteral("Диапазон In–Out (%1 – %2 с)").arg(rangeIn, 0, 'f', 2).arg(rangeOut, 0, 'f', 2));
        m_range->setChecked(true);
    }
    auto* rr = new QWidget;
    auto* rl = new QHBoxLayout(rr);
    rl->setContentsMargins(0, 0, 0, 0);
    rl->addWidget(m_all);
    rl->addWidget(m_range);
    form->addRow(QStringLiteral("Что экспортировать:"), rr);

    m_path = new QLineEdit(QDir(QStandardPaths::writableLocation(QStandardPaths::MoviesLocation)).filePath(suggestedName + ".mp4"));
    auto* browse = new QPushButton(QStringLiteral("Обзор…"));
    auto* pr = new QWidget;
    auto* pl = new QHBoxLayout(pr);
    pl->setContentsMargins(0, 0, 0, 0);
    pl->addWidget(m_path, 1);
    pl->addWidget(browse);
    form->addRow(QStringLiteral("Файл:"), pr);

    m_estimate = new QLabel;
    m_estimate->setForegroundRole(QPalette::PlaceholderText);
    form->addRow(m_estimate);

    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    box->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Экспортировать"));
    connect(box, &QDialogButtonBox::accepted, this, &VideoExportDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    form->addRow(box);

    connect(browse, &QPushButton::clicked, this, [this] {
        const bool webm = m_format->currentData().toInt() == int(VideoFormat::WEBM);
        const QString p = QFileDialog::getSaveFileName(this, QStringLiteral("Сохранить видео"), m_path->text(),
                                                       webm ? QStringLiteral("WEBM (*.webm)") : QStringLiteral("MP4 (*.mp4)"));
        if (!p.isEmpty())
            m_path->setText(p);
    });
    connect(m_format, &QComboBox::currentIndexChanged, this, &VideoExportDialog::updateFormat);
    connect(m_res, &QComboBox::currentIndexChanged, this, [this, custom] {
        custom->setEnabled(m_res->currentData().toSize() == QSize(-1, -1));
        updateEstimate();
    });
    connect(m_quality, &QComboBox::currentIndexChanged, this, &VideoExportDialog::updateFormat);
    for (QSpinBox* s : {m_w, m_h, m_crf})
        connect(s, &QSpinBox::valueChanged, this, &VideoExportDialog::updateEstimate);
    connect(m_fps, &QComboBox::currentIndexChanged, this, &VideoExportDialog::updateEstimate);
    connect(m_all, &QRadioButton::toggled, this, &VideoExportDialog::updateEstimate);
    custom->setEnabled(false);
    updateFormat();
    resize(560, sizeHint().height());
}

void VideoExportDialog::updateFormat()
{
    const bool webm = m_format->currentData().toInt() == int(VideoFormat::WEBM);
    m_preset->setEnabled(!webm);
    static const int x264[] = {18, 21, 24, 28};
    static const int vp9[] = {24, 31, 36, 42};
    const int q = m_quality->currentIndex();
    m_crf->setEnabled(q == 4);
    m_crf->setMaximum(webm ? 63 : 51);
    if (q < 4)
        m_crf->setValue(webm ? vp9[q] : x264[q]);
    QString p = m_path->text();
    const QString wanted = webm ? QStringLiteral(".webm") : QStringLiteral(".mp4");
    if (!p.endsWith(wanted, Qt::CaseInsensitive)) {
        const QFileInfo fi(p);
        m_path->setText(QDir(fi.absolutePath()).filePath(fi.completeBaseName() + wanted));
    }
    updateEstimate();
}

QSize VideoExportDialog::chosenSize() const
{
    QSize s = m_res->currentData().toSize();
    if (s == QSize(-1, -1))
        s = QSize(m_w->value(), m_h->value());
    if (s.isEmpty())
        s = m_state.resolution;
    return QSize(s.width() & ~1, s.height() & ~1);
}

double VideoExportDialog::chosenFps() const
{
    const double f = m_fps->currentData().toDouble();
    return f > 0 ? f : m_state.fps;
}

void VideoExportDialog::updateEstimate()
{
    const QSize s = chosenSize();
    const double dur = m_range->isChecked() ? m_out - m_in : m_state.duration();
    // Rough bits-per-pixel guess for x264/VP9 at the chosen CRF; only for a disk-space hint.
    const double bpp = 0.12 * std::pow(0.88, m_crf->value() - 18);
    const double videoBits = s.width() * double(s.height()) * chosenFps() * bpp * dur;
    const double audioBits = m_audioBr->currentData().toInt() * 1000.0 * dur;
    const double mb = (videoBits + audioBits) / 8.0 / 1e6;
    m_estimateMb = mb;
    m_estimate->setText(QStringLiteral("Длительность: %1 с, примерный размер: ~%2 МБ").arg(dur, 0, 'f', 1).arg(std::max(0.1, mb), 0, 'f', 1));
}

VideoExportSettings VideoExportDialog::settings() const
{
    VideoExportSettings s;
    s.format = VideoFormat(m_format->currentData().toInt());
    s.output = m_path->text();
    s.size = chosenSize();
    s.fps = chosenFps();
    s.crf = m_crf->value();
    s.preset = m_preset->currentText();
    s.audioBitrate = m_audioBr->currentData().toInt();
    if (m_range->isChecked()) {
        s.rangeStart = m_in;
        s.rangeEnd = m_out;
    }
    return s;
}

void VideoExportDialog::accept()
{
    const VideoExportSettings s = settings();
    const QFileInfo fi(s.output);
    if (s.output.trimmed().isEmpty() || !QDir(fi.absolutePath()).exists()) {
        QMessageBox::warning(this, QStringLiteral("Экспорт"), QStringLiteral("Укажите существующую папку для файла."));
        return;
    }
    const QString encoder = s.format == VideoFormat::WEBM ? QStringLiteral("libvpx-vp9") : QStringLiteral("libx264");
    QString err;
    if (!FFmpegJob::ffmpegAvailable(&err)) {
        QMessageBox::critical(this, QStringLiteral("Экспорт"), err);
        return;
    }
    if (!FFmpegJob::hasEncoder(encoder)) {
        QMessageBox::critical(this, QStringLiteral("Нет кодека"),
                              QStringLiteral("В ffmpeg нет кодировщика %1. Выберите другой формат.").arg(encoder));
        return;
    }
    QStorageInfo storage(fi.absolutePath());
    const double estimateMb = m_estimateMb;
    if (storage.isValid() && storage.bytesAvailable() < qint64(estimateMb * 1.3e6) + 50 * 1024 * 1024) {
        const auto r = QMessageBox::warning(this, QStringLiteral("Мало места"),
                                            QStringLiteral("На диске свободно %1 МБ, а файл может занять около %2 МБ. "
                                                           "Продолжить?")
                                                .arg(storage.bytesAvailable() / (1024 * 1024))
                                                .arg(estimateMb, 0, 'f', 0),
                                            QMessageBox::Yes | QMessageBox::No);
        if (r != QMessageBox::Yes)
            return;
    }
    if (fi.exists()) {
        const auto r = QMessageBox::question(this, QStringLiteral("Файл существует"),
                                             QStringLiteral("Файл %1 уже существует. Перезаписать?").arg(fi.fileName()));
        if (r != QMessageBox::Yes)
            return;
    }
    QDialog::accept();
}

} // namespace mf
