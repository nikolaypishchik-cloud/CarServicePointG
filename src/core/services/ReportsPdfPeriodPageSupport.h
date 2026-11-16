#pragma once

#include <QList>
#include <QFont>

#include "core/services/ReportsPdfLayoutSupport.h"
#include "core/services/ReportsPdfPeriodSupport.h"

class QPainter;
class QPdfWriter;

namespace ReportsPdfPeriodPageSupport {

int renderOrderDetails(QPdfWriter& pdf, QPainter& painter, const ReportsPdfLayoutSupport::PdfTheme& theme,
                       int pageNumber, const QList<ReportsPdfSupport::PeriodPdfRow>& rows);
int renderSummary(QPdfWriter& pdf,QPainter& painter,const ReportsPdfLayoutSupport::PdfTheme& theme,
                   QDate first,QDate last,const ReportsPdfSupport::PeriodSummary& summary);
bool renderPeriodPage(
    QPdfWriter& pdf,
    QPainter& painter,
    const ReportsPdfLayoutSupport::PdfTheme& theme,
    int pageNumber,
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
    const QFont& totalMarginFont);

} // namespace ReportsPdfPeriodPageSupport
