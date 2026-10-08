#pragma once

#include "media/Timeline.h"

#include <QHash>
#include <QWidget>

class QCheckBox;
class QDoubleSpinBox;
class QGridLayout;
class QLabel;
class QSpinBox;
class QStackedWidget;

namespace mf {

// Edits the selected clip (or project settings when nothing is selected).
// Every field commits through Timeline::updateClip so changes are undoable.
class ClipProperties : public QWidget {
    Q_OBJECT
public:
    explicit ClipProperties(Timeline* tl, QWidget* parent = nullptr);
    void showClip(Track track, quint64 id); // id 0 = project page
    void refresh();

private:
    enum Field { In, Out, Duration, Speed, Volume, Mute, FadeIn, FadeOut, Start, X, Y, Width, Opacity };
    void apply(Field f);
    QDoubleSpinBox* dspin(double min, double max, int decimals, const QString& suffix, double step);
    void addRow(const QString& label, QWidget* field);
    void setRow(QWidget* field, bool visible);

    Timeline* m_tl;
    Track m_track = Track::Main;
    quint64 m_id = 0;
    bool m_updating = false;

    QStackedWidget* m_stack;
    // project page
    QSpinBox* m_projW;
    QSpinBox* m_projH;
    QDoubleSpinBox* m_projFps;
    QCheckBox* m_muteOriginal;
    QLabel* m_projInfo;
    // clip page
    // QGridLayout instead of QFormLayout: hiding QFormLayout rows corrupts memory in Qt 6.4.
    QGridLayout* m_grid;
    QHash<QWidget*, QWidget*> m_labels;
    QLabel* m_name;
    QLabel* m_kind;
    QLabel* m_length;
    QDoubleSpinBox* m_in;
    QDoubleSpinBox* m_out;
    QDoubleSpinBox* m_duration;
    QDoubleSpinBox* m_start;
    QDoubleSpinBox* m_speed;
    QWidget* m_speedButtons;
    QSpinBox* m_volume;
    QCheckBox* m_mute;
    QDoubleSpinBox* m_fadeIn;
    QDoubleSpinBox* m_fadeOut;
    QSpinBox* m_x;
    QSpinBox* m_y;
    QSpinBox* m_width;
    QSpinBox* m_opacity;
};

} // namespace mf
