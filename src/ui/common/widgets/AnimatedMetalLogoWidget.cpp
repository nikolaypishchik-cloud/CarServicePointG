#include <algorithm>
#include "AnimatedMetalLogoWidget.h"

#include <QFont>
#include <QFontMetricsF>
#include <QLinearGradient>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPixmap>

namespace {
constexpr qreal kTargetLetterPx = 38.0; // ~1cm at 96 DPI
constexpr qreal kShadowAlpha = 0.22;
}

AnimatedMetalLogoWidget::AnimatedMetalLogoWidget(QWidget* parent)
    : QWidget(parent), m_text(QStringLiteral("CarService PointG")) {
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_OpaquePaintEvent, false);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    setMinimumHeight(44);
    setMaximumHeight(48);

    m_timer.setInterval(33);
    connect(&m_timer, &QTimer::timeout, this, &AnimatedMetalLogoWidget::onTick);
    m_clock.start();
    m_timer.start();
}

QSize AnimatedMetalLogoWidget::sizeHint() const {
    QFont font(QStringLiteral("Times New Roman"));
    font.setBold(true);
    font.setPixelSize(static_cast<int>(kTargetLetterPx));
    QFontMetrics fm(font);
    return QSize(fm.horizontalAdvance(m_text) + 20, 46);
}

void AnimatedMetalLogoWidget::onTick() {
    const qint64 elapsed = m_clock.elapsed() - m_phaseStartMs;
    const int revealDuration = static_cast<int>(m_text.size()) * kRevealPerLetterMs + kRevealSettleMs;
    const int dissolveDuration = static_cast<int>(m_text.size()) * kDissolvePerLetterMs + kDissolveTailMs;

    switch (m_phase) {
    case Phase::Reveal:
        if (elapsed >= revealDuration) {
            m_phase = Phase::Hold;
            m_phaseStartMs = m_clock.elapsed();
        }
        break;
    case Phase::Hold:
        if (elapsed >= kHoldMs) {
            m_phase = Phase::PreDissolveSweep;
            m_phaseStartMs = m_clock.elapsed();
        }
        break;
    case Phase::PreDissolveSweep:
        if (elapsed >= kSweepMs) {
            m_phase = Phase::Dissolve;
            m_phaseStartMs = m_clock.elapsed();
        }
        break;
    case Phase::Dissolve:
        if (elapsed >= dissolveDuration) {
            m_phase = Phase::Reveal;
            m_phaseStartMs = m_clock.elapsed();
        }
        break;
    }
    update();
}

qreal AnimatedMetalLogoWidget::letterOpacity(int index) const {
    const qint64 elapsed = m_clock.elapsed() - m_phaseStartMs;
    if (m_phase == Phase::Reveal) {
        const qreal t = (elapsed - static_cast<qint64>(index) * kRevealPerLetterMs) / static_cast<qreal>(kRevealPerLetterMs);
        return std::clamp(t, 0.0, 1.0);
    }
    if (m_phase == Phase::Hold || m_phase == Phase::PreDissolveSweep) {
        return 1.0;
    }
    const qreal t = (elapsed - static_cast<qint64>(index) * kDissolvePerLetterMs) / static_cast<qreal>(kDissolvePerLetterMs);
    return 1.0 - std::clamp(t, 0.0, 1.0);
}

qreal AnimatedMetalLogoWidget::letterLift(int index) const {
    if (m_phase != Phase::Dissolve) {
        return 0.0;
    }
    const qint64 elapsed = m_clock.elapsed() - m_phaseStartMs;
    const qreal t = (elapsed - static_cast<qint64>(index) * kDissolvePerLetterMs) / static_cast<qreal>(kDissolvePerLetterMs);
    const qreal eased = std::clamp(t, 0.0, 1.0);
    return -5.0 * eased;
}

qreal AnimatedMetalLogoWidget::metallicSweepFactor(const QRectF& bounds) const {
    if (m_phase != Phase::PreDissolveSweep) {
        return -1.0;
    }
    Q_UNUSED(bounds);
    const qreal t = std::clamp((m_clock.elapsed() - m_phaseStartMs) / static_cast<qreal>(kSweepMs), 0.0, 1.0);
    return t;
}


