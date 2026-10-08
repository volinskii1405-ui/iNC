#pragma once

#include <QObject>
#include <QPointer>

class QAudioSink;
class QIODevice;

namespace mf {

// Plays 16-bit interleaved PCM from any QIODevice through the default output.
// When no audio device exists the app keeps working silently.
class PcmPlayer : public QObject {
    Q_OBJECT
public:
    explicit PcmPlayer(QObject* parent = nullptr);
    ~PcmPlayer() override;

    bool start(QIODevice* source, int rate = 48000, int channels = 2); // takes ownership of source
    void stop();
    bool isActive() const;
    void setVolume(double v);
    QString lastError() const { return m_error; }
    static bool deviceAvailable();

signals:
    void finished();

private:
    QAudioSink* m_sink = nullptr;
    QIODevice* m_source = nullptr;
    QString m_error;
    double m_volume = 1.0;
};

} // namespace mf
