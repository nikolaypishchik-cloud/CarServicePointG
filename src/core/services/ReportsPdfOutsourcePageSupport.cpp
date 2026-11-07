#include "core/services/ReportsPdfOutsourcePageSupport.h"

#include "core/services/ReportsPdfLayoutSupport.h"
#include "core/services/ReportsPdfPaintingSupport.h"
#include "core/util/FormatUtils.h"

#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include "core/services/PdfWebsiteFooter.h"
#include <QPdfWriter>

namespace {

QString money2(double value)
{
    return FormatUtils::money(value, 2);
}

} // namespace

namespace ReportsPdfOutsourcePageSupport {

void renderOutsourcePages(
    QPdfWriter& pdf,
    QPainter& painter,
    const ReportsPdfLayoutSupport::PdfTheme& theme,
    const QDate& fromDate,
    const QDate& toDate,
    const QMap<QString, QList<ReportsPdfSupport::OutsourcePdfRow>>& groups,
    int count,
    double repairTotal,
    double marginTotal,
    const QFont& titleFont,
    const QFont& subtitleFont,
    const QFont& sectionFont,
    const QFont& headerFont,
    const QFont& cellFont,
    const QFont& summaryLabelFont,
    const QFont& summaryValueFont)
{
    using ReportsPdfLayoutSupport::mmToPx;
    using ReportsPdfPaintingSupport::drawCell;
    using ReportsPdfPaintingSupport::drawCenteredText;
    using ReportsPdfPaintingSupport::kPdfTablePenWidth;
    using ReportsPdfPaintingSupport::wrappedHeight;

    bool firstPage = true;
    int y = theme.top;

    const QVector<int> widths = {
        mmToPx(&pdf, 22),
        mmToPx(&pdf, 22),
        mmToPx(&pdf, 28),
        mmToPx(&pdf, 12),
        qMax(mmToPx(&pdf, 48), theme.contentWidth - (mmToPx(&pdf, 22) + mmToPx(&pdf, 22) + mmToPx(&pdf, 28) + mmToPx(&pdf, 12) + mmToPx(&pdf, 20) + mmToPx(&pdf, 18))),
        mmToPx(&pdf, 20),
        mmToPx(&pdf, 18)
    };
    const QStringList headers = {
        QString::fromUtf8("Дата\nвідправки"),
        QString::fromUtf8("План\nповернення"),
        QString::fromUtf8("Компанія"),
        QString::fromUtf8("Наряд"),
        QString::fromUtf8("Запчастина на ремонт"),
        QString::fromUtf8("Вартість\nремонту"),
        QString::fromUtf8("Ціна")
    };
    const int sectionH = mmToPx(&pdf, 10.0);
    const int headerRowH = mmToPx(&pdf, 13.5);
    const int summaryReserve = mmToPx(&pdf, 34);

    auto drawDocHeader = [&](int& yy) {
        drawCenteredText(painter, QRect(theme.left, yy, theme.contentWidth, 34), QStringLiteral("CarService PointG 2.1.2"), titleFont);
        yy += 36;
        drawCenteredText(painter, QRect(theme.left, yy, theme.contentWidth, 28),
                         QString::fromUtf8("АУТСОРС: %1 — %2")
                             .arg(fromDate.toString(QStringLiteral("dd.MM.yy")), toDate.toString(QStringLiteral("dd.MM.yy"))),
                         subtitleFont);
        yy += 34;
    };

    auto startPage = [&]() {
        if (!firstPage) pdf.newPage();
        firstPage = false;
        y = theme.top;
        drawDocHeader(y);
    };

    auto drawTableHeader = [&](int topY) {
        int x = theme.left;
        for (int i = 0; i < headers.size(); ++i) {
            drawCell(painter, QRect(x, topY, widths[i], headerRowH), headers[i], headerFont,
                     Qt::AlignCenter, Qt::white, kPdfTablePenWidth);
            x += widths[i];
        }
    };

    auto ensureSpace = [&](int needed) {
        if (y + needed <= theme.bottom) return;
        startPage();
    };

    startPage();
    bool drewAnySection = false;
    for (auto it = groups.begin(); it != groups.end(); ++it) {
        const auto& rows = it.value();
        const int minNeeded = sectionH + headerRowH + mmToPx(&pdf, 11.0);
        ensureSpace(minNeeded);
        if (drewAnySection) y += mmToPx(&pdf, 5.0);

        drawCell(painter, QRect(theme.left, y, theme.contentWidth, sectionH), it.key(), sectionFont,
                 Qt::AlignLeft | Qt::AlignVCenter, Qt::white, kPdfTablePenWidth);
        y += sectionH;
        drawTableHeader(y);
        y += headerRowH;
        drewAnySection = true;

        for (const auto& row : rows) {
            const QStringList cells = {
                row.date,
                row.returnDate.isEmpty() ? QString::fromUtf8("—") : row.returnDate,
                row.company,
                row.orderNumber,
                row.partName,
                money2(row.repairCost),
                money2(row.clientPrice)
            };
            int rowH = mmToPx(&pdf, 14.0);
            for (int i = 0; i < cells.size(); ++i) {
                rowH = qMax(rowH, wrappedHeight(painter, cells[i], cellFont, widths[i], mmToPx(&pdf, 13.0)));
            }
            if (y + rowH > theme.bottom - summaryReserve) {
                startPage();
                drawCell(painter, QRect(theme.left, y, theme.contentWidth, sectionH), it.key(), sectionFont,
                         Qt::AlignLeft | Qt::AlignVCenter, Qt::white, kPdfTablePenWidth);
                y += sectionH;
                drawTableHeader(y);
                y += headerRowH;
            }
            int x = theme.left;
            for (int i = 0; i < cells.size(); ++i) {
                const Qt::Alignment a = (i >= 5)
                    ? (Qt::AlignRight | Qt::AlignTop)
                    : (i == 3 ? (Qt::AlignCenter | Qt::AlignTop) : (Qt::AlignLeft | Qt::AlignTop));
                drawCell(painter, QRect(x, y, widths[i], rowH), cells[i], cellFont, a, Qt::white, kPdfTablePenWidth);
                x += widths[i];
            }
            y += rowH;
        }
    }

    const int summaryTopGap = mmToPx(&pdf, 8.0);
    const QFontMetrics summaryLabelFm(summaryLabelFont);
    const QFontMetrics summaryValueFm(summaryValueFont);
    const int summaryLineH = qMax(mmToPx(&pdf, 7.0), qMax(summaryLabelFm.height(), summaryValueFm.height()) + mmToPx(&pdf, 1.5));
    const int labelW = mmToPx(&pdf, 58);
    const int valueW = mmToPx(&pdf, 34);
    const int summaryX = theme.left;
    ensureSpace(summaryTopGap + summaryLineH * 3 + mmToPx(&pdf, 4.0));
    y += summaryTopGap;

    auto summaryLine = [&](const QString& label, const QString& value) {
        painter.setFont(summaryLabelFont);
        painter.drawText(QRect(summaryX, y, labelW, summaryLineH), Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap, label);
        painter.setFont(summaryValueFont);
        painter.drawText(QRect(summaryX + labelW + mmToPx(&pdf, 8.0), y, valueW, summaryLineH), Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap, value);
        y += summaryLineH;
    };

    summaryLine(QString::fromUtf8("Відправлено на ремонт"), QString::number(count));
    summaryLine(QString::fromUtf8("Вартість ремонту:"), money2(repairTotal) + QString::fromUtf8(" грн"));
    summaryLine(QString::fromUtf8("Маржа:"), money2(marginTotal) + QString::fromUtf8(" грн"));
}

} // namespace ReportsPdfOutsourcePageSupport
