#pragma once

#include <QDate>
#include <QMap>
#include <QList>

#include "core/services/ReportsPdfLayoutSupport.h"
#include "core/services/ReportsPdfSupport.h"

class QFont;
class QPainter;
class QPdfWriter;

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
    const QFont& summaryValueFont);

} // namespace ReportsPdfOutsourcePageSupport
