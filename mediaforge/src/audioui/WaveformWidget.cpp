#include "audioui/WaveformWidget.h"

#include "app/Theme.h"

#include <QDropEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>

#include <cmath>

namespace mf {

namespace {
constexpr int kBucket = 256;
constexpr int kRulerH = 22;
constexpr int kBarH = 14;
} // namespace

WaveformWidget::WaveformWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumHeight(160);
    setMouseTracking(true);
    setAcceptDrops(true);
    setFocusPolicy(Qt::ClickFocus);
    m_bar = new QScrollBar(Qt::Horizontal, this);
    connect(m_bar, &QScrollBar::valueChanged, this, [this](int v) {
        m_offset = qint64(v * m_fpp);
        update();
    });
}

void WaveformWidget::setBuffer(const PcmBuffer* pcm)
{
    m_pcm = pcm;
    bufferChanged();
    zoomToFit();
}

void WaveformWidget::bufferChanged()
{
    rebuildPeaks();
    const qint64 n = m_pcm ? m_pcm->frames() : 0;
    m_playhead = std::clamp<qint64>(m_playhead, 0, n);
    m_selA = std::clamp<qint64>(m_selA, 0, n);
    m_selB = std::clamp<qint64>(m_selB, 0, n);
    if (m_fit)
        zoomToFit();
    updateScrollBar();
    update();
}

void WaveformWidget::rebuildPeaks()
{
    m_peaks.clear();
    if (!m_pcm || m_pcm->isEmpty())
        return;
    const int ch = m_pcm->channels;
    const qint64 n = m_pcm->frames();
    const qint64 buckets = (n + kBucket - 1) / kBucket;
    m_peaks.resize(ch);
    for (int c = 0; c < ch; ++c)
        m_peaks[c].resize(buckets * 2);
    const float* d = m_pcm->data.constData();
    for (qint64 b = 0; b < buckets; ++b) {
        const qint64 e = std::min(n, (b + 1) * kBucket);
        for (int c = 0; c < ch; ++c) {
            float mn = 0, mx = 0;
            for (qint64 i = b * kBucket; i < e; ++i) {
                const float v = d[i * ch + c];
                mn = std::min(mn, v);
                mx = std::max(mx, v);
            }
            m_peaks[c][2 * b] = mn;
            m_peaks[c][2 * b + 1] = mx;
        }
    }
}

qint64 WaveformWidget::frameAtX(double x) const
{
    const qint64 n = m_pcm ? m_pcm->frames() : 0;
    return std::clamp<qint64>(m_offset + qint64(x * m_fpp), 0, n);
}

double WaveformWidget::xOfFrame(qint64 f) const
{
    return (f - m_offset) / m_fpp;
}

void WaveformWidget::updateScrollBar()
{
    const qint64 n = m_pcm ? m_pcm->frames() : 0;
    const int total = int(n / m_fpp);
    m_bar->setRange(0, std::max(0, total - width()));
    m_bar->setPageStep(std::max(1, width()));
    m_bar->setValue(int(m_offset / m_fpp));
}

void WaveformWidget::zoomAround(double factor, double x)
{
    if (!m_pcm || m_pcm->isEmpty())
        return;
    const qint64 f = frameAtX(x);
    const double maxFpp = std::max(1.0, double(m_pcm->frames()) / std::max(1, width()));
    m_fpp = std::clamp(m_fpp / factor, 1.0 / 16, maxFpp);
    m_fit = false;
    m_offset = std::max<qint64>(0, f - qint64(x * m_fpp));
    updateScrollBar();
    update();
}

void WaveformWidget::zoomIn()
{
    zoomAround(2.0, std::clamp(xOfFrame(m_playhead), 0.0, double(width())));
}

void WaveformWidget::zoomOut()
{
    zoomAround(0.5, std::clamp(xOfFrame(m_playhead), 0.0, double(width())));
}

