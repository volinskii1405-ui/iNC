#pragma once

#include "app/Workspace.h"
#include "media/MediaInfo.h"
#include "media/Timeline.h"

#include <QHash>
#include <QImage>
#include <QTemporaryDir>

class QAction;
class QLabel;
class QListWidget;
class QToolButton;

namespace mf {

class ClipProperties;
class MediaCache;
class PreviewEngine;
class TimelineAudio;
class TimelineWidget;
struct ExportPlan;

class PreviewWidget : public QWidget {
    Q_OBJECT
public:
    explicit PreviewWidget(QWidget* parent = nullptr);
    void setFrame(const QImage& img);
    const QImage& frame() const { return m_frame; }
    void setAspect(QSize res);
    QSize renderSize() const; // pixels to render for the current widget size
    QSize sizeHint() const override { return QSize(640, 360); }

signals:
    void resized();
    void filesDropped(const QStringList& paths);

protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void dragEnterEvent(QDragEnterEvent*) override;
    void dropEvent(QDropEvent*) override;

private:
    QRect frameRect() const;
    QImage m_frame;
    QSize m_aspect = QSize(16, 9);
};

class VideoEditor : public Workspace {
    Q_OBJECT
public:
    explicit VideoEditor(QWidget* parent = nullptr);
    ~VideoEditor() override;

    QList<QMenu*> menus() const override { return m_menus; }
    bool confirmClose() override;
    void openFiles(const QStringList& paths) override;
    void addAudioTrack(const QString& path, bool replaceOriginal);
    QString documentTitle() const;
    Timeline* timeline() const { return m_tl; }
    void seek(double t);
    double playhead() const { return m_playhead; }
    QAction* action(const QString& id) const { return m_a.value(id); }
    PreviewWidget* preview() const { return m_preview; }
    TimelineWidget* timelineWidget() const { return m_timeline; }

signals:
    void sendFrameToImageEditor(const QImage& image, const QString& name);
    void openInAudioEditor(const QString& path);

private:
    QAction* act(const QString& text, const QString& iconName, const QKeySequence& key, std::function<void()> fn);
    void buildActions();
    void buildLayout();
    void buildMenus();

    void importDialog();
    void importFiles(const QStringList& paths, std::optional<Track> forceTrack, double at);
    void addFromBin(Track track);
    void newProject();
    void openProject(const QString& path = QString());
    bool saveProject(bool saveAs);
    void exportVideo();
    void exportFrame();
    void exportFrameSequence();
    void frameToImageEditor();
    void runExport(const QString& title, const ExportPlan& plan, const QString& output, const QString& doneMessage);

    void togglePlay();
    void stopPlayback();
    void step(double seconds);
    void splitAtPlayhead();
    void deleteSelected();
    void setSpeedOfSelected(double speed);
    void toggleMuteSelected();
    void openSelectedAudio();
    void moveSelected(int delta);
    void setMark(bool in);
    void onTimelineChanged();
    void updateTimeLabel();
    void updateActions();
    void requestFrame();
    QImage renderFullFrame(double t);

    Timeline* m_tl;
    MediaCache* m_cache;
    PreviewEngine* m_engine;
    TimelineAudio* m_audio;
    TimelineWidget* m_timeline;
    PreviewWidget* m_preview;
    ClipProperties* m_props;
    QListWidget* m_bin;
    QLabel* m_timeLabel;
    QToolButton* m_playButton;
    QHash<QString, MediaInfo> m_binInfo;
    QHash<QString, QAction*> m_a;
    QList<QMenu*> m_menus;
    QString m_projectPath;
    double m_playhead = 0.0;
    double m_in = -1.0;
    double m_out = -1.0;
    bool m_playing = false;
    QTemporaryDir m_exportTemp;
};

} // namespace mf
