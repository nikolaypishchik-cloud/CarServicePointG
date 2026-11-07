#include "core/services/ReportsPdfPaintingSupport.h"

#include <QFontMetrics>
#include <QFontMetricsF>
#include <QPainter>
#include <QPen>
#include <QRegularExpression>

namespace {

int mmToPx(const QPaintDevice* dev, double mm)
{
    return qRound(mm * dev->logicalDpiX() / 25.4);
}

} // namespace

namespace ReportsPdfPaintingSupport {

qreal ptToPx(const QPaintDevice* dev, double pt)
{
    return pt * dev->logicalDpiX() / 72.0;
}

void drawIssueTextWithUnderline(QPainter& p, const QRect& rect, const QString& text, const QFont& font, qreal underlineWidth)
{
    p.save();
    p.setPen(Qt::black);

    const QString singleLineText = text.simplified();
    if (singleLineText.isEmpty()) {
        p.restore();
        return;
    }

    QFont fittedFont(font);
    const qreal basePt = font.pointSizeF() > 0.0 ? font.pointSizeF() : qreal(font.pointSize());
    const qreal minPt = qMax<qreal>(7.5, basePt > 0.0 ? basePt * 0.78 : 7.5);
    const qreal maxTextWidth = qMax<qreal>(1.0, rect.width() - ptToPx(p.device(), 1.0));

    qreal chosenPt = qMax<qreal>(basePt, minPt);
    for (qreal candidate = qMax<qreal>(basePt, minPt); candidate >= minPt; candidate -= 0.1) {
        fittedFont.setPointSizeF(candidate);
        const QFontMetricsF candidateFm(fittedFont, p.device());
        chosenPt = candidate;
        if (candidateFm.horizontalAdvance(singleLineText) <= maxTextWidth) {
            break;
        }
    }
    fittedFont.setPointSizeF(chosenPt);
    p.setFont(fittedFont);

    const QFontMetricsF fm(fittedFont, p.device());
    const qreal naturalWidth = fm.horizontalAdvance(singleLineText);
    const qreal horizontalScale = (naturalWidth > maxTextWidth && naturalWidth > 0.0)
        ? qMax<qreal>(0.82, maxTextWidth / naturalWidth)
        : 1.0;

    const qreal textHeight = fm.height();
    const qreal baselineY = rect.top() + qMax<qreal>(0.0, (rect.height() - textHeight) / 2.0) + fm.ascent();
    const qreal textTop = baselineY - fm.ascent();
    const qreal textBottom = textTop + textHeight;

    if (horizontalScale < 0.999) {
        p.save();
        p.translate(rect.left(), 0.0);
        p.scale(horizontalScale, 1.0);
        p.drawText(QPointF(0.0, baselineY), singleLineText);
        p.restore();
    } else {
        p.drawText(QPointF(rect.left(), baselineY), singleLineText);
    }

    const qreal drawnWidth = naturalWidth * horizontalScale;
    const qreal underlineStartX = rect.left();
    const qreal underlineEndX = qMin<qreal>(rect.left() + drawnWidth, rect.left() + maxTextWidth);
    const qreal lineGap = mmToPx(p.device(), 1.0);
    const qreal lineY = qMin<qreal>(rect.bottom() - 1.0, textBottom + lineGap);
    p.setPen(QPen(Qt::black, underlineWidth));
    p.drawLine(QPointF(underlineStartX, lineY), QPointF(underlineEndX, lineY));

    p.restore();
}

void drawSingleLineFittedCell(QPainter& p, const QRect& rect, const QString& text, const QFont& font,
                              Qt::Alignment align, const QColor& fill, qreal penWidth,
                              qreal minPointSize, qreal minHorizontalScale)
{
    p.save();
    p.setPen(QPen(Qt::black, penWidth));
    p.fillRect(rect, fill);
    p.drawRect(rect);

    const QRect inner = rect.adjusted(5, 4, -5, -4);
    const QString singleLineText = text.simplified();
    if (singleLineText.isEmpty()) {
        p.restore();
        return;
    }

    QFont fittedFont(font);
    const qreal basePt = font.pointSizeF() > 0.0 ? font.pointSizeF() : qreal(font.pointSize());
    qreal chosenPt = qMax(minPointSize, basePt);
    const qreal maxWidth = qMax<qreal>(1.0, inner.width());
    for (qreal candidate = qMax(minPointSize, basePt); candidate >= minPointSize; candidate -= 0.1) {
        fittedFont.setPointSizeF(candidate);
        const QFontMetricsF candidateFm(fittedFont, p.device());
        chosenPt = candidate;
        if (candidateFm.horizontalAdvance(singleLineText) <= maxWidth) {
            break;
        }
    }
    fittedFont.setPointSizeF(chosenPt);
    const QFontMetricsF fm(fittedFont, p.device());
    const qreal naturalWidth = fm.horizontalAdvance(singleLineText);
    const qreal horizontalScale = (naturalWidth > maxWidth && naturalWidth > 0.0)
        ? qMax(minHorizontalScale, maxWidth / naturalWidth)
        : 1.0;
    const qreal drawnWidth = naturalWidth * horizontalScale;

    const bool alignRight = align.testFlag(Qt::AlignRight);
    const bool alignHCenter = align.testFlag(Qt::AlignHCenter) || align.testFlag(Qt::AlignCenter);
    qreal drawX = inner.left();
    if (alignRight) {
        drawX = inner.right() - drawnWidth;
    } else if (alignHCenter) {
        drawX = inner.left() + (inner.width() - drawnWidth) / 2.0;
    }
    drawX = qMax<qreal>(inner.left(), drawX);

    const qreal baselineY = inner.top() + qMax<qreal>(0.0, (inner.height() - fm.height()) / 2.0) + fm.ascent();
    p.setFont(fittedFont);

    if (horizontalScale < 0.999) {
        p.save();
        p.translate(drawX, 0.0);
        p.scale(horizontalScale, 1.0);
        p.drawText(QPointF(0.0, baselineY), singleLineText);
        p.restore();
    } else {
        p.drawText(QPointF(drawX, baselineY), singleLineText);
    }

    p.restore();
}

void drawCenteredText(QPainter& p, const QRect& r, const QString& txt, const QFont& f)
{
    p.setFont(f);
    p.drawText(r, Qt::AlignCenter | Qt::TextWordWrap, txt);
}

void drawLeftText(QPainter& p, const QRect& r, const QString& txt, const QFont& f)
{
    p.setFont(f);
    p.drawText(r, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap, txt);
}

void drawRightText(QPainter& p, const QRect& r, const QString& txt, const QFont& f)
{
    p.setFont(f);
    p.drawText(r, Qt::AlignRight | Qt::AlignVCenter | Qt::TextWordWrap, txt);
}

namespace {

QString preparePdfCellTextForWrap(QString text)
{
    text.replace(QChar(0x00A0), QLatin1Char(' '));
    text.replace(QChar(0x202F), QLatin1Char(' '));
    text.replace(QRegularExpression(QStringLiteral(R"([ \t\r]+)")), QStringLiteral(" "));
    text.replace(QRegularExpression(QStringLiteral(R"(\s*([+,;/])\s*)")), QStringLiteral(" \\1 "));
    QStringList lines;
    for (QString line : text.split('\n')) {
        lines << line.simplified();
    }
    return lines.join(QStringLiteral("\n"));
}

template <typename Metrics>
QString wrapCellTextWithMetrics(const QString& text, const Metrics& fm, int width)
{
    QStringList paragraphs = preparePdfCellTextForWrap(text).split('\n');
    QStringList out;

    auto splitLongToken = [&](const QString& token) {
        QStringList chunks;
        QString current;
        for (const QChar ch : token) {
            const QString candidate = current + ch;
            if (!current.isEmpty() && fm.horizontalAdvance(candidate) > width) {
                chunks << current;
                current = QString(ch);
            } else {
                current = candidate;
            }
        }
        if (!current.isEmpty()) chunks << current;
        return chunks;
    };

    for (const QString& para : paragraphs) {
        const QStringList words = para.split(' ', Qt::SkipEmptyParts);
        QString line;
        for (const QString& rawWord : words) {
            QStringList pieces;
            if (rawWord.isEmpty()) {
                continue;
            } else if (fm.horizontalAdvance(rawWord) > width) {
                pieces = splitLongToken(rawWord);
            } else {
                pieces << rawWord;
            }

            for (const QString& piece : pieces) {
                const QString candidate = line.isEmpty() ? piece : (line + ' ' + piece);
                if (!line.isEmpty() && fm.horizontalAdvance(candidate) > width) {
                    out << line;
                    line = piece;
                } else {
                    line = candidate;
                }
            }
        }
        out << line;
    }

    return out.join("\n");
}

QString wrapCellTextForDevice(const QString& text, const QFont& font, int width, const QPaintDevice* device)
{
    if (device) {
        const QFontMetricsF fm(font, device);
        return wrapCellTextWithMetrics(text, fm, width);
    }
    const QFontMetrics fm(font);
    return wrapCellTextWithMetrics(text, fm, width);
}

} // namespace

QString wrapCellText(const QString& text, const QFont& font, int width)
{
    return wrapCellTextForDevice(text, font, width, nullptr);
}

int wrappedHeight(QPainter& p, const QString& text, const QFont& font, int width, int minHeight)
{
    p.setFont(font);
    const int innerWidth = qMax(8, width - 12);
    const QString wrapped = wrapCellTextForDevice(text, font, innerWidth, p.device());
    QRect br = p.boundingRect(QRect(0, 0, innerWidth, 4000), Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop, wrapped);
    return qMax(minHeight, br.height() + 22);
}

void drawCell(QPainter& p, const QRect& rect, const QString& text, const QFont& font,
              Qt::Alignment align, const QColor& fill, qreal penWidth)
{
    p.save();
    p.setPen(QPen(Qt::black, penWidth));
    p.fillRect(rect, fill);
    p.drawRect(rect);
    p.setFont(font);
    QRect inner = rect.adjusted(6, 6, -6, -6);
    const Qt::Alignment effectiveAlign = (align & Qt::AlignVCenter) ? ((align & ~Qt::AlignVCenter) | Qt::AlignTop) : align;
    const QString wrapped = wrapCellTextForDevice(text, font, qMax(8, inner.width()), p.device());
    p.drawText(inner, effectiveAlign | Qt::TextWordWrap, wrapped);
    p.restore();
}

} // namespace ReportsPdfPaintingSupport