void WaveformWidget::zoomToFit()
{
    const qint64 n = m_pcm ? m_pcm->frames() : 0;
    m_fpp = std::max(1.0, double(n) / std::max(1, width()));
    m_offset = 0;
    m_fit = true;
    updateScrollBar();
    update();
}

void WaveformWidget::zoomToSelection()
{
    if (!hasSelection())
        return;
    m_fpp = std::max(1.0 / 16, double(m_selB - m_selA) / std::max(1, width() - 20));
    m_fit = false;
    m_offset = std::max<qint64>(0, m_selA - qint64(10 * m_fpp));
    updateScrollBar();
    update();
}

void WaveformWidget::setPlayhead(qint64 frame, bool follow)
{
    m_playhead = frame;
    if (follow) {
        const double x = xOfFrame(frame);
        if (x < 0 || x > width() - 10)
            m_bar->setValue(int(std::max<qint64>(0, frame - qint64(width() * 0.1 * m_fpp)) / m_fpp));
    }
    update();
}

void WaveformWidget::setSelection(qint64 a, qint64 b)
{
    if (b < a)
        std::swap(a, b);
    const qint64 n = m_pcm ? m_pcm->frames() : 0;
    m_selA = std::clamp<qint64>(a, 0, n);
    m_selB = std::clamp<qint64>(b, 0, n);
    update();
    emit selectionChanged();
}

void WaveformWidget::resizeEvent(QResizeEvent*)
{
    m_bar->setGeometry(0, height() - kBarH, width(), kBarH);
    const qint64 n = m_pcm ? m_pcm->frames() : 0;
    if (m_fpp * width() > n)
        zoomToFit();
    else
        updateScrollBar();
}

void WaveformWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(24, 24, 26));
    if (!m_pcm || m_pcm->isEmpty()) {
        p.setPen(theme::dimText());
        p.drawText(rect(), Qt::AlignCenter,
                   QStringLiteral("Откройте аудиофайл (Ctrl+O) или видео, чтобы редактировать его звук.\n"
                                  "Можно перетащить файл сюда."));
        return;
    }
    const int rate = m_pcm->rate;
    const int ch = m_pcm->channels;
    const QRect area(0, kRulerH, width(), height() - kRulerH - kBarH);

    // Ruler.
    p.fillRect(QRect(0, 0, width(), kRulerH), QColor(36, 36, 39));
    const double secPerPx = m_fpp / rate;
    static const double steps[] = {0.001, 0.005, 0.01, 0.05, 0.1, 0.25, 0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300, 600};
    double step = 600;
    for (double s : steps)
        if (s / secPerPx >= 80) {
            step = s;
            break;
        }
    p.setPen(theme::dimText());
    const double t0 = std::floor(m_offset / double(rate) / step) * step;
    for (double t = t0;; t += step) {
        const double x = xOfFrame(qint64(t * rate));
        if (x > width())
            break;
        if (x < 0)
            continue;
        p.drawLine(QPointF(x, kRulerH - 8), QPointF(x, kRulerH));
        const int m = int(t) / 60;
        const double s = t - m * 60;
        const int decimals = step < 0.01 ? 3 : 2;
        p.drawText(QPointF(x + 3, 14), step < 1 ? QStringLiteral("%1:%2").arg(m).arg(s, 3 + decimals, 'f', decimals, QLatin1Char('0'))
                                                : QStringLiteral("%1:%2").arg(m).arg(int(s), 2, 10, QLatin1Char('0')));
    }

    // Selection.
    if (hasSelection()) {
        const double x0 = xOfFrame(m_selA), x1 = xOfFrame(m_selB);
        p.fillRect(QRectF(x0, area.top(), x1 - x0, area.height()), QColor(61, 142, 232, 70));
    }

    const double laneH = area.height() / double(ch);
    for (int c = 0; c < ch; ++c) {
        const double mid = area.top() + laneH * (c + 0.5);
        const double amp = laneH / 2 - 3;
        p.setPen(QColor(60, 60, 66));
        p.drawLine(QPointF(0, mid), QPointF(width(), mid));
        if (c > 0) {
            p.setPen(QColor(45, 45, 50));
            p.drawLine(QPointF(0, area.top() + laneH * c), QPointF(width(), area.top() + laneH * c));
        }
        p.setPen(QColor(110, 200, 150));
        const float* d = m_pcm->data.constData();
        const qint64 n = m_pcm->frames();
        if (m_fpp < 1.0) {
            // Very deep zoom: draw individual samples as a polyline.
            QPolygonF poly;
            for (qint64 f = m_offset; f < n; ++f) {
                const double x = xOfFrame(f);
                if (x > width())
                    break;
                poly << QPointF(x, mid - d[f * ch + c] * amp);
            }
            p.drawPolyline(poly);
            continue;
        }
        for (int x = 0; x < width(); ++x) {
            const qint64 a = m_offset + qint64(x * m_fpp);
            const qint64 b = std::min(n, m_offset + qint64((x + 1) * m_fpp));
            if (a >= n)
                break;
            float mn = 0, mx = 0;
            if (m_fpp >= kBucket * 2) {
                for (qint64 k = a / kBucket; k < std::max(a / kBucket + 1, b / kBucket); ++k) {
                    mn = std::min(mn, m_peaks[c][2 * k]);
                    mx = std::max(mx, m_peaks[c][2 * k + 1]);
                }
            } else {
                for (qint64 f = a; f < std::max(a + 1, b); ++f) {
                    mn = std::min(mn, d[f * ch + c]);
                    mx = std::max(mx, d[f * ch + c]);
                }
            }
            p.drawLine(QPointF(x + 0.5, mid - mx * amp), QPointF(x + 0.5, mid - mn * amp));
        }
    }

    const double px = xOfFrame(m_playhead);
    p.setPen(QPen(QColor(235, 70, 70), 1.5));
    p.drawLine(QPointF(px, 0), QPointF(px, area.bottom()));
}

