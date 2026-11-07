#include "core/services/ReportsPdfPayrollPageSupport.h"

#include "core/services/ReportsPdfExpenseSupport.h"
#include "core/services/ReportsPdfPaintingSupport.h"
#include "core/services/ReportsPdfPayrollSupport.h"
#include "core/util/FormatUtils.h"

#include <QFontMetrics>
#include <QPainter>
#include "core/services/PdfWebsiteFooter.h"
#include <QPdfWriter>

namespace {

QString money0(double value)
{
    return FormatUtils::money(value, 0);
}

} // namespace

namespace ReportsPdfPayrollPageSupport {

void renderPayrollPages(
    QPdfWriter& pdf,
    QPainter& painter,
    const ReportsPdfLayoutSupport::PdfTheme& theme,
    const QDate& fromDate,
    const QDate& toDate,
    const QList<ReportsPdfSupport::PayrollMasterPdf>& masters,
    const QMap<QString, QString>& adminSources,
    const ReportsPdfExpenseSupport::AdminSalaryTotals& adminTotals,
    const ReportsPdfExpenseSupport::ExpenseBreakdown& expenseBreakdown,
    const ReportsPdfExpenseSupport::ExpenseNotes& expenseNotes,
    int activeMasterCount,
    const PayrollFonts& fonts)
{
    using ReportsPdfLayoutSupport::mmToPx;
    using ReportsPdfPaintingSupport::drawCell;
    using ReportsPdfPaintingSupport::drawCenteredText;
    using ReportsPdfPaintingSupport::drawIssueTextWithUnderline;
    using ReportsPdfPaintingSupport::kPdfTablePenWidth;
    using ReportsPdfPaintingSupport::ptToPx;
    using ReportsPdfPaintingSupport::wrappedHeight;

    bool firstPage = true;
    QVector<int> w = { mmToPx(&pdf, 16), mmToPx(&pdf, 12), mmToPx(&pdf, 56), 0, mmToPx(&pdf, 18) };
    const int fixed = w[0] + w[1] + w[2] + w[4];
    w[3] = qMax(mmToPx(&pdf, 74), theme.contentWidth - fixed);
    const QStringList headers = ReportsPdfPayrollSupport::payrollHeaders();
    const int tableHeaderH = mmToPx(&pdf, 9.5);
    const int pageBottomReserve = mmToPx(&pdf, 18.0);

    auto drawHeader = [&](int& y) {
        drawCenteredText(painter, QRect(theme.left, y, theme.contentWidth, 28), QStringLiteral("CarService PointG 2.1.2"), fonts.title);
        y += 28;
        painter.setPen(QPen(Qt::black, kPdfTablePenWidth));
        const int lineInset = mmToPx(&pdf, 32.0);
        painter.drawLine(theme.left + lineInset, y, theme.right - lineInset, y);
        y += 8;
        drawCenteredText(painter, QRect(theme.left, y, theme.contentWidth, 18),
                         QString::fromUtf8("ЗАРПЛАТА ЗА ПЕРІОД: %1 — %2")
                             .arg(fromDate.toString(QStringLiteral("dd.MM.yy")), toDate.toString(QStringLiteral("dd.MM.yy"))),
                         fonts.subtitle);
        y += 24;
    };

    auto drawMasterBanner = [&](int& y, const QString& roleTitle, const QString& displayName) {
        painter.setFont(fonts.person);
        painter.drawText(QRect(theme.left, y, theme.contentWidth, mmToPx(&pdf, 8.0)), Qt::AlignLeft | Qt::AlignVCenter,
                         QString::fromUtf8("%1: %2").arg(roleTitle, displayName));
        y += mmToPx(&pdf, 8.2);
        painter.setFont(fonts.period);
        painter.drawText(QRect(theme.left, y, theme.contentWidth, mmToPx(&pdf, 5.5)), Qt::AlignLeft | Qt::AlignVCenter,
                         QString::fromUtf8("Період: %1 — %2")
                             .arg(fromDate.toString(QStringLiteral("dd.MM.yy")), toDate.toString(QStringLiteral("dd.MM.yy"))));
        y += mmToPx(&pdf, 6.5);
    };

    auto drawMasterPairs = [&](int& y, const QList<ReportsPdfPayrollSupport::PayrollSummaryLine>& lines) {
        const int labelW = mmToPx(&pdf, 40.0);
        const int valueW = mmToPx(&pdf, 30.0);
        const int pairLineH = mmToPx(&pdf, 7.0);
        for (const auto& line : lines) {
            painter.setFont(line.emphasize ? fonts.pairEmphasis : fonts.pairLabel);
            painter.drawText(QRect(theme.left, y, labelW, pairLineH), Qt::AlignLeft | Qt::AlignVCenter, line.label);
            painter.setFont(line.emphasize ? fonts.pairEmphasis : fonts.pairValue);
            painter.drawText(QRect(theme.left + labelW + mmToPx(&pdf, 6.0), y, valueW, pairLineH), Qt::AlignLeft | Qt::AlignVCenter, line.value);
            y += pairLineH;
        }
        y += mmToPx(&pdf, 4.0);
    };

    auto buildExpenseItems = [&](const ReportsPdfExpenseSupport::ExpenseBreakdown& sharedExpenseParts) {
        return ReportsPdfPayrollSupport::buildExpenseItems(sharedExpenseParts, expenseNotes);
    };

    auto expenseBlockHeight = [&](const ReportsPdfExpenseSupport::ExpenseBreakdown& sharedExpenseParts) {
        if (sharedExpenseParts.total() <= 0.0) {
            return mmToPx(&pdf, 8.0);
        }
        const auto expenseItems = buildExpenseItems(sharedExpenseParts);
        const int valueW = mmToPx(&pdf, 28.0);
        const int labelW = qMax(mmToPx(&pdf, 50.0), theme.contentWidth - valueW - mmToPx(&pdf, 4.0));
        int total = mmToPx(&pdf, 7.0);
        QFontMetrics fm(fonts.expenseBody);
        for (const auto& item : expenseItems) {
            const int rowH = qMax(mmToPx(&pdf, 5.0), fm.boundingRect(QRect(0, 0, labelW, 4000), Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop, item.first).height());
            total += rowH + 2;
        }
        total += mmToPx(&pdf, 5.5);
        total += mmToPx(&pdf, 6.0);
        total += mmToPx(&pdf, 8.0);
        return total;
    };

    auto drawExpenseBlock = [&](int& y, const ReportsPdfExpenseSupport::ExpenseBreakdown& sharedExpenseParts, double accrued) {
        if (sharedExpenseParts.total() <= 0.0) {
            drawIssueTextWithUnderline(painter, QRect(theme.left, y, theme.contentWidth, mmToPx(&pdf, 8.0)),
                                       QString::fromUtf8("До видачі: %1 грн").arg(money0(accrued)),
                                       fonts.pairEmphasis, ptToPx(&pdf, 0.5));
            y += mmToPx(&pdf, 8.0);
            return;
        }

        const auto expenseItems = buildExpenseItems(sharedExpenseParts);
        const int valueW = mmToPx(&pdf, 28.0);
        const int labelW = qMax(mmToPx(&pdf, 50.0), theme.contentWidth - valueW - mmToPx(&pdf, 4.0));

        painter.setFont(fonts.expenseTitle);
        painter.drawText(QRect(theme.left, y, theme.contentWidth, mmToPx(&pdf, 6.0)), Qt::AlignLeft | Qt::AlignVCenter,
                         QString::fromUtf8("Утримано"));
        y += mmToPx(&pdf, 7.0);

        auto drawExpense = [&](int topY, int rowH, const QString& label, double value) {
            painter.setFont(fonts.expenseBody);
            painter.drawText(QRect(theme.left, topY, labelW, rowH), Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, label);
            painter.drawText(QRect(theme.left + labelW + mmToPx(&pdf, 4.0), topY, valueW, rowH),
                             Qt::AlignRight | Qt::AlignTop | Qt::TextWordWrap, money0(value) + QString::fromUtf8(" грн"));
        };

        QFontMetrics fm(fonts.expenseBody);
        for (const auto& item : expenseItems) {
            const int rowH = qMax(mmToPx(&pdf, 5.0), fm.boundingRect(QRect(0, 0, labelW, 4000), Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop, item.first).height());
            drawExpense(y, rowH, item.first, item.second);
            y += rowH + 2;
        }

        const int totalRowH = mmToPx(&pdf, 5.5);
        drawExpense(y, totalRowH, QString::fromUtf8("Всього утримано"), sharedExpenseParts.total());
        y += totalRowH;
        painter.setPen(QPen(Qt::black, kPdfTablePenWidth));
        painter.drawLine(theme.left, y + 2, theme.left + labelW + mmToPx(&pdf, 4.0) + valueW, y + 2);
        y += mmToPx(&pdf, 6.0);

        drawIssueTextWithUnderline(painter, QRect(theme.left, y, theme.contentWidth, mmToPx(&pdf, 8.0)),
                                   QString::fromUtf8("До видачі: %1 грн").arg(money0(accrued)),
                                   fonts.pairEmphasis, ptToPx(&pdf, 0.5));
        y += mmToPx(&pdf, 8.0);
    };

    auto drawTableHeader = [&](int y) {
        int x = theme.left;
        for (int i = 0; i < headers.size(); ++i) {
            drawCell(painter, QRect(x, y, w[i], tableHeaderH), headers[i], fonts.tableHeader,
                     Qt::AlignCenter, QColor(220, 220, 220), kPdfTablePenWidth);
            x += w[i];
        }
    };

    for (const auto& m : masters) {
        if (!firstPage) pdf.newPage();
        firstPage = false;
        int y = theme.top;
        drawHeader(y);

        const bool isAdmin = m.masterName.startsWith(QString::fromUtf8("Адмін:"));
        const QString displayName = ReportsPdfPayrollSupport::displayMasterName(m.masterName);
        const QString roleTitle = ReportsPdfPayrollSupport::roleTitleForMaster(isAdmin);
        drawMasterBanner(y, roleTitle, displayName);

        int x = theme.left;
        if (isAdmin) {
            const QString sourceKey = adminSources.value(m.masterId, QStringLiteral("margin"));
            const double accrued = qRound64(ReportsPdfExpenseSupport::adminSalaryBaseForSource(adminTotals, sourceKey) * m.percent / 100.0);
            QVector<int> aw = { mmToPx(&pdf, 24), mmToPx(&pdf, 30), mmToPx(&pdf, 30), mmToPx(&pdf, 24), 0 };
            const int afixed = aw[0] + aw[1] + aw[2] + aw[3];
            aw[4] = qMax(mmToPx(&pdf, 32), theme.contentWidth - afixed);
            const QStringList aheaders = ReportsPdfPayrollSupport::adminPayrollHeaders();
            for (int i = 0; i < aheaders.size(); ++i) {
                drawCell(painter, QRect(x, y, aw[i], mmToPx(&pdf, 10.0)), aheaders[i], fonts.tableHeader,
                         Qt::AlignCenter, QColor(220, 220, 220), kPdfTablePenWidth);
                x += aw[i];
            }
            y += mmToPx(&pdf, 10.0);

            const QStringList acells = ReportsPdfPayrollSupport::buildAdminPayrollCells(adminTotals);
            const int rowH = mmToPx(&pdf, 10.0);
            x = theme.left;
            for (int i = 0; i < acells.size(); ++i) {
                const Qt::Alignment a = (i >= 2) ? (Qt::AlignRight | Qt::AlignVCenter) : (Qt::AlignCenter | Qt::AlignVCenter);
                drawCell(painter, QRect(x, y, aw[i], rowH), acells[i], fonts.adminCell, a, Qt::white, kPdfTablePenWidth);
                x += aw[i];
            }
            y += rowH + mmToPx(&pdf, 5.0);
            drawMasterPairs(y, ReportsPdfPayrollSupport::buildMasterSummaryLines(adminTotals.ordersCount, accrued));
            drawIssueTextWithUnderline(painter, QRect(theme.left, y, theme.contentWidth, mmToPx(&pdf, 8.0)),
                                       QString::fromUtf8("До видачі: %1 грн").arg(money0(m.toIssue)),
                                       fonts.pairEmphasis, ptToPx(&pdf, 0.5));
            continue;
        }

        drawTableHeader(y);
        y += tableHeaderH;

        for (const auto& row : m.orders) {
            const QStringList cells = ReportsPdfPayrollSupport::buildPayrollRowCells(row);
            int rowH = mmToPx(&pdf, 7.5);
            for (int i = 0; i < cells.size(); ++i) {
                rowH = qMax(rowH, wrappedHeight(painter, cells[i], fonts.tableCell, w[i], mmToPx(&pdf, 7.0)));
            }
            const int projectedFooter = mmToPx(&pdf, 40.0);
            if (y + rowH + projectedFooter > theme.bottom - pageBottomReserve) {
                pdf.newPage();
                y = theme.top;
                drawHeader(y);
                drawMasterBanner(y, roleTitle, displayName);
                drawTableHeader(y);
                y += tableHeaderH;
            }
            x = theme.left;
            for (int i = 0; i < cells.size(); ++i) {
                const Qt::Alignment a = (i == 4) ? (Qt::AlignRight | Qt::AlignTop) : (Qt::AlignLeft | Qt::AlignTop);
                drawCell(painter, QRect(x, y, w[i], rowH), cells[i], fonts.tableCell, a, Qt::white, kPdfTablePenWidth);
                x += w[i];
            }
            y += rowH;
        }

        const int summaryRowH = mmToPx(&pdf, 7.0);
        x = theme.left;
        const double grossAccrued = qRound64(m.worksTotal * m.percent / 100.0);
        drawCell(painter, QRect(x, y, w[0] + w[1] + w[2] + w[3], summaryRowH), QString::fromUtf8("ПІДСУМОК"), fonts.summary,
                 Qt::AlignCenter, Qt::white, kPdfTablePenWidth);
        drawCell(painter, QRect(x + w[0] + w[1] + w[2] + w[3], y, w[4], summaryRowH), money0(grossAccrued) + QString::fromUtf8(" грн"),
                 fonts.summary, Qt::AlignRight | Qt::AlignVCenter, Qt::white, kPdfTablePenWidth);
        y += summaryRowH + mmToPx(&pdf, 4.0);

        drawMasterPairs(y, ReportsPdfPayrollSupport::buildMasterSummaryLines(m.ordersCount, grossAccrued));

        const ReportsPdfExpenseSupport::ExpenseBreakdown sharedExpenseParts =
            ReportsPdfPayrollSupport::buildSharedExpenseBreakdown(expenseBreakdown, activeMasterCount);
        if (y + expenseBlockHeight(sharedExpenseParts) > theme.bottom - pageBottomReserve) {
            pdf.newPage();
            y = theme.top;
            drawHeader(y);
            drawMasterBanner(y, roleTitle, displayName);
            drawMasterPairs(y, ReportsPdfPayrollSupport::buildMasterSummaryLines(m.ordersCount, grossAccrued));
        }
        drawExpenseBlock(y, sharedExpenseParts, m.toIssue);
    }
}

} // namespace ReportsPdfPayrollPageSupport
