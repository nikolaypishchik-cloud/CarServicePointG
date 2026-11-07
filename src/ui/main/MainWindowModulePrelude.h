#pragma once

// P2 architecture module prelude.
// Generated during monolith-removal p2 so converted implementation slices
// compile as separate .cpp modules without being text-included by the owner.
// Owner: src/ui/main/MainWindow.cpp

#include "ui/main/MainWindow.h"
#include "ui/main/MainWindowSupport.h"
#include "app/AppContext.h"
#include "core/diagnostics/RuntimeLoggingControl.h"
#include "core/backup/BackupPerformanceGuard.h"
#include "app/update/UpdateService.h"
#include "ui/common/widgets/UnpaidBannerWidget.h"
#include "ui/common/widgets/GlossyButton.h"
#include "ui/common/widgets/GraphiteShimmerOverlay.h"
#include "ui/common/widgets/AnimatedMetalLogoWidget.h"
#include "ui/common/AdaptiveUi.h"
#include "ui/tabs/schedule/PlanningPage.h"
#include "ui/tabs/orders/CurrentOrdersPage.h"
#include "ui/tabs/masters/MastersPage.h"
#include "ui/tabs/history/HistoryPage.h"
#include "ui/tabs/parts/PartsPage.h"
#include "ui/tabs/inventory/WorkshopInventoryPage.h"
#include "ui/tabs/reports/ReportsPage.h"
#include "ui/tabs/statistics/StatsPage.h"
#include "ui/tabs/catalog/CatalogPage.h"
#include "ui/tabs/settings/SettingsPage.h"
#include "ui/tabs/settings/PhoneSyncSettingsPage.h"
#include "ui/tabs/crm/CrmPage.h"
#include "ui/tabs/cloudsync/CloudSyncPage.h"
#include "ui/tabs/booking/OnlineBookingPage.h"
#include "ui/tabs/intake/VehicleIntakePage.h"
#include "ui/common/dialogs/OrderDialog.h"
#include "core/util/FormatUtils.h"
#include "core/util/PlateUtils.h"
#include "core/services/PartsQueryService.h"
#include "ui/theme/ThemeManager.h"
#include "core/services/settings/GeneralSettingsService.h"
#include "core/services/MainWindowMetricsService.h"
#include "ui/runtime/PageActivationCoordinator.h"
#include "ui/runtime/PageFreezeController.h"
#include <QApplication>
#include <QAbstractButton>
#include <QIcon>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QSpacerItem>
#include <QScrollArea>
#include <QResizeEvent>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <QMoveEvent>
#include <QEasingCurve>
#include <QGuiApplication>
#include <QScreen>
#include <QPointer>
#include <QWindow>
#include <QShowEvent>
#include <QEvent>
#include <exception>
#include <new>
#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <QPen>
#include <QElapsedTimer>
#include <QPaintEvent>
#include <QRandomGenerator>
#include <QtMath>
#include <memory>

using namespace MainWindowSupport;

// File-local helpers required by converted slices.
namespace {
static bool shouldUseFullPageFade(int idx)
{
    Q_UNUSED(idx);
    // Full-page fade adds measurable latency to tab activation and does not
    // bring enough UX value to justify the extra 40-90 ms in hot navigation
    // paths. Keep micro-animations inside pages, but make tab activation direct.
    return false;
}

class SidebarMenuButton : public QPushButton
{
public:
    explicit SidebarMenuButton(const QString& text, QWidget* parent = nullptr)
        : QPushButton(text, parent)
    {
        setCheckable(true);
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover, true);

        m_twinkleTimer.setInterval(33);
        m_pressAnim.setTargetObject(this);
        m_pressAnim.setPropertyName("pos");

        connect(&m_twinkleTimer, &QTimer::timeout, this, [this]() {
            if (isChecked()) update();
        });
        connect(this, &QPushButton::toggled, this, [this](bool checked) {
            if (checked) {
                resetSparkCycle(true);
                if (!m_twinkleTimer.isActive()) m_twinkleTimer.start();
            } else {
                m_twinkleTimer.stop();
            }
            update();
        });
    }

protected:
    void showEvent(QShowEvent* event) override
    {
        QPushButton::showEvent(event);
        if (!m_pressedVisual) m_restPos = pos();
        if (isChecked() && !m_twinkleClock.isValid()) {
            resetSparkCycle(true);
        }
    }

