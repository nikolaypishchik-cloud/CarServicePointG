#include "ui/common/widgets/GlossyButton.h"

#include <QLinearGradient>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QStyleOptionButton>
#include <QStylePainter>

GlossyButton::GlossyButton(const QString& text, QWidget* parent)
    : QPushButton(text, parent),
      m_textColor(245,245,245),
      m_outlineColor(25,25,25)
{
    setCursor(Qt::PointingHandCursor);
    setMinimumHeight(36);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    setFocusPolicy(Qt::StrongFocus);
}

void GlossyButton::setVisualStyle(VisualStyle style)
{
    if (m_visualStyle == style) return;
    m_visualStyle = style;
    update();
}

void GlossyButton::paintEvent(QPaintEvent* e)
{
    Q_UNUSED(e);

    QStyleOptionButton opt;
    initStyleOption(&opt);
    const QString text = opt.text;
    opt.text.clear();

    QStylePainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRectF r = rect().adjusted(1, 1, -1, -1);
    const qreal radius = qMin<qreal>(18.0, qMax<qreal>(12.0, r.height() * 0.42));
    const bool hovered = underMouse() || (opt.state & QStyle::State_MouseOver);
    const bool pressed = isDown() || (opt.state & QStyle::State_Sunken);
    const bool enabled = isEnabled();

    if (m_visualStyle == VisualStyle::CatalogMetal) {
        QColor upper0(214, 222, 233);
        QColor upper1(166, 179, 196);
        QColor upper2(131, 146, 165);
        QColor lower0(8, 23, 48);
        QColor lower1(2, 12, 29);
        QColor lower2(1, 6, 17);
        QColor borderOuter(235, 241, 248, 225);
        QColor borderInner(52, 67, 88, 180);
        QColor textColor(252, 252, 252);
        QColor outlineColor(24, 37, 54, 220);

        if (!enabled) {
            upper0 = QColor(169, 176, 184);
            upper1 = QColor(139, 147, 158);
            upper2 = QColor(113, 121, 132);
            lower0 = QColor(46, 57, 74);
            lower1 = QColor(34, 44, 59);
            lower2 = QColor(25, 33, 44);
            borderOuter = QColor(208, 214, 222, 170);
            borderInner = QColor(52, 60, 74, 140);
            textColor = QColor(239, 239, 239, 210);
            outlineColor = QColor(35, 42, 54, 170);
        } else if (pressed) {
            upper0 = QColor(179, 191, 205);
            upper1 = QColor(136, 149, 166);
            upper2 = QColor(106, 121, 141);
            lower0 = QColor(4, 16, 38);
            lower1 = QColor(1, 9, 24);
            lower2 = QColor(0, 4, 12);
            borderOuter = QColor(227, 234, 242, 210);
            borderInner = QColor(27, 42, 62, 200);
        } else if (hovered) {
            upper0 = QColor(228, 236, 246);
            upper1 = QColor(184, 196, 212);
            upper2 = QColor(145, 160, 178);
            lower0 = QColor(14, 31, 60);
            lower1 = QColor(4, 16, 36);
            lower2 = QColor(1, 8, 20);
            borderOuter = QColor(245, 249, 252, 235);
            borderInner = QColor(66, 82, 104, 190);
        }

        const QRectF bodyRect = r;
        const QRectF topRect(bodyRect.left(), bodyRect.top(), bodyRect.width(), bodyRect.height() * 0.56);
        const QRectF bottomRect(bodyRect.left(), bodyRect.top() + bodyRect.height() * 0.50, bodyRect.width(), bodyRect.height() * 0.50);

        QPainterPath outerPath;
        outerPath.addRoundedRect(bodyRect, radius, radius);
        p.setClipPath(outerPath);

        QLinearGradient upperGrad(topRect.topLeft(), topRect.bottomLeft());
        upperGrad.setColorAt(0.0, upper0);
        upperGrad.setColorAt(0.42, upper1);
        upperGrad.setColorAt(1.0, upper2);
        p.fillRect(topRect, upperGrad);

        QLinearGradient lowerGrad(bottomRect.topLeft(), bottomRect.bottomLeft());
        lowerGrad.setColorAt(0.0, lower0);
        lowerGrad.setColorAt(0.55, lower1);
        lowerGrad.setColorAt(1.0, lower2);
        p.fillRect(bottomRect, lowerGrad);

        const QRectF glossRect(bodyRect.left() + 2.0, bodyRect.top() + 2.0, bodyRect.width() - 4.0, bodyRect.height() * 0.38);
        QPainterPath glossPath;
        glossPath.addRoundedRect(glossRect, qMax<qreal>(7.0, radius - 3.0), qMax<qreal>(7.0, radius - 3.0));
        QLinearGradient glossGrad(glossRect.topLeft(), glossRect.bottomLeft());
        glossGrad.setColorAt(0.0, QColor(255, 255, 255, enabled ? 125 : 70));
        glossGrad.setColorAt(0.55, QColor(255, 255, 255, enabled ? 42 : 22));
        glossGrad.setColorAt(1.0, QColor(255, 255, 255, 0));
        p.fillPath(glossPath, glossGrad);

        const qreal dividerY = bodyRect.top() + bodyRect.height() * 0.50;
        p.setPen(QPen(QColor(232, 237, 244, enabled ? 95 : 55), 1.0));
        p.drawLine(QPointF(bodyRect.left() + 2.0, dividerY), QPointF(bodyRect.right() - 2.0, dividerY));
        p.setPen(QPen(QColor(39, 52, 72, enabled ? 95 : 55), 1.0));
        p.drawLine(QPointF(bodyRect.left() + 2.0, dividerY + 1.0), QPointF(bodyRect.right() - 2.0, dividerY + 1.0));

        p.setClipping(false);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(borderOuter, 1.15));
        p.drawRoundedRect(bodyRect, radius, radius);
        p.setPen(QPen(borderInner, 1.0));
        p.drawRoundedRect(bodyRect.adjusted(1.0, 1.0, -1.0, -1.0), qMax<qreal>(7.0, radius - 1.5), qMax<qreal>(7.0, radius - 1.5));

        const QRectF innerShadowRect = bodyRect.adjusted(1.5, 1.5, -1.5, -1.5);
        QLinearGradient innerShadow(innerShadowRect.topLeft(), innerShadowRect.bottomLeft());
        innerShadow.setColorAt(0.0, QColor(255, 255, 255, enabled ? 18 : 8));
        innerShadow.setColorAt(1.0, QColor(0, 0, 0, enabled ? 22 : 10));
        p.setPen(QPen(QBrush(innerShadow), 1.0));
        p.drawRoundedRect(innerShadowRect, qMax<qreal>(6.5, radius - 2.0), qMax<qreal>(6.5, radius - 2.0));

        if (!text.isEmpty()) {
            QFont f = font();
            f.setBold(true);
            p.setFont(f);

            QRectF textRect = bodyRect.adjusted(16.0, 0.0, -16.0, 0.0);
            if (pressed) textRect.translate(0.0, 1.0);

            QPainterPath path;
            QFontMetrics fm(f);
            const int w = fm.horizontalAdvance(text);
            const qreal x = textRect.center().x() - (w / 2.0);
            const qreal y = textRect.center().y() + (fm.ascent() - fm.descent()) / 2.0;
            path.addText(QPointF(x, y), f, text);
            p.setPen(QPen(outlineColor, 2.15, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.setBrush(Qt::NoBrush);
            p.drawPath(path);
            p.setPen(Qt::NoPen);
            p.setBrush(textColor);
            p.drawPath(path);
        }
        return;
    }

    // Default dark glossy button
    QLinearGradient g(r.topLeft(), r.bottomLeft());
    if (pressed) {
        g.setColorAt(0.0, QColor(55,55,55));
        g.setColorAt(1.0, QColor(25,25,25));
    } else if (hovered) {
        g.setColorAt(0.0, QColor(95,95,95));
        g.setColorAt(1.0, QColor(45,45,45));
    } else {
        g.setColorAt(0.0, QColor(75,75,75));
        g.setColorAt(1.0, QColor(35,35,35));
    }

    p.setPen(Qt::NoPen);
    p.setBrush(g);
    p.drawRoundedRect(r, 10, 10);

    QLinearGradient hi(r.topLeft(), QPointF(r.left(), r.center().y()));
    hi.setColorAt(0.0, QColor(255,255,255,70));
    hi.setColorAt(1.0, QColor(255,255,255,0));
    p.setBrush(hi);
    p.drawRoundedRect(r.adjusted(1,1,-1,-1), 9, 9);

    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(0,0,0,120), 1));
    p.drawRoundedRect(r, 10, 10);

    if (!text.isEmpty()) {
        QFont f = font();
        f.setBold(true);
        p.setFont(f);

        QPainterPath path;
        QRectF tr = r.adjusted(10, 0, -10, 0);
        QFontMetrics fm(f);
        const int w = fm.horizontalAdvance(text);
        const qreal x = tr.center().x() - (w / 2.0);
        const qreal y = tr.center().y() + (fm.ascent() - fm.descent()) / 2.0;

        path.addText(QPointF(x, y), f, text);
        p.setPen(QPen(m_outlineColor, 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
        p.setPen(Qt::NoPen);
        p.setBrush(m_textColor);
        p.drawPath(path);
    }
}