void WaveformWidget::mousePressEvent(QMouseEvent* e)
{
    if (!m_pcm || e->button() != Qt::LeftButton)
        return;
    const qint64 f = frameAtX(e->position().x());
    if ((e->modifiers() & Qt::ShiftModifier) && hasSelection()) {
        m_anchor = std::abs(f - m_selA) < std::abs(f - m_selB) ? m_selB : m_selA;
        setSelection(m_anchor, f);
    } else {
        m_anchor = f;
        setSelection(f, f);
        m_playhead = f;
        emit playheadMoved(f);
    }
    m_dragging = true;
}

void WaveformWidget::mouseMoveEvent(QMouseEvent* e)
{
    if (!m_dragging)
        return;
    double x = e->position().x();
    // Auto-scroll when dragging past the edges.
    if (x < 0)
        m_bar->setValue(m_bar->value() + int(x / 4) - 1);
    else if (x > width())
        m_bar->setValue(m_bar->value() + int((x - width()) / 4) + 1);
    setSelection(m_anchor, frameAtX(x));
}

void WaveformWidget::mouseReleaseEvent(QMouseEvent*)
{
    m_dragging = false;
}

void WaveformWidget::wheelEvent(QWheelEvent* e)
{
    const QPoint d = e->angleDelta();
    if (e->modifiers() & Qt::ControlModifier)
        zoomAround(std::pow(1.002, d.y()), e->position().x());
    else
        m_bar->setValue(m_bar->value() - (d.x() ? d.x() : d.y()));
    e->accept();
}

void WaveformWidget::dragEnterEvent(QDragEnterEvent* e)
{
    if (e->mimeData()->hasUrls())
        e->acceptProposedAction();
}

void WaveformWidget::dropEvent(QDropEvent* e)
{
    QStringList paths;
    for (const QUrl& u : e->mimeData()->urls())
        if (u.isLocalFile())
            paths << u.toLocalFile();
    if (!paths.isEmpty())
        emit filesDropped(paths);
}

} // namespace mf