    void moveEvent(QMoveEvent* event) override
    {
        QPushButton::moveEvent(event);
        if (!m_pressedVisual && m_pressAnim.state() != QAbstractAnimation::Running) {
            m_restPos = pos();
        }
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if (event && event->button() == Qt::LeftButton) {
            if (m_pressAnim.state() == QAbstractAnimation::Running) m_pressAnim.stop();
            m_restPos = pos();
            m_pressedVisual = true;
            animateTo(m_restPos + QPoint(7, 4), 165, QEasingCurve::OutCubic);
        }
        QPushButton::mousePressEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        QPushButton::mouseReleaseEvent(event);
        m_pressedVisual = false;
        animateTo(m_restPos, 300, QEasingCurve::OutBack);
    }

    void leaveEvent(QEvent* event) override
    {
        QPushButton::leaveEvent(event);
        if (!isDown() && m_pressedVisual) {
            m_pressedVisual = false;
            animateTo(m_restPos, 300, QEasingCurve::OutBack);
        }
    }

    void paintEvent(QPaintEvent* event) override
    {
        const bool ordersButton = objectName() == QStringLiteral("sb_btn_orders");
        if (!isChecked() && !ordersButton) {
            QPushButton::paintEvent(event);
            return;
        }

        Q_UNUSED(event);
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const QRectF r = rect().adjusted(1.0, 1.0, -1.0, -1.0);
        const qreal radius = ordersButton && !isChecked() ? 0.0 : qMin<qreal>(18.0, r.height() * 0.44);

        QPainterPath clip;
        clip.addRoundedRect(r, radius, radius);
        p.setClipPath(clip);

        const bool pressed = isDown() || m_pressedVisual;
        const bool hovered = underMouse();

        QLinearGradient base(r.topLeft(), r.bottomLeft());
        if (ordersButton) {
            base.setColorAt(0.00, QColor(255, 253, 204));
            base.setColorAt(0.16, hovered ? QColor(255, 242, 95) : QColor(255, 229, 48));
            base.setColorAt(0.48, pressed ? QColor(236, 184, 0) : QColor(255, 218, 0));
            base.setColorAt(0.50, QColor(235, 181, 0));
            base.setColorAt(1.00, QColor(190, 132, 0));
        } else if (pressed) {
            base.setColorAt(0.00, QColor(248, 251, 255, 242));
            base.setColorAt(0.08, QColor(108, 116, 130, 238));
            base.setColorAt(0.14, QColor(8, 10, 14, 252));
            base.setColorAt(0.58, QColor(0, 0, 0, 255));
            base.setColorAt(1.00, QColor(0, 0, 0, 255));
        } else if (hovered) {
            base.setColorAt(0.00, QColor(252, 254, 255, 246));
            base.setColorAt(0.08, QColor(142, 150, 164, 242));
            base.setColorAt(0.14, QColor(10, 12, 17, 252));
            base.setColorAt(0.58, QColor(0, 0, 0, 255));
            base.setColorAt(1.00, QColor(0, 0, 0, 255));
        } else {
            base.setColorAt(0.00, QColor(248, 250, 253, 244));
            base.setColorAt(0.08, QColor(126, 134, 149, 240));
            base.setColorAt(0.14, QColor(8, 10, 15, 252));
            base.setColorAt(0.58, QColor(0, 0, 0, 255));
            base.setColorAt(1.00, QColor(0, 0, 0, 255));
        }
        p.fillPath(clip, base);

        const QRectF glossRect = r.adjusted(2.0, 2.0, -2.0, -r.height() * 0.34);
        QPainterPath glossPath;
        glossPath.addRoundedRect(glossRect, qMax(0.0, radius - 2.0), qMax(0.0, radius - 2.0));
        QLinearGradient gloss(glossRect.topLeft(), glossRect.bottomLeft());
        gloss.setColorAt(0.00, QColor(255, 255, 255, hovered ? 255 : 248));
        gloss.setColorAt(0.14, QColor(255, 255, 255, hovered ? 208 : 194));
        gloss.setColorAt(0.30, QColor(255, 255, 255, hovered ? 112 : 102));
        gloss.setColorAt(0.52, QColor(255, 255, 255, 28));
        gloss.setColorAt(1.00, QColor(255, 255, 255, 0));
        p.fillPath(glossPath, gloss);

        const QRectF glossStrip = r.adjusted(3.0, r.height() * 0.06, -3.0, -r.height() * 0.59);
        QLinearGradient strip(glossStrip.topLeft(), glossStrip.bottomLeft());
        strip.setColorAt(0.00, QColor(255, 255, 255, 182));
        strip.setColorAt(0.34, QColor(255, 255, 255, 74));
        strip.setColorAt(1.00, QColor(255, 255, 255, 0));
        p.fillRect(glossStrip, strip);

        const QRectF razorBand = r.adjusted(5.0, r.height() * 0.11, -5.0, -r.height() * 0.71);
        QLinearGradient razor(razorBand.topLeft(), razorBand.bottomLeft());
        razor.setColorAt(0.00, QColor(255, 255, 255, 210));
        razor.setColorAt(0.45, QColor(255, 255, 255, 54));
        razor.setColorAt(1.00, QColor(255, 255, 255, 0));
        p.fillRect(razorBand, razor);

        QLinearGradient lowerSheen(r.left(), r.top() + r.height() * 0.44, r.left(), r.bottom());
        lowerSheen.setColorAt(0.00, QColor(255, 255, 255, 0));
        lowerSheen.setColorAt(0.54, QColor(255, 255, 255, 16));
        lowerSheen.setColorAt(0.78, QColor(8, 12, 18, 72));
        lowerSheen.setColorAt(1.00, QColor(0, 0, 0, 138));
        p.fillPath(clip, lowerSheen);

        QRadialGradient hotspot(QPointF(r.left() + r.width() * 0.34, r.top() + r.height() * 0.18), r.width() * 0.92);
        hotspot.setColorAt(0.00, QColor(255, 255, 255, hovered ? 92 : 82));
        hotspot.setColorAt(0.22, QColor(242, 246, 252, 42));
        hotspot.setColorAt(0.56, QColor(255, 255, 255, 12));
        hotspot.setColorAt(1.00, QColor(255, 255, 255, 0));
        p.fillPath(clip, hotspot);

        QPen rim(QColor(250, 252, 255, 248));
        rim.setWidthF(1.35);
        p.setPen(rim);
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(r.adjusted(0.35, 0.35, -0.35, -0.35), radius, radius);

        QPen innerRim(QColor(120, 140, 172, 95));
        innerRim.setWidthF(0.95);
        p.setPen(innerRim);
        p.drawRoundedRect(r.adjusted(1.55, 1.55, -1.55, -1.55), qMax(0.0, radius - 1.55), qMax(0.0, radius - 1.55));

        drawTwinkles(p, r);

        p.setClipping(false);
        QFont f = font();
        f.setBold(true);
        if (ordersButton) f.setPointSizeF(QFontInfo(f).pointSizeF() + 2.0);
        p.setFont(f);
        p.setPen(QPen(ordersButton ? QColor(255, 247, 183, 190) : QColor(8, 10, 14, 225), 2.9, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        QRectF textRect = r.adjusted(14.0, 0.0, -14.0, 0.0);
        p.drawText(textRect.translated(0.0, 1.05), Qt::AlignCenter, text());
        p.setPen(ordersButton ? QColor(0, 0, 0) : QColor(252, 253, 255));
        p.drawText(textRect, Qt::AlignCenter, text());
    }

private:
    struct Spark {
        QPointF anchor;
        QPointF drift;
        qreal baseSize = 5.0;
        int delayMs = 0;
        int durationMs = 900;
        qreal brightness = 1.0;
    };

    void animateTo(const QPoint& target, int duration, QEasingCurve::Type easing)
    {
        m_pressAnim.stop();
        m_pressAnim.setDuration(duration);
        m_pressAnim.setStartValue(pos());
        m_pressAnim.setEndValue(target);
        m_pressAnim.setEasingCurve(easing);
        m_pressAnim.start();
    }

    static void drawStar(QPainter& p, const QPointF& c, qreal size, qreal alpha)
    {
        if (alpha <= 1.0 || size <= 1.0) return;
        const int aCore = qBound(0, int(alpha), 255);
        const int aHalo = qBound(0, int(alpha * 0.28), 255);
        const int aRays = qBound(0, int(alpha * 0.64), 255);

        QRadialGradient halo(c, size * 2.2);
        halo.setColorAt(0.00, QColor(255, 255, 255, aCore));
        halo.setColorAt(0.18, QColor(246, 250, 255, qBound(0, int(alpha * 0.56), 255)));
        halo.setColorAt(0.48, QColor(210, 226, 248, aHalo));
        halo.setColorAt(1.00, QColor(255, 255, 255, 0));
        p.setPen(Qt::NoPen);
        p.setBrush(halo);
        p.drawEllipse(c, size * 2.0, size * 2.0);

        QPainterPath starPath;
        for (int i = 0; i < 8; ++i) {
            const qreal angle = qDegreesToRadians(22.5 + i * 45.0);
            const qreal longLen = (i % 2 == 0) ? size * 2.35 : size * 1.55;
            const QPointF end(c.x() + std::cos(angle) * longLen,
                              c.y() + std::sin(angle) * longLen);
            starPath.moveTo(c);
            starPath.lineTo(end);
        }

        QPen rayPen(QColor(255, 255, 255, aRays));
        rayPen.setWidthF(1.25);
        rayPen.setCapStyle(Qt::RoundCap);
        p.setPen(rayPen);
        p.drawPath(starPath);

        QPen softPen(QColor(214, 230, 252, qBound(0, int(alpha * 0.28), 255)));
        softPen.setWidthF(2.2);
        softPen.setCapStyle(Qt::RoundCap);
        p.setPen(softPen);
        p.drawLine(QLineF(c.x() - size * 0.95, c.y(), c.x() + size * 0.95, c.y()));
        p.drawLine(QLineF(c.x(), c.y() - size * 0.95, c.x(), c.y() + size * 0.95));

        p.setBrush(QColor(255, 255, 255, qBound(0, int(alpha * 0.75), 255)));
        p.setPen(Qt::NoPen);
        p.drawEllipse(c, size * 0.30, size * 0.30);
    }

    void resetSparkCycle(bool immediate = false)
    {
        m_sparks.clear();
        auto* rng = QRandomGenerator::global();
        auto boundedReal = [rng](double low, double high) -> double {
            const double span = qMax(0.0, high - low);
            return low + rng->generateDouble() * span;
        };

        int offset = immediate ? 180 : rng->bounded(900, 2200);
        const int count = 3;
        for (int i = 0; i < count; ++i) {
            Spark s;
            s.anchor = QPointF(boundedReal(0.17, 0.84), boundedReal(0.16, 0.40));
            s.drift = QPointF(boundedReal(-0.06, 0.06), boundedReal(-0.025, 0.025));
            s.baseSize = boundedReal(4.8, 8.2);
            s.delayMs = offset;
            s.durationMs = rng->bounded(980, 1650);
            s.brightness = boundedReal(0.72, 0.98);
            offset += s.durationMs + rng->bounded(1800, 3600);
            m_sparks.push_back(s);
        }

        m_cycleDurationMs = offset + rng->bounded(1400, 2600);
        m_twinkleClock.restart();
    }

    void drawTwinkles(QPainter& p, const QRectF& r)
    {
        if (!m_twinkleClock.isValid()) {
            resetSparkCycle(true);
        }

        const qint64 ms = m_twinkleClock.elapsed();
        if (ms >= m_cycleDurationMs) {
            resetSparkCycle(false);
        }

        for (const Spark& s : m_sparks) {
            if (ms < s.delayMs || ms > s.delayMs + s.durationMs) continue;
            const qreal t = qBound(0.0, qreal(ms - s.delayMs) / qreal(s.durationMs), 1.0);
            const qreal eased = std::sin(t * M_PI);
            const QPointF c(r.left() + r.width() * (s.anchor.x() + s.drift.x() * (t - 0.5)),
                            r.top() + r.height() * (s.anchor.y() + s.drift.y() * (t - 0.5)));
            const qreal size = s.baseSize + eased * 2.8;
            const qreal alpha = (92.0 + eased * 108.0) * s.brightness;
            drawStar(p, c, size, alpha);
        }
    }

    QTimer m_twinkleTimer;
    QElapsedTimer m_twinkleClock;
    QVector<Spark> m_sparks;
    int m_cycleDurationMs = 0;
    QPropertyAnimation m_pressAnim;
    QPoint m_restPos;
    bool m_pressedVisual = false;
};
static QPushButton* mkMenuButton(const QString& text, QButtonGroup* group, QWidget* parent, bool checked=false)
{
    auto* b = new SidebarMenuButton(text, parent);
    b->setCheckable(true);
    b->setChecked(checked);
    b->setMinimumHeight(33);
    b->setMaximumHeight(33);
    b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    b->setCursor(Qt::PointingHandCursor);
    b->setObjectName(QStringLiteral("sb_btn_uniform"));
    group->addButton(b);
    return b;
}

}
