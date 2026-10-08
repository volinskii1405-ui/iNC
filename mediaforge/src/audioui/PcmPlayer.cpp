#include "audioui/PcmPlayer.h"

#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSink>
#include <QIODevice>
#include <QMediaDevices>

namespace mf {

PcmPlayer::PcmPlayer(QObject* parent)
    : QObject(parent)
{
}

PcmPlayer::~PcmPlayer()
{
    stop();
}

bool PcmPlayer::deviceAvailable()
{
    return !QMediaDevices::defaultAudioOutput().isNull();
}

bool PcmPlayer::start(QIODevice* source, int rate, int channels)
{
    stop();
    m_source = source;
    if (m_source)
        m_source->setParent(this);
    const QAudioDevice dev = QMediaDevices::defaultAudioOutput();
    if (dev.isNull()) {
        m_error = QStringLiteral("Нет устройства вывода звука — воспроизведение без звука.");
        return false;
    }
    QAudioFormat fmt;
    fmt.setSampleRate(rate);
    fmt.setChannelCount(channels);
    fmt.setSampleFormat(QAudioFormat::Int16);
    if (!dev.isFormatSupported(fmt)) {
        m_error = QStringLiteral("Звуковое устройство не поддерживает %1 Гц, 16 бит.").arg(rate);
        return false;
    }
    m_sink = new QAudioSink(dev, fmt, this);
    m_sink->setVolume(m_volume);
    connect(m_sink, &QAudioSink::stateChanged, this, [this](QAudio::State s) {
        if (s == QAudio::IdleState && m_sink && m_sink->error() == QAudio::NoError)
            emit finished();
    });
    m_sink->start(m_source);
    if (m_sink->error() != QAudio::NoError) {
        m_error = QStringLiteral("Не удалось открыть звуковое устройство.");
        stop();
        return false;
    }
    m_error.clear();
    return true;
}

void PcmPlayer::stop()
{
    if (m_sink) {
        m_sink->disconnect(this);
        m_sink->stop();
        delete m_sink;
        m_sink = nullptr;
    }
    if (m_source) {
        delete m_source;
        m_source = nullptr;
    }
}

bool PcmPlayer::isActive() const
{
    return m_sink && (m_sink->state() == QAudio::ActiveState || m_sink->state() == QAudio::IdleState);
}

void PcmPlayer::setVolume(double v)
{
    m_volume = v;
    if (m_sink)
        m_sink->setVolume(v);
}

} // namespace mf
