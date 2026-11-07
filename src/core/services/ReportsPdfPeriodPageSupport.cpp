#include "core/services/ReportsPdfPeriodPageSupport.h"

#include "core/services/ReportsPdfLayoutSupport.h"
#include "core/services/ReportsPdfPaintingSupport.h"
#include "core/util/FormatUtils.h"

#include <QPainter>
#include <QPdfWriter>
#include <QPen>
#include <numeric>

namespace {

QString money0(qint64 value)
{
    return FormatUtils::money(value, 0);
}

} // namespace

namespace ReportsPdfPeriodPageSupport {

void renderPeriodPage(
    QPdfWriter& pdf,
    QPainter& painter,
    const ReportsPdfLayoutSupport::PdfTheme& theme,
    const QString& title,
    const QString& subtitle,
    const QList<QStringList>& formattedRowCells,
    const ReportsPdfPeriodSupport::PeriodVisibleTotals& visibleTotals,
    const QFont& titleFont,
    const QFont& subtitleFont,
    const QFont& headerFontDefault,
    const QFont& headerFontWide,
    const QFont& tableFontDefault,
    const QFont& tableFontWide,
    const QFont& tableFontNarrow,
    const QFont& summaryFont,
    const QFont& summaryBoldFont,
    const QFont& totalMarginFont)
{
    using ReportsPdfLayoutSupport::mmToPx;
    using ReportsPdfLayoutSupport::newPdfPage;
    using ReportsPdfPaintingSupport::drawCell;
    using ReportsPdfPaintingSupport::drawCenteredText;
    using ReportsPdfPaintingSupport::drawLeftText;
    using ReportsPdfPaintingSupport::drawRightText;
    using ReportsPdfPaintingSupport::kPdfTablePenWidth;
    using ReportsPdfPaintingSupport::wrappedHeight;

    const int titleTopGap = mmToPx(&pdf, 1.5);
    const int titleH = mmToPx(&pdf, 15.0);
    const int subtitleH = mmToPx(&pdf, 8.5);
    const int titleToSubtitleGap = mmToPx(&pdf, 0.6);
    const int subtitleToTableGap = mmToPx(&pdf, 7.0);
    const int decorativeInset = mmToPx(&pdf, 1.0);
    const int titleUnderlineGap = mmToPx(&pdf, 1.0);
    const int headerRowH = mmToPx(&pdf, 11.0);
    const int summaryRowH = mmToPx(&pdf, 6.0);
    const int summaryGapAfterTable = mmToPx(&pdf, 5.0);
    const int summaryBlockH = summaryGapAfterTable + summaryRowH * 6 + mmToPx(&pdf, 5.0) + mmToPx(&pdf, 8.0);
    const int carryRows = 2;

    const QVector<qreal> columnWeights = {
        16.0, 11.0, 37.0, 19.0,
        50.0, 54.0, 10.0, 19.0,
        16.0, 19.0, 22.0
    };
    QVector<int> w;
    w.reserve(columnWeights.size());
    const qreal weightSum = std::accumulate(columnWeights.begin(), columnWeights.end(), 0.0);
    int usedWidth = 0;
    for (int i = 0; i < columnWeights.size(); ++i) {
        const int cw = (i == columnWeights.size() - 1)
            ? qMax(1, theme.contentWidth - usedWidth)
            : qMax(1, qRound(theme.contentWidth * columnWeights[i] / weightSum));
        w << cw;
        usedWidth += cw;
    }
    const int tableX = theme.left;
    const QStringList headers = ReportsPdfPeriodSupport::periodHeaders();

    auto headerFontForColumn = [&](int index) -> const QFont& {
        if (index == 4 || index == 5) return headerFontWide;
        return headerFontDefault;
    };
    auto tableFontForColumn = [&](int index) -> const QFont& {
        if (index == 2 || index == 3) return tableFontNarrow;
        if (index == 4 || index == 5) return tableFontWide;
        return tableFontDefault;
    };

    auto drawPageIntroAndHeader = [&](int& y) {
        y = theme.top + titleTopGap;
        drawCenteredText(painter, QRect(theme.left, y, theme.contentWidth, titleH), title, titleFont);
        painter.save();
        painter.setPen(QPen(Qt::black, kPdfTablePenWidth));
        const int decorativeLineY = y + titleH + titleUnderlineGap;
        painter.drawLine(theme.left + decorativeInset, decorativeLineY, theme.right - decorativeInset, decorativeLineY);
        painter.restore();
        y += titleH + titleToSubtitleGap;
        drawCenteredText(painter, QRect(theme.left, y, theme.contentWidth, subtitleH), subtitle, subtitleFont);
        y += subtitleH + subtitleToTableGap;

        int xx = tableX;
        for (int i = 0; i < headers.size(); ++i) {
            drawCell(painter, QRect(xx, y, w[i], headerRowH), headers[i], headerFontForColumn(i),
                     Qt::AlignLeft | Qt::AlignTop, QColor(220, 220, 220), kPdfTablePenWidth);
            xx += w[i];
        }
        y += headerRowH;
    };

    int y = 0;
    drawPageIntroAndHeader(y);

    int rowsLeft = formattedRowCells.size();
    int rowIndex = 0;
    for (const auto& cells : formattedRowCells) {
        int rowH = mmToPx(&pdf, 8.5);
        for (int i = 0; i < cells.size(); ++i) {
            rowH = qMax(rowH, wrappedHeight(painter, cells[i], tableFontForColumn(i), w[i], mmToPx(&pdf, 7.0)));
        }

        int carryHeight = rowH;
        if (rowIndex + 1 < formattedRowCells.size()) {
            const auto& nextCells = formattedRowCells[rowIndex + 1];
            int nextH = mmToPx(&pdf, 8.5);
            for (int i = 0; i < nextCells.size(); ++i) {
                nextH = qMax(nextH, wrappedHeight(painter, nextCells[i], tableFontForColumn(i), w[i], mmToPx(&pdf, 7.0)));
            }
            carryHeight += nextH;
        }

        if (y + rowH > theme.bottom || (rowsLeft > carryRows && rowsLeft <= carryRows + 2 && y + carryHeight + summaryBlockH > theme.bottom && y > theme.top + headerRowH + mmToPx(&pdf, 8.0))) {
            newPdfPage(pdf, painter, y, theme);
            drawPageIntroAndHeader(y);
        }

        int xx = tableX;
        for (int i = 0; i < cells.size(); ++i) {
            const Qt::Alignment align = (i >= 7) ? (Qt::AlignRight | Qt::AlignTop) : (Qt::AlignLeft | Qt::AlignTop);
            drawCell(painter, QRect(xx, y, w[i], rowH), cells[i], tableFontForColumn(i), align, Qt::white, kPdfTablePenWidth);
            xx += w[i];
        }
        y += rowH;
        --rowsLeft;
        ++rowIndex;
    }

    y += summaryGapAfterTable;
    const int summaryX = tableX;
    const int labelW = mmToPx(&pdf, 52.0);
    const int valueX = summaryX + mmToPx(&pdf, 58.0);
    const int valueW = mmToPx(&pdf, 18.0);
    const int currencyGap = mmToPx(&pdf, 2.0);
    const int currencyX = valueX + valueW + currencyGap;
    const int currencyW = mmToPx(&pdf, 12.0);

    for (const auto& line : ReportsPdfPeriodSupport::buildPeriodSummaryLines(visibleTotals)) {
        const QFont& lf = line.bold ? summaryBoldFont : summaryFont;
        const QFont& vf = line.bold ? summaryBoldFont : summaryFont;
        drawLeftText(painter, QRect(summaryX, y, labelW, summaryRowH), line.label, lf);
        drawRightText(painter, QRect(valueX, y, valueW, summaryRowH), money0(line.value), vf);
        drawLeftText(painter, QRect(currencyX, y, currencyW, summaryRowH), QString::fromUtf8("грн"), vf);
        y += summaryRowH;
    }

    y += mmToPx(&pdf, 2.5);
    painter.save();
    painter.setPen(QPen(Qt::black, kPdfTablePenWidth));
    painter.drawLine(summaryX, y, summaryX + mmToPx(&pdf, 43.0), y);
    painter.drawLine(summaryX + mmToPx(&pdf, 58.0), y, summaryX + mmToPx(&pdf, 86.0), y);
    painter.restore();
    y += mmToPx(&pdf, 3.5);

    drawLeftText(painter, QRect(summaryX, y, mmToPx(&pdf, 70.0), mmToPx(&pdf, 8.0)), QString::fromUtf8("Загальна маржа"), totalMarginFont);
    drawRightText(painter, QRect(valueX, y, valueW, mmToPx(&pdf, 8.0)), money0(visibleTotals.totalMargin), totalMarginFont);
    drawLeftText(painter, QRect(currencyX, y, currencyW, mmToPx(&pdf, 8.0)), QString::fromUtf8("грн"), totalMarginFont);
}

} // namespace ReportsPdfPeriodPageSupport
