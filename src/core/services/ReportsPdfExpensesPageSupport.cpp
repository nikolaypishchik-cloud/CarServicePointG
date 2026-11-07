#include "core/services/ReportsPdfExpensesPageSupport.h"

#include "core/services/ReportsPdfExpenseSupport.h"
#include "core/services/ReportsPdfLayoutSupport.h"
#include "core/services/ReportsDateUtils.h"
#include "core/services/ReportsPdfPaintingSupport.h"
#include "core/util/FormatUtils.h"

#include <QFont>
#include <QPainter>
#include <QPdfWriter>

namespace {

QString money0(double value)
{
    return FormatUtils::money(value, 0);
}

} // namespace

namespace ReportsPdfExpensesPageSupport {

void renderExpensesPage(
    QPdfWriter& pdf,
    QPainter& painter,
    const ReportsPdfLayoutSupport::PdfTheme& theme,
    const QString& fromIso,
    const QString& toIso,
    const ReportsPdfRenderer::ExpensesFormData& data,
    const QFont& reportTitleFont,
    const QFont& reportSubtitleFont,
    const QFont& rowFont,
    const QFont& rowBoldFont)
{
    using ReportsPdfLayoutSupport::mmToPx;
    using ReportsPdfPaintingSupport::drawCell;
    using ReportsPdfPaintingSupport::drawCenteredText;
    using ReportsPdfPaintingSupport::kPdfTablePenWidth;
    using ReportsPdfPaintingSupport::wrappedHeight;

    const int titleTopGap = mmToPx(&pdf, 5.0);
    const int titleH = mmToPx(&pdf, 14.0);
    const int subtitleH = mmToPx(&pdf, 9.0);
    const int titleToSubtitleGap = mmToPx(&pdf, 1.0);
    const int subtitleToTableGap = mmToPx(&pdf, 7.0);
    const int sectionGap = mmToPx(&pdf, 3.5);
    const int tableX = theme.left;
    const int tableW = theme.contentWidth;
    const QVector<int> widths = { tableW - mmToPx(&pdf, 56), mmToPx(&pdf, 56) };

    int y = theme.top + titleTopGap;
    drawCenteredText(painter, QRect(theme.left, y, theme.contentWidth, titleH), QStringLiteral("CarService PointG 2.1.2"), reportTitleFont);
    y += titleH + titleToSubtitleGap;
    drawCenteredText(painter, QRect(theme.left, y, theme.contentWidth, subtitleH), QString::fromUtf8("Розрахунок витрат"), reportSubtitleFont);
    y += subtitleH + subtitleToTableGap;

    auto drawRow = [&](const QString& label, const QString& value, bool bold = false) {
        const QFont& font = bold ? rowBoldFont : rowFont;
        const int minH = mmToPx(&pdf, 10.0);
        const int rowH = qMax(minH,
                              qMax(wrappedHeight(painter, label, font, widths[0], minH),
                                   wrappedHeight(painter, value, font, widths[1], minH)));
        drawCell(painter, QRect(tableX, y, widths[0], rowH), label, font,
                 Qt::AlignLeft | Qt::AlignVCenter, Qt::white, kPdfTablePenWidth);
        drawCell(painter, QRect(tableX + widths[0], y, widths[1], rowH), value, font,
                 Qt::AlignRight | Qt::AlignVCenter, Qt::white, kPdfTablePenWidth);
        y += rowH;
    };

    drawRow(QString::fromUtf8("Період"), ReportsDateUtils::displayDate(fromIso) + QString::fromUtf8(" — ") + ReportsDateUtils::displayDate(toIso), true);
    drawRow(QString::fromUtf8("Лічильник минулий"), money0(data.elecPrev));
    drawRow(QString::fromUtf8("Лічильник поточний"), money0(data.elecCurr));
    drawRow(QString::fromUtf8("Споживання кВт"), money0(data.elecKwh));
    drawRow(QString::fromUtf8("Тариф"), money0(data.elecTariff));
    drawRow(QString::fromUtf8("Всього"), money0(data.elecCost) + QString::fromUtf8(" грн"), true);

    y += sectionGap;

    drawRow(ReportsPdfExpenseSupport::expenseNoteJoined(QString::fromUtf8("Витрати на миючі засоби"), data.cleaningNote), money0(data.cleaning) + QString::fromUtf8(" грн"));
    drawRow(ReportsPdfExpenseSupport::expenseNoteJoined(QString::fromUtf8("Витрати на ПЗ OpenDataBot"), data.softwareNote), money0(data.software) + QString::fromUtf8(" грн"));
    drawRow(ReportsPdfExpenseSupport::expenseNoteJoined(QString::fromUtf8("Опалення"), data.heatingNote), money0(data.heating) + QString::fromUtf8(" грн"));
    drawRow(ReportsPdfExpenseSupport::expenseNoteJoined(QString::fromUtf8("Амортизація"), data.amortizationNote), money0(data.amortization) + QString::fromUtf8(" грн"));
    drawRow(ReportsPdfExpenseSupport::expenseNoteJoined(QString::fromUtf8("Оренда"), data.rentNote), money0(data.rent) + QString::fromUtf8(" грн"));
    drawRow(ReportsPdfExpenseSupport::expenseNoteJoined(QString::fromUtf8("Інші витрати"), data.otherNote), money0(data.other) + QString::fromUtf8(" грн"));
    drawRow(QString::fromUtf8("Всього витрат"), money0(data.total) + QString::fromUtf8(" грн"), true);
}

} // namespace ReportsPdfExpensesPageSupport
