#include "ui/splash/SplashScreen.h"

#include <QPointer>
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDebug>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <QScreen>
#include <QtMath>

#include <utility>

static inline double clamp01(double v) { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); }

static void drawPathContourProgress(QPainter& p, const QPainterPath& path, const QPen& pen, double progress)
{
    progress = clamp01(progress);
    if (progress <= 0.0) return;

    const QList<QPolygonF> polys = path.toSubpathPolygons();
    if (polys.isEmpty()) return;

    double totalLen = 0.0;
    for (const QPolygonF& poly : polys) {
        for (int i = 1; i < poly.size(); ++i) {
            totalLen += QLineF(poly.at(i - 1), poly.at(i)).length();
        }
    }
    if (totalLen <= 0.0) return;

    double remain = totalLen * progress;
    p.save();
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    for (const QPolygonF& poly : polys) {
        if (remain <= 0.0) break;
        if (poly.size() < 2) continue;

        QPainterPath partial;
        partial.moveTo(poly.first());

        for (int i = 1; i < poly.size(); ++i) {
            const QPointF a = poly.at(i - 1);
            const QPointF b = poly.at(i);
            const double segLen = QLineF(a, b).length();
            if (segLen <= 0.0) continue;

            if (remain >= segLen) {
                partial.lineTo(b);
                remain -= segLen;
            } else {
                const double k = remain / segLen;
                const QPointF mid(a.x() + (b.x() - a.x()) * k,
                                  a.y() + (b.y() - a.y()) * k);
                partial.lineTo(mid);
                remain = 0.0;
                break;
            }
        }

        p.drawPath(partial);
    }

    p.restore();
}

SplashScreen::SplashScreen(QWidget* parent)
    : QWidget(parent)
{
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_ShowWithoutActivating, true);
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setFocusPolicy(Qt::NoFocus);

    m_tick.setInterval(16); // target ~60 FPS while the UI thread is free
    connect(&m_tick, &QTimer::timeout, this, [this]() {
        const int wallT = m_elapsed.isValid() ? int(m_elapsed.elapsed()) : 0;
        const int rawDelta = qMax(0, wallT - m_lastWallElapsedMs);
        m_lastWallElapsedMs = wallT;

        // Do not let long UI-thread stalls fast-forward the splash animation.
        // When startup blocks the event loop for a while, the ring/text should
        // continue from the last visible state instead of jumping to a much
        // later frame or a fully rendered title.
        constexpr int kMaxAnimStepMs = 34;
        const int animationLimit = 1000 + 85 * qMax(1, m_title.size());
        m_animElapsedMs = qMin(animationLimit, m_animElapsedMs + qMin(rawDelta, kMaxAnimStepMs));

        // Allow the final painted letter to reach the screen, then hand off
        // without waiting for the remaining ring/outline animation.
        if (m_startupReady && m_titleCompleteWallMs >= 0 && wallT - m_titleCompleteWallMs >= 100) {
            m_tick.stop();
            qInfo().noquote() << QStringLiteral("SPLASH [finished] wall=%1ms anim=%2ms")
                                      .arg(QString::number(wallT), QString::number(m_animElapsedMs));
            // Hide the splash first and only then notify main.cpp to show the
            // main window. Emitting the signal before hide() causes the main
            // window to appear underneath the still-visible splash for one more
            // event-loop turn, which looks like the app opens before the splash
            // has actually ended.
            this->hide();
            QPointer<SplashScreen> self(this);
            QTimer::singleShot(0, this, [self]() { if (self) emit self->finished(); });
            return;
        }
        update();
    });
}

void SplashScreen::buildFrameManifest()
{
    if (!m_framePaths.isEmpty()) {
        return;
    }

    QVector<QString> embedded;
    embedded.reserve(162);
    for (int i = 1; i <= 162; ++i) {
        const QString path = QStringLiteral(":/assets/splash_ring_frames/frame_%1.png")
                                 .arg(i, 4, 10, QLatin1Char('0'));
        if (!QFile::exists(path)) {
            embedded.clear();
            break;
        }
        embedded.push_back(path);
    }
    if (!embedded.isEmpty()) {
        m_framePaths = std::move(embedded);
        return;
    }

    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidateDirs = {
        QDir(appDir).filePath(QStringLiteral("../resources/assets/splash_ring_frames")),
        QDir(appDir).filePath(QStringLiteral("resources/assets/splash_ring_frames")),
        QDir(appDir).filePath(QStringLiteral("assets/splash_ring_frames")),
        QStringLiteral("resources/assets/splash_ring_frames")
    };

    for (const QString& candidate : candidateDirs) {
        QDir dir(QDir::cleanPath(candidate));
        if (!dir.exists()) continue;

        const QStringList names = dir.entryList({QStringLiteral("frame_*.png")}, QDir::Files, QDir::Name);
        if (names.isEmpty()) continue;

        QVector<QString> loaded;
        loaded.reserve(names.size());
        for (const QString& name : names) {
            loaded.push_back(dir.filePath(name));
        }

        if (!loaded.isEmpty()) {
            m_framePaths = std::move(loaded);
            return;
        }
    }
}

