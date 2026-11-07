#include "core/services/AppRuntimeService.h"
#include "ui/main/MainWindow.h"
#include "core/services/ElitStartupSync.h"
#include <QStatusBar>
#include "ui/main/MainWindowSupport.h"

#include "app/AppContext.h"
#include "core/cloudsync/CloudSyncClient.h"
#include "core/cloudsync/CloudSyncWorker.h"
#include "core/services/ActivityAuditService.h"
#include "core/backup/BackupPerformanceGuard.h"
#include "app/update/UpdateService.h"
#include "ui/common/widgets/UnpaidBannerWidget.h"
#include "ui/common/widgets/GlossyButton.h"
#include "ui/common/widgets/GraphiteShimmerOverlay.h"
#include "ui/common/widgets/AnimatedMetalLogoWidget.h"
#include "ui/common/AdaptiveUi.h"

#include "ui/tabs/schedule/PlanningPage.h"
#include "core/services/AccessControlService.h"
#include "ui/tabs/orders/CurrentOrdersPage.h"
#include "ui/tabs/masters/MastersPage.h"
#include "ui/tabs/history/HistoryPage.h"
#include "ui/tabs/parts/PartsPage.h"
#include "ui/tabs/reports/ReportsPage.h"
#include "ui/tabs/statistics/StatsPage.h"
#include "ui/tabs/catalog/CatalogPage.h"
#include "ui/tabs/settings/SettingsPage.h"
#include "ui/common/dialogs/OrderDialog.h"
#include "ui/common/dialogs/EmailEnvelopeDialog.h"
#include "ui/common/dialogs/PartRequestsDialog.h"
#include "core/util/FormatUtils.h"
#include "core/util/PlateUtils.h"
#include "core/util/SoundSupport.h"
#include "core/services/PartsQueryService.h"
#include "ui/theme/ThemeManager.h"
#include "core/services/settings/GeneralSettingsService.h"
#include "core/services/MainWindowMetricsService.h"
#include "core/services/booking/BookingRecord.h"
#include "core/services/booking/BookingSyncManager.h"
#include "core/services/email/EmailSyncManager.h"
#include "ui/runtime/PageActivationCoordinator.h"
#include "ui/runtime/PageFreezeController.h"

#include <QApplication>
#include <QDebug>
#include <QAbstractButton>
#include <QIcon>
#include <QHBoxLayout>
#include <QGridLayout>
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
#include <QMessageBox>
#include <QDialog>
#include <QJsonArray>
#include <QSettings>
#include <QThread>
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
#if CS_HAS_MULTIMEDIA
#include <QSoundEffect>
#endif

#include <memory>
#include <algorithm>

using namespace MainWindowSupport;