void AnimatedMetalLogoWidget::rebuildCacheIfNeeded() {
    QFont font(QStringLiteral("Times New Roman"));
    font.setBold(true);
    font.setPixelSize(static_cast<int>(kTargetLetterPx));
    font.setHintingPreference(QFont::PreferFullHinting);

    if (m_cachedFont == font && m_cachedSize == size() && !m_letterPixmaps.isEmpty()) {
        return;
    }

    m_cachedFont = font;
    m_cachedSize = size();
    m_letterPixmaps.clear();
    m_letterPixmaps.reserve(m_text.size());

    QFontMetricsF fm(font);
    const qreal ascent = fm.ascent();
    for (int i = 0; i < m_text.size(); ++i) {
        const QString ch = m_text.mid(i, 1);
        const qreal advance = fm.horizontalAdvance(ch);
        const int w = qMax(8, int(std::ceil(advance + 8.0)));
        const int h = qMax(8, int(std::ceil(fm.height() + 8.0)));
        QPixmap pix(w, h);
        pix.fill(Qt::transparent);
        QPainter pp(&pix);
        pp.setRenderHint(QPainter::Antialiasing, true);
        pp.setRenderHint(QPainter::TextAntialiasing, true);
        pp.setFont(font);
        QPainterPath path;
        path.addText(QPointF(2.0, 2.0 + ascent), font, ch);
        QRectF glyphRect = path.boundingRect();
        QLinearGradient metalGrad(glyphRect.topLeft(), glyphRect.bottomLeft());
        metalGrad.setColorAt(0.0, QColor(252, 252, 255));
        metalGrad.setColorAt(0.18, QColor(214, 220, 228));
        metalGrad.setColorAt(0.42, QColor(132, 144, 156));
        metalGrad.setColorAt(0.58, QColor(230, 234, 239));
        metalGrad.setColorAt(0.82, QColor(120, 132, 145));
        metalGrad.setColorAt(1.0, QColor(236, 240, 245));
        pp.save();
        pp.translate(0.8, 1.0);
        pp.fillPath(path, QColor(10, 18, 28, 56));
        pp.restore();
        pp.fillPath(path, metalGrad);
        QPen outline(QColor(245, 248, 252, 110));
        outline.setWidthF(0.7);
        pp.setPen(outline);
        pp.drawPath(path);
        QLinearGradient gloss(glyphRect.left(), glyphRect.top(), glyphRect.left(), glyphRect.bottom());
        gloss.setColorAt(0.0, QColor(255, 255, 255, 120));
        gloss.setColorAt(0.35, QColor(255, 255, 255, 18));
        gloss.setColorAt(1.0, QColor(255, 255, 255, 0));
        pp.fillPath(path, gloss);
        pp.end();
        m_letterPixmaps.push_back(pix);
    }
}

void AnimatedMetalLogoWidget::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    rebuildCacheIfNeeded();

    QFont font(QStringLiteral("Times New Roman"));
    font.setBold(true);
    font.setPixelSize(static_cast<int>(kTargetLetterPx));
    font.setHintingPreference(QFont::PreferFullHinting);
    p.setFont(font);

    QFontMetricsF fm(font);
    const qreal textWidth = fm.horizontalAdvance(m_text);
    const qreal ascent = fm.ascent();
    const qreal baselineY = (height() + ascent - fm.descent()) * 0.5;
    qreal x = (width() - textWidth) * 0.5;

    QRectF totalBounds(x, baselineY - ascent, textWidth, fm.height());
    const qreal sweepT = metallicSweepFactor(totalBounds);
    const qreal sweepCenterX = totalBounds.left() + sweepT * totalBounds.width();

    for (int i = 0; i < m_text.size(); ++i) {
        const QString ch = m_text.mid(i, 1);
        const qreal advance = fm.horizontalAdvance(ch);
        const qreal opacity = letterOpacity(i);
        if (opacity <= 0.001) {
            x += advance;
            continue;
        }

        const QRectF glyphRect(x, baselineY - ascent + letterLift(i), advance, fm.height());
        const QPixmap& letter = (i < m_letterPixmaps.size()) ? m_letterPixmaps[i] : QPixmap();
        if (!letter.isNull()) {
            p.save();
            p.setOpacity(opacity);
            p.drawPixmap(QPointF(x - 2.0, baselineY - ascent + letterLift(i) - 2.0), letter);
            p.restore();
        }

        if (sweepT >= 0.0) {
            const qreal dist = std::abs(glyphRect.center().x() - sweepCenterX);
            const qreal radius = std::max<qreal>(36.0, totalBounds.width() * 0.10);
            const qreal pulse = std::clamp(1.0 - dist / radius, 0.0, 1.0);
            if (pulse > 0.001) {
                p.save();
                p.setOpacity(opacity);
                QLinearGradient sweepGrad(glyphRect.left(), glyphRect.top(), glyphRect.right(), glyphRect.top());
                sweepGrad.setColorAt(0.0, QColor(255, 255, 255, 0));
                sweepGrad.setColorAt(0.5, QColor(255, 255, 255, static_cast<int>(90 * pulse * opacity)));
                sweepGrad.setColorAt(1.0, QColor(255, 255, 255, 0));
                p.fillRect(glyphRect.adjusted(-8, -4, 8, 4), sweepGrad);
                p.restore();
            }
        }

        x += advance;
    }
}
