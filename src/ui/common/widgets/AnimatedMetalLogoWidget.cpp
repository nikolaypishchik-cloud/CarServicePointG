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

    m_timer.setInterval(kHoldMs);
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
    if (!m_sweeping) {
        m_sweeping = true;
        m_clock.restart();
        m_timer.setInterval(33);
    } else if (m_clock.elapsed() >= kSweepMs) {
        m_sweeping = false;
        m_timer.setInterval(kHoldMs);
    }
    update();
}

qreal AnimatedMetalLogoWidget::metallicSweepFactor() const {
    return m_sweeping ? std::clamp(m_clock.elapsed() / qreal(kSweepMs), 0.0, 1.0) : -1.0;
}

void AnimatedMetalLogoWidget::rebuildCacheIfNeeded() {
    QFont font(QStringLiteral("Times New Roman"));
    font.setBold(true);
    font.setPixelSize(static_cast<int>(kTargetLetterPx));
    font.setHintingPreference(QFont::PreferFullHinting);
    const qreal naturalWidth = QFontMetricsF(font).horizontalAdvance(m_text);
    if (naturalWidth > width() - 12)
        font.setPixelSize(qMax(1, qFloor(kTargetLetterPx * (width() - 12) / naturalWidth)));

    const qreal ratio = devicePixelRatioF();
    if (m_cachedFont == font && m_cachedSize == size() && !m_letterPixmaps.isEmpty()
        && qFuzzyCompare(m_letterPixmaps.first().devicePixelRatio(), ratio)) {
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
        QPixmap pix(qCeil(w * ratio), qCeil(h * ratio));
        pix.setDevicePixelRatio(ratio);
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
    font = m_cachedFont;
    p.setFont(font);

    QFontMetricsF fm(font);
    const qreal textWidth = fm.horizontalAdvance(m_text);
    const qreal ascent = fm.ascent();
    const qreal baselineY = (height() + ascent - fm.descent()) * 0.5;
    qreal x = width() - textWidth - 1.0;

    QRectF totalBounds(x, baselineY - ascent, textWidth, fm.height());
    const qreal sweepT = metallicSweepFactor();
    const qreal sweepCenterX = totalBounds.left() + sweepT * totalBounds.width();

    for (int i = 0; i < m_text.size(); ++i) {
        const QString ch = m_text.mid(i, 1);
        const qreal advance = fm.horizontalAdvance(ch);
        const QRectF glyphRect(x, baselineY - ascent, advance, fm.height());
        const QPixmap& letter = (i < m_letterPixmaps.size()) ? m_letterPixmaps[i] : QPixmap();
        if (!letter.isNull()) {
            p.save();
            p.drawPixmap(QPointF(x - 2.0, baselineY - ascent - 2.0), letter);
            p.restore();
        }

        if (sweepT >= 0.0) {
            const qreal dist = std::abs(glyphRect.center().x() - sweepCenterX);
            const qreal radius = std::max<qreal>(36.0, totalBounds.width() * 0.10);
            const qreal pulse = std::clamp(1.0 - dist / radius, 0.0, 1.0);
            if (pulse > 0.001) {
                p.save();
                QLinearGradient sweepGrad(glyphRect.left(), glyphRect.top(), glyphRect.right(), glyphRect.top());
                sweepGrad.setColorAt(0.0, QColor(255, 255, 255, 0));
                sweepGrad.setColorAt(0.5, QColor(255, 255, 255, static_cast<int>(90 * pulse)));
                sweepGrad.setColorAt(1.0, QColor(255, 255, 255, 0));
                p.fillRect(glyphRect.adjusted(-8, -4, 8, 4), sweepGrad);
                p.restore();
            }
        }

        x += advance;
    }
}
