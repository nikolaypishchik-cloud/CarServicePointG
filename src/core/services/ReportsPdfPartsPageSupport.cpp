#include "core/services/ReportsPdfPartsPageSupport.h"

#include "core/services/ReportsPdfLayoutSupport.h"
#include "core/services/ReportsPdfPaintingSupport.h"
#include "core/services/ReportsPdfPartsSupport.h"
#include "core/util/StoreNameNormalizer.h"

#include <QPainter>
#include "core/services/PdfWebsiteFooter.h"
#include <QPdfWriter>
#include <QPen>
#include <numeric>

namespace ReportsPdfPartsPageSupport {

namespace {

QString displayStoreTitleForPartsReport(const QString& rawStoreTitle)
{
    if (rawStoreTitle == QString::fromUtf8("Моторне мастило")) {
        return QString::fromUtf8("Моторна олива");
    }
    return StoreNameNormalizer::canonKey(rawStoreTitle) == QStringLiteral("avtonova_d")
        ? QString::fromUtf8("Автонова-D")
        : rawStoreTitle;
}

} // namespace

void renderPartsPages(
    QPdfWriter& pdf,
    QPainter& painter,
    const ReportsPdfLayoutSupport::PdfTheme& theme,
    const QDate& fromDate,
    const QDate& toDate,
    const QMap<QString, QList<ReportsPdfSupport::PartPdfRow>>& groups,
    bool hasInvoiceColumn,
    const QFont& storeTitleFont,
    const QFont& reportTitleFont,
    const QFont& periodFont,
    const QFont& sectionTitleFont,
    const QFont& headerFont,
    const QFont& cellFont,
    const QFont& summaryLabelFont,
    const QFont& summaryValueFont,
    const QFont& summaryBoldFont,
    const QFont& oilReportTitleFont)
{
    using ReportsPdfLayoutSupport::mmToPx;
    using ReportsPdfPaintingSupport::drawCell;
    using ReportsPdfPaintingSupport::drawCenteredText;
    using ReportsPdfPaintingSupport::drawSingleLineFittedCell;
    using ReportsPdfPaintingSupport::kPdfTablePenWidth;
    using ReportsPdfPaintingSupport::wrappedHeight;

    const int pageBottom = theme.bottom - mmToPx(&pdf, 12.0);
    const int tableX = theme.left;
    const qreal tablePenWidth = kPdfTablePenWidth;

    QVector<int> w = {
        mmToPx(&pdf, 12),
        mmToPx(&pdf, 15),
        mmToPx(&pdf, 10),
        0,
        mmToPx(&pdf, 18),
        mmToPx(&pdf, 12),
        mmToPx(&pdf, 13),
        mmToPx(&pdf, 17)
    };
    if (hasInvoiceColumn) {
        w.push_back(mmToPx(&pdf, 19));
    }
    int fixed = w[0] + w[1] + w[2] + w[4] + w[5] + w[6] + w[7];
    if (hasInvoiceColumn) fixed += w[8];
    w[3] = qMax(mmToPx(&pdf, hasInvoiceColumn ? 46.0 : 54.0), theme.contentWidth - fixed);
    const int tableWidth = std::accumulate(w.begin(), w.end(), 0);

    const QStringList headers = {
        QString::fromUtf8("№ з/с"),
        QString::fromUtf8("Дата"),
        QString::fromUtf8("Наряд"),
        QString::fromUtf8("Назва"),
        QString::fromUtf8("Ціна л."),
        QString::fromUtf8("Доданий\n%"),
        QString::fromUtf8("Кіл-ть"),
        QString::fromUtf8("Вартість")
    };
    QStringList headersWithInvoice = headers;
    if (hasInvoiceColumn) {
        headersWithInvoice << QString::fromUtf8("№-Н");
    }

    QVector<int> oilW = {
        w[0],
        w[1],
        w[2],
        w[3] + w[5] + (hasInvoiceColumn ? w[8] : 0),
        w[4],
        w[6],
        w[7]
    };
    const int oilTableWidth = std::accumulate(oilW.begin(), oilW.end(), 0);
    const QStringList oilHeaders = {
        QString::fromUtf8("№ з/с"),
        QString::fromUtf8("Дата"),
        QString::fromUtf8("Наряд"),
        QString::fromUtf8("Назва"),
        QString::fromUtf8("Ціна л."),
        QString::fromUtf8("Кіл-ть"),
        QString::fromUtf8("Вартість")
    };

    const int tableHeaderH = mmToPx(&pdf, 9.0);
    const int sectionH = mmToPx(&pdf, 7.0);
    const int summaryRowH = mmToPx(&pdf, 5.0);
    const int summaryGap = mmToPx(&pdf, 4.0);
    const int interSectionGap = mmToPx(&pdf, 8.0);
    const int lineInset = mmToPx(&pdf, 14.0);
    const int summaryLabelW = mmToPx(&pdf, 34.0);
    const int summaryValueX = tableX + summaryLabelW;

    auto drawStoreHeader = [&](const QString& storeTitle, int& y) {
        const bool oilHeader = (storeTitle == QString::fromUtf8("Моторне мастило"));
        const QString displayStoreTitle = displayStoreTitleForPartsReport(storeTitle);
        const int headerTop = theme.top + mmToPx(&pdf, 3.0);
        y = headerTop;
        drawCenteredText(painter, QRect(theme.left, y, theme.contentWidth, mmToPx(&pdf, 9.0)), displayStoreTitle, storeTitleFont);
        y += mmToPx(&pdf, 10.0);
        painter.save();
        painter.setPen(QPen(Qt::black, tablePenWidth));
        painter.drawLine(tableX + lineInset, y, tableX + (oilHeader ? oilTableWidth : tableWidth) - lineInset, y);
        painter.restore();
        y += mmToPx(&pdf, 2.0);
        if (oilHeader) {
            drawCenteredText(painter, QRect(theme.left, y, theme.contentWidth, mmToPx(&pdf, 5.0)),
                             QString::fromUtf8("Звіт придбання запчастин"), oilReportTitleFont);
            y += mmToPx(&pdf, 4.0);
        }
        drawCenteredText(painter, QRect(theme.left, y, theme.contentWidth, mmToPx(&pdf, 5.0)),
                         QString::fromUtf8("За період (дата з %1 по %2)")
                             .arg(fromDate.toString(QStringLiteral("dd.MM.yy")),
                                  toDate.toString(QStringLiteral("dd.MM.yy"))),
                         periodFont);
        y += mmToPx(&pdf, 7.0);
    };

    auto drawTableHeaderFor = [&](int y, const QStringList& tableHeaders, const QVector<int>& widths) {
        int x = tableX;
        for (int i = 0; i < tableHeaders.size(); ++i) {
            const QRect cellRect(x, y, widths[i], tableHeaderH);
            if (tableHeaders[i] == QString::fromUtf8("Назва")) {
                drawSingleLineFittedCell(painter, cellRect, tableHeaders[i], headerFont,
                                         Qt::AlignCenter | Qt::AlignVCenter, QColor(238, 238, 238), tablePenWidth,
                                         6.0, 0.85);
            } else if (tableHeaders[i] == QString::fromUtf8("Ціна л.")) {
                drawSingleLineFittedCell(painter, cellRect, tableHeaders[i], headerFont,
                                         Qt::AlignCenter | Qt::AlignVCenter, QColor(238, 238, 238), tablePenWidth,
                                         5.6, 0.82);
            } else if (tableHeaders[i] == QString::fromUtf8("Кіл-ть")) {
                drawSingleLineFittedCell(painter, cellRect, tableHeaders[i], headerFont,
                                         Qt::AlignCenter | Qt::AlignVCenter, QColor(238, 238, 238), tablePenWidth,
                                         5.6, 0.82);
            } else if (tableHeaders[i] == QString::fromUtf8("Вартість")) {
                drawSingleLineFittedCell(painter, cellRect, tableHeaders[i], headerFont,
                                         Qt::AlignCenter | Qt::AlignVCenter, QColor(238, 238, 238), tablePenWidth,
                                         5.4, 0.82);
            } else {
                drawCell(painter, cellRect, tableHeaders[i], headerFont,
                         Qt::AlignCenter | Qt::AlignVCenter, QColor(238, 238, 238), tablePenWidth);
            }
            x += widths[i];
        }
    };

    auto drawTableHeader = [&](int y) { drawTableHeaderFor(y, headersWithInvoice, w); };
    auto drawOilTableHeader = [&](int y) { drawTableHeaderFor(y, oilHeaders, oilW); };

    auto drawSectionTitleRow = [&](const QString& title, int y) {
        drawCell(painter, QRect(tableX, y, oilTableWidth, sectionH), title, sectionTitleFont,
                 Qt::AlignLeft | Qt::AlignVCenter, Qt::white, tablePenWidth);
    };

    auto rowHeightForCells = [&](const QStringList& cells, const QVector<int>& widths) {
        int rowH = mmToPx(&pdf, 8.0);
        for (int i = 0; i < cells.size(); ++i) {
            rowH = qMax(rowH, wrappedHeight(painter, cells[i], cellFont, widths[i], mmToPx(&pdf, 8.0)));
        }
        return rowH;
    };

    auto drawDataRow = [&](const QStringList& cells, const QVector<int>& widths, bool oilMode, int& y) {
        const int rowH = rowHeightForCells(cells, widths);
        int x = tableX;
        for (int i = 0; i < cells.size(); ++i) {
            const bool numericCol = oilMode ? (i == 4 || i == 5 || i == 6) : (i == 4 || i == 5 || i == 6 || i == 7);
            const Qt::Alignment align = numericCol ? (Qt::AlignRight | Qt::AlignTop) : (Qt::AlignLeft | Qt::AlignTop);
            drawCell(painter, QRect(x, y, widths[i], rowH), cells[i], cellFont, align, Qt::white, tablePenWidth);
            x += widths[i];
        }
        y += rowH;
    };

    auto summaryHeight = [&](bool oilMode) {
        return summaryGap + summaryRowH * (oilMode ? 2 : 4);
    };

    auto drawSummaryBlock = [&](int& y, int positions, double qtyTotal, bool oilUnits, double purchaseTotal, double addedTotal, double clientTotal, bool oilMode) {
        Q_UNUSED(positions);
        y += summaryGap;
        const auto lines = ReportsPdfPartsSupport::buildSummaryLines(qtyTotal, oilUnits, purchaseTotal, addedTotal, clientTotal, oilMode);
        for (const auto& line : lines) {
            painter.setFont(line.emphasize ? summaryBoldFont : summaryLabelFont);
            painter.drawText(QRect(tableX, y, summaryLabelW, summaryRowH), Qt::AlignLeft | Qt::AlignVCenter, line.label);
            painter.setFont(line.emphasize ? summaryBoldFont : summaryValueFont);
            painter.drawText(QRect(summaryValueX, y, mmToPx(&pdf, 35.0), summaryRowH), Qt::AlignLeft | Qt::AlignVCenter, line.value);
            y += summaryRowH;
        }
    };

    bool firstPage = true;
    auto startPageForStore = [&](const QString& storeTitle, int& y) {
        if (!firstPage) pdf.newPage();
        firstPage = false;
        drawStoreHeader(storeTitle, y);
    };

    const QString motorOilTitle = QString::fromUtf8("Моторне мастило");

    for (auto it = groups.begin(); it != groups.end(); ++it) {
        const QString storeTitle = it.key();
        const auto& rows = it.value();
        int y = theme.top;
        startPageForStore(storeTitle, y);

        if (storeTitle == motorOilTitle) {
            const auto oilGroups = ReportsPdfPartsSupport::groupMotorOilRowsBySection(rows);

            bool firstSection = true;
            for (auto oit = oilGroups.begin(); oit != oilGroups.end(); ++oit) {
                const auto& oilRows = oit.value();
                if (!firstSection) {
                    const int sectionReserve = interSectionGap + sectionH + tableHeaderH + summaryHeight(true);
                    if (y + sectionReserve > pageBottom) {
                        startPageForStore(storeTitle, y);
                    } else {
                        y += interSectionGap;
                    }
                }
                firstSection = false;

                drawSectionTitleRow(oit.key(), y);
                y += sectionH;
                drawOilTableHeader(y);
                y += tableHeaderH;

                int seq = 1;
                double qtyTotal = 0.0;
                double purchaseTotal = 0.0;
                double addedTotal = 0.0;
                double clientTotal = 0.0;

                for (int rowIndex = 0; rowIndex < oilRows.size(); ++rowIndex) {
                    const auto& row = oilRows[rowIndex];
                    const QStringList cells = ReportsPdfPartsSupport::buildPartCells(row, seq, true, hasInvoiceColumn);
                    const int rowH = rowHeightForCells(cells, oilW);
                    const bool isLastRow = (rowIndex == oilRows.size() - 1);
                    const int reserve = isLastRow ? summaryHeight(true) : 0;
                    if (y + rowH + reserve > pageBottom) {
                        startPageForStore(storeTitle, y);
                        drawSectionTitleRow(oit.key(), y);
                        y += sectionH;
                        drawOilTableHeader(y);
                        y += tableHeaderH;
                    }

                    drawDataRow(cells, oilW, true, y);
                    ++seq;
                    qtyTotal += row.qtyValue;
                    purchaseTotal += row.purchase;
                    addedTotal += row.added;
                    clientTotal += row.clientPrice;
                }

                drawSummaryBlock(y, oilRows.size(), qtyTotal, true, purchaseTotal, addedTotal, clientTotal, true);
            }
            continue;
        }

        drawTableHeader(y);
        y += tableHeaderH;

        int seq = 1;
        double qtyTotal = 0.0;
        double purchaseTotal = 0.0;
        double addedTotal = 0.0;
        double clientTotal = 0.0;

        for (int rowIndex = 0; rowIndex < rows.size(); ++rowIndex) {
            const auto& row = rows[rowIndex];
            const QStringList cells = ReportsPdfPartsSupport::buildPartCells(row, seq, false, hasInvoiceColumn);
            const int rowH = rowHeightForCells(cells, w);
            const bool isLastRow = (rowIndex == rows.size() - 1);
            const int reserve = isLastRow ? summaryHeight(false) : 0;
            if (y + rowH + reserve > pageBottom) {
                startPageForStore(storeTitle, y);
                drawTableHeader(y);
                y += tableHeaderH;
            }

            drawDataRow(cells, w, false, y);
            ++seq;
            qtyTotal += row.qtyValue;
            purchaseTotal += row.purchase;
            addedTotal += row.added;
            clientTotal += row.clientPrice;
        }

        drawSummaryBlock(y, rows.size(), qtyTotal, false, purchaseTotal, addedTotal, clientTotal, false);
    }
}

} // namespace ReportsPdfPartsPageSupport