namespace {
static bool shouldUseFullPageFade(int idx)
{
    Q_UNUSED(idx);
    // Full-page fade adds measurable latency to tab activation and does not
    // bring enough UX value to justify the extra 40-90 ms in hot navigation
    // paths. Keep micro-animations inside pages, but make tab activation direct.
    return false;
}

bool onlineBookingNotifySoundsEnabled()
{
    return SoundSupport::soundsEnabled(true);
}

int onlineBookingNotifyVolumePct()
{
    return SoundSupport::soundsVolumePct(95);
}

QByteArray loadOnlineBookingNotifyBytes(QString* loadedFrom)
{
    return SoundSupport::loadAssetBytes(QStringLiteral("online_booking_notify.wav"),
                                        loadedFrom,
                                        QStringLiteral("Online booking notification sound"));
}

#if CS_HAS_MULTIMEDIA
QSoundEffect* onlineBookingNotifyEffectInstance()
{
    static QSoundEffect* effect = []() {
        auto* e = new QSoundEffect();
        SoundSupport::setupEffect(e,
                                  QStringLiteral("online_booking_notify.wav"),
                                  1,
                                  0.95f,
                                  QStringLiteral("Online booking notification sound"));
        return e;
    }();
    return effect;
}
#endif

QString onlineBookingNotifyWavFilePath()
{
    static QString cachedPath;
    if (!cachedPath.isEmpty()) return cachedPath;

    QString loadedFrom;
    const QByteArray bytes = loadOnlineBookingNotifyBytes(&loadedFrom);
    if (bytes.isEmpty()) {
        qWarning() << "Online booking notification sound is empty";
        return QString();
    }

    cachedPath = SoundSupport::ensureTempWavFile(bytes,
                                                 QStringLiteral("online_booking_notify"),
                                                 loadedFrom,
                                                 QStringLiteral("Online booking notification sound loaded from:"));
    return cachedPath;
}

bool playOnlineBookingNativeNotificationSound()
{
    return SoundSupport::playNativeFile(onlineBookingNotifyWavFilePath(), true, false);
}

void playOnlineBookingNotificationSound()
{
    if (!onlineBookingNotifySoundsEnabled()) return;

    const int volumePct = onlineBookingNotifyVolumePct();
    bool played = playOnlineBookingNativeNotificationSound();
#if CS_HAS_MULTIMEDIA
    if (!played) {
        auto* effect = onlineBookingNotifyEffectInstance();
        effect->setMuted(false);
        effect->setVolume(qBound(0, volumePct, 100) / 100.0f);
        effect->stop();
        effect->play();
        played = true;
    }
#endif
    if (!played) {
        QApplication::beep();
        qWarning() << "Online booking notification sound unavailable; beep used";
    }
}


bool emailNotifySoundsEnabled()
{
    return SoundSupport::soundsEnabled(true);
}

int emailNotifyVolumePct()
{
    return SoundSupport::soundsVolumePct(95);
}

QByteArray loadEmailNotifyBytes(QString* loadedFrom)
{
    return SoundSupport::loadAssetBytes(QStringLiteral("email_message_notify.wav"),
                                        loadedFrom,
                                        QStringLiteral("Email notification sound"));
}

#if CS_HAS_MULTIMEDIA
QSoundEffect* emailNotifyEffectInstance()
{
    static QSoundEffect* effect = []() {
        auto* e = new QSoundEffect();
        SoundSupport::setupEffect(e,
                                  QStringLiteral("email_message_notify.wav"),
                                  1,
                                  0.95f,
                                  QStringLiteral("Email notification sound"));
        return e;
    }();
    return effect;
}
#endif

QString emailNotifyWavFilePath()
{
    static QString cachedPath;
    if (!cachedPath.isEmpty()) return cachedPath;

    QString loadedFrom;
    const QByteArray bytes = loadEmailNotifyBytes(&loadedFrom);
    if (bytes.isEmpty()) {
        qWarning() << "Email notification sound is empty";
        return QString();
    }

    cachedPath = SoundSupport::ensureTempWavFile(bytes,
                                                 QStringLiteral("email_message_notify"),
                                                 loadedFrom,
                                                 QStringLiteral("Email notification sound loaded from:"));
    return cachedPath;
}

bool playEmailNativeNotificationSound()
{
    return SoundSupport::playNativeFile(emailNotifyWavFilePath(), true, false);
}

void playEmailNotificationSound()
{
    if (!emailNotifySoundsEnabled()) return;

    const int volumePct = emailNotifyVolumePct();
    bool played = playEmailNativeNotificationSound();
#if CS_HAS_MULTIMEDIA
    if (!played) {
        auto* effect = emailNotifyEffectInstance();
        effect->setMuted(false);
        effect->setVolume(qBound(0, volumePct, 100) / 100.0f);
        effect->stop();
        effect->play();
        played = true;
    }
#endif
    if (!played) {
        QApplication::beep();
        qWarning() << "Email notification sound unavailable; beep used";
    }
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
        if (!isChecked()) {
            QPushButton::paintEvent(event);
            return;
        }

        Q_UNUSED(event);
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const QRectF r = rect().adjusted(1.0, 1.0, -1.0, -1.0);
        const qreal radius = qMin<qreal>(18.0, r.height() * 0.44);

        QPainterPath clip;
        clip.addRoundedRect(r, radius, radius);
        p.setClipPath(clip);

        const bool pressed = isDown() || m_pressedVisual;
        const bool hovered = underMouse();

        QLinearGradient base(r.topLeft(), r.bottomLeft());
        if (pressed) {
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
        glossPath.addRoundedRect(glossRect, radius - 2.0, radius - 2.0);
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
        p.drawRoundedRect(r.adjusted(1.55, 1.55, -1.55, -1.55), radius - 1.55, radius - 1.55);

        drawTwinkles(p, r);

        p.setClipping(false);
        QFont f = font();
        f.setBold(true);
        p.setFont(f);
        p.setPen(QPen(QColor(8, 10, 14, 225), 2.9, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        QRectF textRect = r.adjusted(14.0, 0.0, -14.0, 0.0);
        p.drawText(textRect.translated(0.0, 1.05), Qt::AlignCenter, text());
        p.setPen(QColor(252, 253, 255));
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
}


static QPushButton* mkMenuButton(const QString& text, QButtonGroup* group, QWidget* parent, bool checked=false)
{
    auto* b = new SidebarMenuButton(text, parent);
    b->setCheckable(true);
    b->setChecked(checked);
    b->setMinimumHeight(44);
    b->setMaximumHeight(44);
    b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    b->setCursor(Qt::PointingHandCursor);
    b->setObjectName(QStringLiteral("sb_btn_uniform"));
    group->addButton(b);
    return b;
}


void MainWindow::scheduleGeometryLock(int delayMs)
{
    if (m_geometryLockPending) return;
    m_geometryLockPending = true;
    QPointer<MainWindow> self(this);
    QTimer::singleShot(qMax(0, delayMs), this, [self]() {
        if (!self) return;
        self->m_geometryLockPending = false;
        if (!self->isVisible() || self->isMinimized()) return;
        self->lockToAvailableScreenGeometry();
    });
}

void MainWindow::lockToAvailableScreenGeometry()
{
    if (m_geometryLockReentry) return;

    QScreen* sc = nullptr;
    if (windowHandle() && windowHandle()->screen()) sc = windowHandle()->screen();
    if (!sc) sc = screen() ? screen() : QGuiApplication::primaryScreen();
    if (!sc) return;

    const QRect avail = sc->availableGeometry();
    if (!avail.isValid() || avail.width() < 200 || avail.height() < 200) return;

    // Keep normal-mode geometry inside the Windows work area, but do not fight
    // native maximize/restore behaviour. Caption buttons and window states must
    // remain fully controlled by Windows.
    const QSize adaptiveMin(
        qBound(920, AdaptiveUi::px(this, 1000), qMax(920, avail.width() - 120)),
        qBound(620, AdaptiveUi::px(this, 650), qMax(620, avail.height() - 80)));

    if (isMaximized() || isFullScreen() || isMinimized()) {
        setMinimumSize(adaptiveMin);
        setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
        return;
    }

    const QRect currentGeom = geometry();
    const QRect currentFrame = frameGeometry();
    const int frameLeft = qMax(0, currentGeom.left() - currentFrame.left());
    const int frameTop = qMax(0, currentGeom.top() - currentFrame.top());
    const int frameRight = qMax(0, currentFrame.right() - currentGeom.right());
    const int frameBottom = qMax(0, currentFrame.bottom() - currentGeom.bottom());

    const QRect targetClient = clampNormalWindowFrameToAvailableGeometry(currentFrame, currentGeom, avail);
    QRect targetFrame = targetClient.adjusted(-frameLeft, -frameTop, frameRight, frameBottom);
    const bool samePos = (currentFrame.topLeft() == targetFrame.topLeft());
    const bool sameSize = (qAbs(currentFrame.width() - targetFrame.width()) <= 1
                        && qAbs(currentFrame.height() - targetFrame.height()) <= 1);
    const bool sameClient = (qAbs(currentGeom.width() - targetClient.width()) <= 1
                          && qAbs(currentGeom.height() - targetClient.height()) <= 1
                          && currentGeom.topLeft() == targetClient.topLeft());
    if (samePos && sameSize && sameClient
            && minimumSize() == adaptiveMin
            && maximumSize() == QSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX)) {
        return;
    }

    m_geometryLockReentry = true;
    const bool prevUpdates = updatesEnabled();
    setUpdatesEnabled(false);

    setMinimumSize(adaptiveMin);
    setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    setGeometry(targetClient);

    setUpdatesEnabled(prevUpdates);
    m_geometryLockReentry = false;
}

void MainWindow::showOnlineBookingModal(const BookingRecord& booking)
{
    if (AccessControlService::currentActorIsMaster()) return;
    auto valueOrDash = [](const QString& value) {
        const QString trimmed = value.trimmed();
        return trimmed.isEmpty() ? QString::fromUtf8("—") : trimmed;
    };

    auto carDisplay = [&booking, &valueOrDash]() {
        const QString combined = booking.carMakeModel.trimmed().isEmpty()
            ? QStringLiteral("%1 %2").arg(booking.carMake.trimmed(), booking.carModel.trimmed()).simplified()
            : booking.carMakeModel.trimmed();
        return valueOrDash(combined);
    };

    auto infoValueLabel = [](const QString& text, QWidget* parent) {
        QLabel* label = new QLabel(text, parent);
        label->setWordWrap(true);
        label->setObjectName(QStringLiteral("bookingValueLabel"));
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        return label;
    };

    QDialog dialog(this);
    dialog.setWindowTitle(QString::fromUtf8("Новий онлайн-запис"));
    dialog.setWindowFlags(Qt::Dialog | Qt::WindowStaysOnTopHint | Qt::CustomizeWindowHint | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
    dialog.setWindowModality(Qt::ApplicationModal);
    dialog.setMinimumWidth(560);
    dialog.setStyleSheet(QStringLiteral(
        "QDialog {"
        "  background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #8f9cac, stop:0.08 #2e3948, stop:0.16 #e8eef6, stop:0.24 #7c899b, stop:0.42 #1e2733, stop:0.72 #0b1018, stop:1 #03060b);"
        "  border: 1px solid rgba(248,250,255,215);"
        "  border-radius: 18px;"
        "}"
        "QLabel#bookingTitle { color:#ffffff; font-size:15pt; font-weight:900; }"
        "QLabel#bookingSubtitle { color:#f7f0b2; font-size:10pt; font-weight:800; }"
        "QFrame#bookingVehicleCard {"
        "  background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 rgba(255,255,255,78), stop:0.08 rgba(255,255,255,34), stop:0.50 rgba(68,79,96,138), stop:1 rgba(10,13,18,182));"
        "  border: 1px solid rgba(255,255,255,112);"
        "  border-radius: 14px;"
        "}"
        "QLabel#bookingFieldLabel { color:#dce8f8; font-size:10pt; font-weight:900; }"
        "QLabel#bookingValueLabel { color:#ffffff; font-size:10.5pt; font-weight:800; }"
        "QPushButton {"
        "  min-height: 32px;"
        "  padding: 5px 15px;"
        "  border-radius: 13px;"
        "  border: 1px solid rgba(248,250,255,205);"
        "  color: #ffffff;"
        "  font-size: 10pt;"
        "  font-weight: 900;"
        "  background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #ffffff, stop:0.09 #d8e0ea, stop:0.18 #818ea0, stop:0.36 #2d394a, stop:0.66 #111924, stop:1 #04070c);"
        "}"
        "QPushButton:hover {"
        "  background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #ffffff, stop:0.08 #eef3f9, stop:0.20 #96a3b4, stop:0.42 #374557, stop:0.74 #17202c, stop:1 #081018);"
        "}"
        "QPushButton:pressed {"
        "  padding-top: 7px;"
        "  background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #17212e, stop:1 #020305);"
        "}"));

    auto* root = new QVBoxLayout(&dialog);
    root->setContentsMargins(18, 16, 18, 18);
    root->setSpacing(12);

    auto* title = new QLabel(QString::fromUtf8("Новий онлайн-запис"), &dialog);
    title->setObjectName(QStringLiteral("bookingTitle"));
    auto* subtitle = new QLabel(QString::fromUtf8("Запис отримано та додано у Планування. Перевірте дані авто."), &dialog);
    subtitle->setObjectName(QStringLiteral("bookingSubtitle"));
    subtitle->setWordWrap(true);
    root->addWidget(title);
    root->addWidget(subtitle);

    auto* vehicleCard = new QFrame(&dialog);
    vehicleCard->setObjectName(QStringLiteral("bookingVehicleCard"));
    auto* cardLayout = new QVBoxLayout(vehicleCard);
    cardLayout->setContentsMargins(16, 14, 16, 14);
    cardLayout->setSpacing(8);

    auto addInfoLine = [&](const QString& leftText, const QString& rightText = QString()) {
        auto* row = new QHBoxLayout();
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(18);

        QLabel* left = infoValueLabel(leftText, vehicleCard);
        row->addWidget(left, rightText.trimmed().isEmpty() ? 1 : 0);

        if (!rightText.trimmed().isEmpty()) {
            QLabel* right = infoValueLabel(rightText, vehicleCard);
            right->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            row->addWidget(right, 1);
        }

        cardLayout->addLayout(row);
    };

    const QString clientLine = QString::fromUtf8("Клієнт: %1").arg(valueOrDash(booking.clientName));
    const QString phoneLine = QString::fromUtf8("Тел.: %1").arg(valueOrDash(booking.clientPhone));
    const QString yearText = valueOrDash(booking.carYear);
    const QString makeModel = carDisplay();
    const QString vehicleLine = yearText == QString::fromUtf8("—")
        ? makeModel
        : QString::fromUtf8("%1, %2 р.").arg(makeModel, yearText);
    const QString engineVolumeText = booking.engineVolumeL.trimmed().isEmpty()
        ? QString::fromUtf8("—")
        : QStringLiteral("%1 л").arg(booking.engineVolumeL.trimmed());
    const QString fuelText = valueOrDash(booking.fuelType);
    const QString engineFuelLine = QString::fromUtf8("%1, %2").arg(engineVolumeText, fuelText);
    const QString odometerLine = booking.odometerKm.trimmed().isEmpty()
        ? QString::fromUtf8("Одометр: —")
        : QString::fromUtf8("Одометр: %1 км").arg(booking.odometerKm.trimmed());
    const QString serviceLine = QString::fromUtf8("Тип запису: %1").arg(valueOrDash(booking.serviceType));

    addInfoLine(clientLine, phoneLine);
    addInfoLine(vehicleLine);
    addInfoLine(engineFuelLine);
    addInfoLine(odometerLine);
    addInfoLine(serviceLine);

    root->addWidget(vehicleCard);

    auto* buttons = new QHBoxLayout();
    buttons->setSpacing(10);
    auto* confirm = new QPushButton(QString::fromUtf8("Підтвердити"), &dialog);
    auto* openPlanning = new QPushButton(QString::fromUtf8("Відкрити в Плануванні"), &dialog);
    auto* createOrder = new QPushButton(QString::fromUtf8("Створити наряд"), &dialog);
    auto* reject = new QPushButton(QString::fromUtf8("Відхилити"), &dialog);
    buttons->addWidget(confirm);
    buttons->addWidget(openPlanning);
    buttons->addWidget(createOrder);
    buttons->addWidget(reject);
    root->addLayout(buttons);

    enum class BookingModalAction { None, Confirm, OpenPlanning, CreateOrder, Reject };
    BookingModalAction action = BookingModalAction::None;
    QObject::connect(confirm, &QPushButton::clicked, &dialog, [&]() { action = BookingModalAction::Confirm; dialog.accept(); });
    QObject::connect(openPlanning, &QPushButton::clicked, &dialog, [&]() { action = BookingModalAction::OpenPlanning; dialog.accept(); });
    QObject::connect(createOrder, &QPushButton::clicked, &dialog, [&]() { action = BookingModalAction::CreateOrder; dialog.accept(); });
    QObject::connect(reject, &QPushButton::clicked, &dialog, [&]() { action = BookingModalAction::Reject; dialog.reject(); });

    playOnlineBookingNotificationSound();
    dialog.exec();

    if (action == BookingModalAction::None) return;
    if (action == BookingModalAction::Confirm) {
        if (m_bookingSync) m_bookingSync->updateBookingStatus(booking, QStringLiteral("confirmed"));
        refreshAll();
        return;
    }
    if (action == BookingModalAction::Reject) {
        if (m_bookingSync) {
            m_bookingSync->updateBookingStatus(booking, QStringLiteral("cancelled"), QString::fromUtf8("Відхилено адміністратором"));
        }
        refreshAll();
        return;
    }
    if (action == BookingModalAction::OpenPlanning) {
        gotoPage(0);
        ensurePage(0);
        if (m_pagePlanning) m_pagePlanning->openOnlineBookingDate(booking.date);
        refreshAll();
        return;
    }
    if (action == BookingModalAction::CreateOrder) {
        if (m_bookingSync) m_bookingSync->updateBookingStatus(booking, QStringLiteral("confirmed"));
        requestOpenNewOrderFromPlanning(0, bookingPlanningText(booking), booking.date, booking.timeFrom);
        return;
    }
}

void MainWindow::showEmailEnvelopeModal(const EmailNotification& notification)
{
    if (AccessControlService::currentActorIsMaster()) return;
    if (m_emailModalActive) {
        if (m_emailSync) m_emailSync->postpone(notification.uid, 5);
        return;
    }

    m_emailModalActive = true;
    playEmailNotificationSound();
    EmailEnvelopeDialog dlg(this);
    dlg.setAccountLabel(notification.account);
    const int result = dlg.exec();
    m_emailModalActive = false;

    if (!m_emailSync) return;
    if (result == QDialog::Accepted) {
        m_emailSync->markViewed(notification.uid);
    } else {
        m_emailSync->postpone(notification.uid, 15);
    }
}


void MainWindow::checkPartRequestNotifications()
{
    if (!ActivityAuditService::currentActor().isValid()
        || !hasPermission(QStringLiteral("nav.settings")) || m_partRequestCheckInProgress) return;
    m_partRequestCheckInProgress = true;

    struct Result { QJsonObject body; QString error; };
    auto result = std::make_shared<Result>();
    QPointer<MainWindow> guard(this);
    auto* thread = QThread::create([result]() {
        result->body = CloudSyncClient::listPartRequests(&result->error);
        AppRuntimeService::closeThreadConnection();
    });
    connect(thread, &QThread::finished, this, [guard, result]() {
        if (!guard) return;
        guard->m_partRequestCheckInProgress = false;
        if (!result->error.isEmpty()) return;

        QSettings settings;
        QVector<QJsonObject> newRequests;
        const QJsonArray items = result->body.value(QStringLiteral("items")).toArray();
        for (const QJsonValue& value : items) {
            if (!value.isObject()) continue;
            const QJsonObject item = value.toObject();
            if (item.value(QStringLiteral("status")).toString().trimmed() != QStringLiteral("pending")) continue;
            const QString uid = item.value(QStringLiteral("part_request_uid")).toString().trimmed();
            if (uid.isEmpty()) continue;
            const QString key = QStringLiteral("part_requests/notified/") + uid;
            if (settings.value(key, false).toBool()) continue;
            settings.setValue(key, true);
            newRequests.push_back(item);
        }
        if (newRequests.isEmpty()) return;

        std::sort(newRequests.begin(), newRequests.end(), [](const QJsonObject& left, const QJsonObject& right) {
            return left.value(QStringLiteral("created_at")).toString() < right.value(QStringLiteral("created_at")).toString();
        });
        for (const QJsonObject& item : newRequests) guard->m_partRequestNotificationQueue.enqueue(item);
        guard->showNextPartRequestNotification();
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void MainWindow::showNextPartRequestNotification()
{
    if (m_partRequestDialogActive || m_partRequestNotificationQueue.isEmpty()) return;
    if (!ActivityAuditService::currentActor().isValid() || !hasPermission(QStringLiteral("nav.settings"))) return;

    const QJsonObject request = m_partRequestNotificationQueue.dequeue();
    m_partRequestDialogActive = true;

    PartRequestsDialog dialog(this);
    dialog.setInitialRequest(request.value(QStringLiteral("part_request_uid")).toString());
    connect(&dialog, &PartRequestsDialog::requestOrdered, this, &MainWindow::refreshAll);
    dialog.exec();

    m_partRequestDialogActive = false;
    refreshAll();
    QTimer::singleShot(0, this, &MainWindow::showNextPartRequestNotification);
}
MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(QString::fromUtf8("CarServicePointG_Sergiy&Soul"));
    setWindowIcon(QIcon(":/assets/app_icon.png"));
    setWindowFlag(Qt::WindowMinimizeButtonHint, true);
    setWindowFlag(Qt::WindowMaximizeButtonHint, true);
    setWindowFlag(Qt::WindowCloseButtonHint, true);

    buildUi();
    applyStyle();

    m_pageActivation = new PageActivationCoordinator(this);

    // banner updates are started lazily after the shell is visible,
    // so startup and the very first tab activation stay lightweight.
    m_bannerTimer.setInterval(180000);
    connect(&m_bannerTimer, &QTimer::timeout, this, &MainWindow::updateUnpaidBanner);

    auto* accessTimer = new QTimer(this);
    accessTimer->setInterval(1000);
    connect(accessTimer, &QTimer::timeout, this, [this] {
        const QString scope = AccessControlService::currentScopeKey();
        if (property("csAccessScope").toString() == scope) return;
        setProperty("csAccessScope", scope);
        refreshAccessControl();
    });
    accessTimer->start();

    m_bookingSync = new BookingSyncManager(this);
    m_bookingSync->setNotificationCallback([this](const BookingRecord& booking) {
        QPointer<MainWindow> safeThis(this);
        QTimer::singleShot(0, this, [safeThis, booking]() {
            if (!safeThis) return;
            safeThis->showOnlineBookingModal(booking);
        });
    });
    m_bookingSync->start();


    m_emailSync = new EmailSyncManager(this);
    m_emailSync->setNotificationCallback([this](const EmailNotification& notification) {
        QPointer<MainWindow> safeThis(this);
        QTimer::singleShot(0, this, [safeThis, notification]() {
            if (!safeThis) return;
            safeThis->showEmailEnvelopeModal(notification);
        });
    });
    m_emailSync->start();

    if (auto* cloudSyncWorker = AppContext::instance().cloudSyncWorker()) {
        connect(cloudSyncWorker, &CloudSyncWorker::dataReceived, this,
                [this]() {
                    refreshAll();
                    checkPartRequestNotifications();
                });
    }
    QTimer::singleShot(1800, this, &MainWindow::checkPartRequestNotifications);
    QTimer::singleShot(3000, this, [this] {
        ElitStartupSync::start(this,[this](bool,const QString& message) {
            statusBar()->showMessage(message);
            // Observations can be committed even when some cabinet PDFs are unavailable.
            refreshAll();
        },[this](const QString& message) {
            statusBar()->showMessage(message);
        });
    });
}
