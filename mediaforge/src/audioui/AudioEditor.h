#pragma once

#include "app/Workspace.h"
#include "media/Audio.h"
#include "media/ExportBuilder.h"

#include <QElapsedTimer>
#include <QHash>
#include <QTimer>
#include <QUndoStack>

class QAction;
class QDoubleSpinBox;
class QLabel;

namespace mf {

class PcmPlayer;
class WaveformWidget;

class AudioEditor : public Workspace {
    Q_OBJECT
public:
    explicit AudioEditor(QWidget* parent = nullptr);
    ~AudioEditor() override;

    QList<QMenu*> menus() const override { return m_menus; }
    bool confirmClose() override;
    void openFiles(const QStringList& paths) override;
    QString documentTitle() const;
    QAction* action(const QString& id) const { return m_a.value(id); }
    WaveformWidget* waveform() const { return m_wave; }
    QUndoStack* undoStack() { return &m_undo; }

    const PcmBuffer& buffer() const { return m_pcm; }
    // Replaces the buffer as one undo step (used by the editing commands).
    void applyEdit(const PcmBuffer& result, const QString& text, qint64 selA, qint64 selB);
    void restore(const PcmBuffer& pcm, qint64 selA, qint64 selB);

signals:
    void sendToVideo(const QString& wavPath, bool replaceOriginal);

private:
    QAction* act(const QString& text, const QString& iconName, const QKeySequence& key, std::function<void()> fn);
    void buildActions();
    void buildLayout();
    bool loadFile(const QString& path);
    void openDialog();
    void extractFromVideo();
    void exportAudio();
    void sendTo(bool replace);
    bool askExportSettings(const QString& title, AudioExportSettings* s, bool allowSelection, bool* onlySelection);
    void runJob(const QString& title, const ExportPlan& plan, const QString& output);

    void range(qint64* a, qint64* b, bool wholeIfEmpty = true) const;
    void trimToSelection();
    void deleteSelection();
    void silenceSelection();
    void volumeDialog();
    void normalize();
    void fade(bool in);
    void speedDialog();
    void copy(bool cut);
    void paste();

    void togglePlay();
    void stopPlayback();
    void updateInfo();
    void updateActions();

    PcmBuffer m_pcm;
    PcmBuffer m_clipboard;
    QString m_path;
    QString m_name;
    QUndoStack m_undo;
    WaveformWidget* m_wave;
    PcmPlayer* m_player;
    QTimer m_playTimer;
    QElapsedTimer m_playClock;
    qint64 m_playStart = 0;
    qint64 m_playEnd = 0;
    bool m_playing = false;
    QLabel* m_info;
    QDoubleSpinBox* m_selStartSpin;
    QDoubleSpinBox* m_selEndSpin;
    QHash<QString, QAction*> m_a;
    QList<QMenu*> m_menus;
    bool m_syncingSpins = false;
};

} // namespace mf
