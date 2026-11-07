#pragma once

#include <QList>

#include "core/services/ReportsPdfLayoutSupport.h"
#include "core/services/ReportsSyncSupport.h"

class QFont;
class QPainter;
class QPdfWriter;

namespace ReportsPdfElectricityPageSupport {

void renderElectricityPage(
    QPdfWriter& pdf,
    QPainter& painter,
    const ReportsPdfLayoutSupport::PdfTheme& theme,
    const QList<ElectricityPeriodRow>& rows,
    const QFont& reportTitleFont,
    const QFont& reportSubtitleFont,
    const QFont& headerFont,
    const QFont& cellFont);

} // namespace ReportsPdfElectricityPageSupport
