#pragma once

#include <QString>
#include <QVector>

#include "core/services/ReportsPdfLayoutSupport.h"
#include "core/services/ReportsPdfRenderer.h"

class QFont;
class QPainter;
class QPdfWriter;

namespace ReportsPdfExpensesPageSupport {

bool renderExpensesPage(
    QPdfWriter& pdf,
    QPainter& painter,
    const ReportsPdfLayoutSupport::PdfTheme& theme,
    const QString& fromIso,
    const QString& toIso,
    const ReportsPdfRenderer::ExpensesFormData& data);

} // namespace ReportsPdfExpensesPageSupport
