#include "core/services/ReportsPdfElectricityPageSupport.h"

#include "core/services/ReportsPdfLayoutSupport.h"
#include "core/services/ReportsDateUtils.h"
#include "core/services/ReportsPdfPaintingSupport.h"
#include "core/util/FormatUtils.h"

#include <QColor>
#include <QDate>
#include <QFont>
#include <QPainter>
#include "core/services/PdfWebsiteFooter.h"
#include <QPdfWriter>

namespace {

QString money0(double value)
{
    return FormatUtils::money(value, 0);
}

} // namespace

namespace ReportsPdfElectricityPageSupport {

void renderElectricityPage(
    QPdfWriter& pdf,
    QPainter& painter,
    const ReportsPdfLayoutSupport::PdfTheme& theme,
    const QList<ElectricityPeriodRow>& rows,
    const QFont& reportTitleFont,
    const QFont& reportSubtitleFont,
    const QFont& headerFont,
    const QFont& cellFont)
{
    using ReportsPdfLayoutSupport::mmToPx;
    using ReportsPdfPaintingSupport::drawCell;
    using ReportsPdfPaintingSupport::drawCenteredText;
    using ReportsPdfPaintingSupport::kPdfTablePenWidth;

    const int titleTopGap = mmToPx(&pdf, 6.0);
    const int titleH = mmToPx(&pdf, 14.0);
    const int subtitleH = mmToPx(&pdf, 9.0);
    const int titleToSubtitleGap = mmToPx(&pdf, 1.0);
    const int subtitleToTableGap = mmToPx(&pdf, 7.0);
    const int tableX = theme.left + mmToPx(&pdf, 17.0);
    const int tableRightMargin = mmToPx(&pdf, 6.0);
    const int tableW = pdf.width() - tableX - tableRightMargin;
    const QVector<int> widths = {
        mmToPx(&pdf, 53), mmToPx(&pdf, 24), mmToPx(&pdf, 23), mmToPx(&pdf, 17), mmToPx(&pdf, 21),
        tableW - (mmToPx(&pdf, 53) + mmToPx(&pdf, 24) + mmToPx(&pdf, 23) + mmToPx(&pdf, 17) + mmToPx(&pdf, 21))
    };
    const QStringList headers = {
        QString::fromUtf8("Період"),
        QString::fromUtf8("Лічильник до"),
        QString::fromUtf8("Лічильник після"),
        QString::fromUtf8("Спожито кВт"),
        QString::fromUtf8("Ціна кВт"),
        QString::fromUtf8("Вартість")
    };
    const int headerH = mmToPx(&pdf, 11.0);
    const int bodyH = mmToPx(&pdf, 9.0);

    auto drawPageHeader = [&](int& y) {
        y = theme.top + titleTopGap;
        drawCenteredText(painter, QRect(theme.left, y, theme.contentWidth, titleH), QStringLiteral("CarService PointG 2.1.2"), reportTitleFont);
        y += titleH + titleToSubtitleGap;
        drawCenteredText(painter, QRect(theme.left, y, theme.contentWidth, subtitleH), QString::fromUtf8("Звіт по електроенергії"), reportSubtitleFont);
        y += subtitleH + subtitleToTableGap;

        int x = tableX;
        for (int i = 0; i < headers.size(); ++i) {
            drawCell(painter, QRect(x, y, widths[i], headerH), headers[i], headerFont,
                     Qt::AlignCenter, QColor(220, 220, 220), kPdfTablePenWidth);
            x += widths[i];
        }
        y += headerH;
    };

    int y = theme.top;
    drawPageHeader(y);

    for (const auto& row : rows) {
        if (y + bodyH > theme.bottom) {
            pdf.newPage();
            painter.resetTransform();
            drawPageHeader(y);
        }

        const QString period = ReportsDateUtils::displayDate(row.periodFrom) + QString::fromUtf8(" — ") + ReportsDateUtils::displayDate(row.periodTo);
        const QStringList cells = {
            period,
            money0(row.meterBefore),
            money0(row.meterAfter),
            money0(row.consumedKwh),
            money0(row.tariff),
            money0(row.amount)
        };

        int x = tableX;
        for (int i = 0; i < cells.size(); ++i) {
            const Qt::Alignment align = (i == 0)
                ? (Qt::AlignLeft | Qt::AlignVCenter)
                : (Qt::AlignRight | Qt::AlignVCenter);
            drawCell(painter, QRect(x, y, widths[i], bodyH), cells[i], cellFont, align, Qt::white, kPdfTablePenWidth);
            x += widths[i];
        }
        y += bodyH;
    }
}

} // namespace ReportsPdfElectricityPageSupport
