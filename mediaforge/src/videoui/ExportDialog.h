#pragma once

#include "media/ExportBuilder.h"

#include <QDialog>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QRadioButton;
class QSpinBox;

namespace mf {

class VideoExportDialog : public QDialog {
    Q_OBJECT
public:
    VideoExportDialog(const TimelineState& st, double rangeIn, double rangeOut, const QString& suggestedName, QWidget* parent);
    VideoExportSettings settings() const;
    void accept() override;

private:
    void updateFormat();
    void updateEstimate();
    QSize chosenSize() const;
    double chosenFps() const;

    TimelineState m_state;
    double m_in, m_out;
    QComboBox* m_format;
    QComboBox* m_res;
    QSpinBox* m_w;
    QSpinBox* m_h;
    QComboBox* m_fps;
    QComboBox* m_quality;
    QSpinBox* m_crf;
    QComboBox* m_preset;
    QComboBox* m_audioBr;
    QRadioButton* m_all;
    QRadioButton* m_range;
    QLineEdit* m_path;
    QLabel* m_estimate;
    double m_estimateMb = 0.0;
};

} // namespace mf
