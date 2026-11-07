#pragma once

#include <QDate>
#include <QMap>
#include <QList>
#include <QFont>
#include <QString>

#include "core/services/ReportsPdfLayoutSupport.h"
#include "core/services/ReportsPdfSupport.h"

class QPainter;
class QPdfWriter;

namespace ReportsPdfPartsPageSupport {

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
    const QFont& oilReportTitleFont);

} // namespace ReportsPdfPartsPageSupport
