#pragma once

#include "image/Filters.h"
#include "image/Layer.h"

#include <QDialog>
#include <QFutureWatcher>
#include <QTimer>
#include <QVariantMap>

#include <functional>

class QCheckBox;
class QComboBox;
class QFontComboBox;
class QLabel;
class QPlainTextEdit;
class QSpinBox;
class QToolButton;

namespace mf {

class Document;

// Shows a live preview of an operation on the active layer while a dialog is
// open. The heavy work runs on a worker thread; only the newest request wins.
class PreviewRunner : public QObject {
    Q_OBJECT
public:
    using Fn = std::function<QImage(const QImage&)>;
    PreviewRunner(Document* doc, bool respectSelection, QObject* parent = nullptr);
    ~PreviewRunner() override;

    void request(Fn fn);
    void showOriginal();
    bool commit(const QString& title, QWidget* busyParent);
    void cancel();

signals:
    void busyChanged(bool busy);

private:
    void startJob();
    void onJobDone();

    Document* m_doc;
    int m_layer;
    QImage m_src;
    QImage m_mask;
    Fn m_latest;
    quint64 m_requested = 0;
    quint64 m_running = 0;
    quint64 m_shown = 0;
    bool m_finished = false;
    QTimer m_debounce;
    QFutureWatcher<QImage> m_watcher;
};

struct FilterParam {
    QString key;
    QString label;
    double min = 0;
    double max = 100;
    double value = 0;
    int decimals = 0;
    QString suffix;
    QStringList choices; // non-empty → combo box
    bool isBool = false;
};

class FilterDialog : public QDialog {
    Q_OBJECT
public:
    using Fn = std::function<QImage(const QImage&, const QVariantMap&)>;
    FilterDialog(Document* doc, const QString& title, const QVector<FilterParam>& params, Fn fn, QWidget* parent,
                 bool respectSelection = true);
    QVariantMap values() const;

    void accept() override;
    void reject() override;

private:
    void schedule();

    Document* m_doc;
    QString m_title;
    QVector<FilterParam> m_params;
    QVector<QWidget*> m_editors;
    Fn m_fn;
    PreviewRunner* m_runner;
    QCheckBox* m_preview;
    QLabel* m_busy;
};

class CurvesWidget : public QWidget {
    Q_OBJECT
public:
    explicit CurvesWidget(QWidget* parent = nullptr);
    void setPoints(const QVector<QPointF>& pts);
    QVector<QPointF> points() const { return m_points; }
    void setHistogram(const QVector<int>& hist, QColor color);
    QSize sizeHint() const override { return QSize(300, 300); }

signals:
    void changed();

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;

private:
    QRectF plot() const;
    QPointF toWidget(QPointF v) const;
    QPointF toValue(QPointF w) const;

    QVector<QPointF> m_points;
    QVector<int> m_hist;
    QColor m_color = Qt::white;
    int m_drag = -1;
};

class CurvesDialog : public QDialog {
    Q_OBJECT
public:
    CurvesDialog(Document* doc, QWidget* parent);
    void accept() override;
    void reject() override;

private:
    void switchChannel(int ch);
    void schedule();

    Document* m_doc;
    PreviewRunner* m_runner;
    CurvesWidget* m_curve;
    QComboBox* m_channel;
    QVector<QPointF> m_pts[4];
    QVector<int> m_hist[4];
    int m_current = 0;
};

class TextDialog : public QDialog {
    Q_OBJECT
public:
    TextDialog(const TextInfo& initial, QWidget* parent);
    TextInfo result() const;

private:
    void updateColorButton();

    TextInfo m_info;
    QPlainTextEdit* m_text;
    QFontComboBox* m_font;
    QSpinBox* m_size;
    QCheckBox* m_bold;
    QCheckBox* m_italic;
    QToolButton* m_color;
};

struct NewImageSpec {
    QSize size;
    QColor background;
};
bool askNewImage(QWidget* parent, QColor bg, QColor fg, NewImageSpec* out);
bool askImageSize(QWidget* parent, QSize current, QSize* out, bool* smooth);
bool askCanvasSize(QWidget* parent, QSize current, QSize* out, int* anchor, bool* fillBackground);

} // namespace mf