QPixmap SplashScreen::sourceFrameForIndex(int idx) const
{
    if (m_framePaths.isEmpty()) {
        return QPixmap();
    }

    const int sourceIdx = qBound(0, idx, m_framePaths.size() - 1);

    auto it = m_sourceFrameCache.constFind(sourceIdx);
    if (it != m_sourceFrameCache.constEnd()) {
        return it.value();
    }

    QPixmap px(m_framePaths[sourceIdx]);
    if (!px.isNull()) {
        m_sourceFrameCache.insert(sourceIdx, px);
    }
    return px;
}

QPixmap SplashScreen::scaledFrameForIndex(int idx, int targetSize) const
{
    if (targetSize <= 0) {
        return QPixmap();
    }

    const QPixmap source = sourceFrameForIndex(idx);
    if (source.isNull()) {
        return QPixmap();
    }

    const QString key = QStringLiteral("%1:%2x%2").arg(idx).arg(targetSize);
    auto it = m_scaledFrameCache.constFind(key);
    if (it != m_scaledFrameCache.constEnd()) {
        return it.value();
    }

    const QPixmap scaled = source.scaled(targetSize, targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    if (!scaled.isNull()) {
        m_scaledFrameCache.insert(key, scaled);
    }
    return scaled;
}

void SplashScreen::start(int durationMs)
{
    m_durationMs = qMax(1000, durationMs);
    m_title = QStringLiteral("CarService PointG");
    m_lastWallElapsedMs = 0;
    // The first synchronous repaint must contain a visible ring, not an
    // entirely transparent frame followed by a potentially long DB startup.
    m_animElapsedMs = 180;
    m_startupReady = false;
    m_titleCompleteWallMs = -1;
    m_loggedFirstPaint = false;
    m_loggedTextStart = false;
    m_loggedTitleComplete = false;

    buildFrameManifest();

    if (QScreen* sc = QGuiApplication::primaryScreen()) {
        setGeometry(sc->geometry());
    }

    qInfo().noquote() << QStringLiteral("SPLASH [start] duration=%1ms frames=%2")
                             .arg(QString::number(m_durationMs), QString::number(m_framePaths.size()));
    m_elapsed.restart();
    show();
    raise();
    m_tick.start();
}

void SplashScreen::setStartupReady()
{
    m_startupReady = true;
    qInfo() << "SPLASH [startup_ready]";
}

void SplashScreen::paintEvent(QPaintEvent* e)
{
    Q_UNUSED(e);

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);

    const int wallT = m_elapsed.isValid() ? int(m_elapsed.elapsed()) : 0;
    const int t = m_animElapsedMs;

    if (!m_loggedFirstPaint) {
        m_loggedFirstPaint = true;
        qInfo().noquote() << QStringLiteral("SPLASH [first_visible_frame] wall=%1ms anim=%2ms")
                                  .arg(QString::number(wallT), QString::number(t));
    }

    // Required timing:
    // - smoke video starts immediately;
    // - title starts ~1s later;
    // - handoff follows the final painted letter once startup is ready.
    const int fadeIn = 260;
    const int videoStart = 0;
    const int videoFadeIn = 180;
    const int textStart = 1000;
    const int msPerChar = 85;

    const int titleLen = m_title.size();
    const int textDone = textStart + msPerChar * qMax(1, titleLen);

    const int outlineStart = textDone + 120;
    const int outlinePerGlyph = 105;
    const int outlineSweep = 160;

    double alpha = 1.0;
    if (t < fadeIn) alpha = clamp01(double(t) / double(fadeIn));

    const double scale = qMax(0.10, qMin(width() / 1920.0, height() / 1080.0));
    const QPointF center = rect().center();

    QFont f(QStringLiteral("Times New Roman"));
    f.setBold(true);
    f.setItalic(true);
    f.setPointSizeF(72.0 * scale);

    const QPointF targetCenter(center.x(), center.y() - 10.0 * scale);

    QPainterPath fullTitlePathRaw;
    fullTitlePathRaw.addText(0, 0, f, m_title);
    const QRectF fullRawBounds = fullTitlePathRaw.boundingRect();
    const double baseX = targetCenter.x() - fullRawBounds.center().x();
    const double baselineY = targetCenter.y() - fullRawBounds.center().y();
    const QRectF fullTitleBounds = fullRawBounds.translated(baseX, baselineY);

    // -------- Ring / smoke video (behind the text) --------
    if (!m_framePaths.isEmpty() && t >= videoStart) {
        const double ringA = clamp01(double(t - videoStart) / double(videoFadeIn));
        const double videoProgress = clamp01(double(t - videoStart) / double(qMax(1, m_durationMs - 1)));
        const int idx = qBound(0, int(videoProgress * double(m_framePaths.size() - 1)), m_framePaths.size() - 1);

        const int startIdx = m_title.indexOf(QChar('r'));
        const int endIdx = m_title.lastIndexOf(QChar('n'));

        QFontMetricsF fm(f);
        double x = baseX;
        QRectF startRect;
        QRectF endRect;

        for (int i = 0; i < titleLen; ++i) {
            const QString ch = m_title.mid(i, 1);
            QPainterPath glyphPath;
            glyphPath.addText(QPointF(x, baselineY), f, ch);
            const QRectF gb = glyphPath.boundingRect();
            if (i == startIdx) startRect = gb;
            if (i == endIdx) endRect = gb;
            x += fm.horizontalAdvance(ch);
        }

        QPointF ringCenter = fullTitleBounds.center();
        double ringDiameter = 520.0 * scale;
        if (!startRect.isNull() && !endRect.isNull() && endRect.right() > startRect.left()) {
            ringCenter = QPointF((startRect.left() + endRect.right()) * 0.5,
                                 (startRect.center().y() + endRect.center().y()) * 0.5);
            ringDiameter = endRect.right() - startRect.left();
        }

        ringDiameter = qMax(140.0 * scale, ringDiameter);

        const int ringSize = qMax(1, int(qRound(ringDiameter)));
        const QPixmap scaled = scaledFrameForIndex(idx, ringSize);
        const QPoint topLeft(int(ringCenter.x() - scaled.width() / 2.0),
                             int(ringCenter.y() - scaled.height() / 2.0));

        p.save();
        p.setOpacity(alpha * ringA);
        p.drawPixmap(topLeft, scaled);
        p.restore();
    }

    // -------- Title --------
    int chars = 0;
    if (t >= textStart) {
        chars = int((t - textStart) / msPerChar);
        chars = qBound(0, chars, titleLen);
    }

    if (!m_loggedTextStart && chars > 0) {
        m_loggedTextStart = true;
        qInfo().noquote() << QStringLiteral("SPLASH [text_start] wall=%1ms anim=%2ms chars=%3")
                                  .arg(QString::number(wallT), QString::number(t), QString::number(chars));
    }
    if (!m_loggedTitleComplete && chars == titleLen) {
        m_loggedTitleComplete = true;
        m_titleCompleteWallMs = wallT;
        qInfo().noquote() << QStringLiteral("SPLASH [title_complete] wall=%1ms anim=%2ms")
                                  .arg(QString::number(wallT), QString::number(t));
    }

    if (chars > 0) {
        const QString shown = m_title.left(chars);
        QPainterPath textPath;
        textPath.addText(QPointF(baseX, baselineY), f, shown);
        const QRectF tb = textPath.boundingRect();

        p.save();
        p.setOpacity(alpha);

        QPainterPath shadow = textPath.translated(3.0 * scale, 3.0 * scale);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 160));
        p.drawPath(shadow);

        QPen outer(QColor(120, 20, 18, 220), 6.5 * scale, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        p.setPen(outer);
        p.setBrush(Qt::NoBrush);
        p.drawPath(textPath);

        QLinearGradient gold(tb.topLeft(), tb.bottomLeft());
        gold.setColorAt(0.00, QColor(255, 245, 205));
        gold.setColorAt(0.35, QColor(245, 195, 90));
        gold.setColorAt(0.70, QColor(190, 120, 35));
        gold.setColorAt(1.00, QColor(120, 70, 20));
        p.setPen(Qt::NoPen);
        p.setBrush(gold);
        p.drawPath(textPath);

        QLinearGradient hl(tb.topLeft(), tb.topRight());
        hl.setColorAt(0.0, QColor(255, 255, 255, 120));
        hl.setColorAt(1.0, QColor(255, 255, 255, 0));
        p.setOpacity(alpha * 0.45);
        p.setBrush(hl);
        p.drawPath(textPath);

        if (chars == titleLen && t >= outlineStart) {
            const double outlineWidth = 0.67;
            QPen blackOutline(QColor(8, 8, 8, 235), outlineWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
            blackOutline.setCosmetic(true);
            QFontMetricsF fm(f);
            double x = baseX;

            for (int i = 0; i < titleLen; ++i) {
                const QString ch = m_title.mid(i, 1);
                const qint64 glyphStart = outlineStart + i * outlinePerGlyph;
                const double glyphProgress = clamp01(double(t - glyphStart) / double(outlineSweep));
                const double adv = fm.horizontalAdvance(ch);
                if (glyphProgress <= 0.0) {
                    x += adv;
                    continue;
                }

                QPainterPath glyphPath;
                glyphPath.addText(QPointF(x, baselineY), f, ch);

                p.save();
                p.setOpacity(alpha);
                drawPathContourProgress(p, glyphPath, blackOutline, glyphProgress);
                p.restore();

                x += adv;
            }
        }

        p.restore();
    }
}
