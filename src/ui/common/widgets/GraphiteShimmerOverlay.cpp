#include "ui/common/widgets/GraphiteShimmerOverlay.h"

#include <QApplication>
#include <QEvent>
#include <QPainter>
#include <QRandomGenerator>
#include <QtMath>

namespace {
static constexpr double kPi = 3.14159265358979323846;

static QPointF normalized(const QPointF& v)
{
    const qreal len = std::hypot(v.x(), v.y());
    if (len <= 0.0001) return QPointF(1.0, 0.0);
    return QPointF(v.x() / len, v.y() / len);
}
}

GraphiteShimmerOverlay::GraphiteShimmerOverlay(QWidget* targetParent)
    : QWidget(targetParent)
    , m_targetParent(targetParent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setFocusPolicy(Qt::NoFocus);
    hide();

    if (m_targetParent) {
        m_targetParent->installEventFilter(this);
        syncToParent();
    }

    m_waitTimer.setSingleShot(true);
    connect(&m_waitTimer, &QTimer::timeout, this, &GraphiteShimmerOverlay::startRandomPass);

    m_frameTimer.setInterval(33);
    connect(&m_frameTimer, &QTimer::timeout, this, [this]() {
        if (!m_animClock.isValid()) {
            m_frameTimer.stop();
            hide();
            scheduleNext();
            return;
        }
        if (m_animClock.elapsed() >= m_durationMs) {
            m_frameTimer.stop();
            hide();
            scheduleNext();
            return;
        }
        update();
    });

    scheduleNext();
}

bool GraphiteShimmerOverlay::shimmerEnabled() const
{
    return qApp->property("carservice.graphiteThemeActive").toBool();
}

void GraphiteShimmerOverlay::syncToParent()
{
    if (!m_targetParent) return;
    setGeometry(m_targetParent->rect());
    lower();
}

void GraphiteShimmerOverlay::scheduleNext()
{
    if (!m_targetParent) return;
    const int delayMs = QRandomGenerator::global()->bounded(45000, 60001);
    m_waitTimer.start(delayMs);
}

void GraphiteShimmerOverlay::startRandomPass()
{
    if (!m_targetParent) {
        scheduleNext();
        return;
    }

    syncToParent();

    const QRectF r = rect();
    if (r.width() < 10.0 || r.height() < 10.0) {
        scheduleNext();
        return;
    }

    const qreal angleDeg = -32.0 + QRandomGenerator::global()->generateDouble() * 64.0;
    const qreal angle = angleDeg * (kPi / 180.0);
    QPointF dir(std::cos(angle), std::sin(angle));
    if (QRandomGenerator::global()->bounded(2) == 0) {
        dir = QPointF(-dir.x(), -dir.y());
    }
    m_dir = normalized(dir);

    const QPointF center = r.center();
    const qreal travel = std::hypot(r.width(), r.height()) * 0.92;
    m_start = center - m_dir * travel;
    m_end   = center + m_dir * travel;

    m_bandWidth = qMax<qreal>(140.0, std::hypot(r.width(), r.height()) * (0.11 + QRandomGenerator::global()->generateDouble() * 0.05));
    m_peakAlpha = 12.0 + QRandomGenerator::global()->generateDouble() * 16.0;
    m_durationMs = QRandomGenerator::global()->bounded(15000, 20001);

    m_animClock.restart();
    show();
    lower();
    update();
    m_frameTimer.start();
}

bool GraphiteShimmerOverlay::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_targetParent) {
        switch (event->type()) {
        case QEvent::Resize:
        case QEvent::Move:
        case QEvent::Show:
        case QEvent::ZOrderChange:
            syncToParent();
            break;
        default:
            break;
        }
    }
    return QWidget::eventFilter(watched, event);
}


void GraphiteShimmerOverlay::rebuildCacheIfNeeded()
{
    if (m_cachedSize == size() && !m_cachedBase.isNull()) return;
    m_cachedSize = size();
    if (m_cachedSize.isEmpty()) { m_cachedBase = QPixmap(); return; }
    m_cachedBase = QPixmap(m_cachedSize);
    m_cachedBase.fill(Qt::transparent);
}

void GraphiteShimmerOverlay::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    if (!shimmerEnabled() || !m_animClock.isValid()) return;

    const qreal t = qBound<qreal>(0.0, qreal(m_animClock.elapsed()) / qreal(m_durationMs), 1.0);
    const qreal eased = 0.5 - 0.5 * std::cos(t * kPi);
    const QPointF center = m_start + (m_end - m_start) * eased;
    const QPointF dir = normalized(m_dir);

    // Taper in/out so the shimmer feels metallic rather than like a flash.
    const qreal envelope = std::pow(std::sin(t * kPi), 2.15);
    const int alphaWide = qBound(0, int(m_peakAlpha * 0.16 * envelope), 255);
    const int alphaCore = qBound(0, int((1.0 + m_peakAlpha) * envelope), 255);

    rebuildCacheIfNeeded();

    QPainter p(this);
    if (!m_cachedBase.isNull()) p.drawPixmap(0, 0, m_cachedBase);
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setCompositionMode(QPainter::CompositionMode_Screen);

    const QRectF fillRect = rect();

    QLinearGradient wide(center - dir * (m_bandWidth * 1.2), center + dir * (m_bandWidth * 1.2));
    wide.setColorAt(0.00, QColor(255, 255, 255, 0));
    wide.setColorAt(0.36, QColor(205, 220, 240, alphaWide));
    wide.setColorAt(0.50, QColor(255, 255, 255, alphaCore));
    wide.setColorAt(0.64, QColor(205, 220, 240, alphaWide));
    wide.setColorAt(1.00, QColor(255, 255, 255, 0));
    p.fillRect(fillRect, wide);

    QLinearGradient core(center - dir * (m_bandWidth * 0.34), center + dir * (m_bandWidth * 0.34));
    core.setColorAt(0.00, QColor(255, 255, 255, 0));
    core.setColorAt(0.42, QColor(255, 255, 255, int(alphaCore * 0.38)));
    core.setColorAt(0.50, QColor(255, 255, 255, int(alphaCore * 0.72)));
    core.setColorAt(0.58, QColor(255, 255, 255, int(alphaCore * 0.38)));
    core.setColorAt(1.00, QColor(255, 255, 255, 0));
    p.fillRect(fillRect, core);
}
