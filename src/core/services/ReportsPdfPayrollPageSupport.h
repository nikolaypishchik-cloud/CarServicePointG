#pragma once

#include <QDate>
#include <QFont>
#include <QList>
#include <QMap>
#include <QString>
#include <QVector>

#include "core/services/ReportsPdfExpenseSupport.h"
#include "core/services/ReportsPdfLayoutSupport.h"
#include "core/services/ReportsPdfSupport.h"

class QPainter;
class QPdfWriter;

namespace ReportsPdfPayrollPageSupport {

struct PayrollFonts {
    QFont title;
    QFont subtitle;
    QFont person;
    QFont period;
    QFont tableHeader;
    QFont tableCell;
    QFont pairLabel;
    QFont pairValue;
    QFont pairEmphasis;
    QFont adminCell;
    QFont summary;
    QFont expenseTitle;
    QFont expenseBody;
};

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
    const PayrollFonts& fonts);

} // namespace ReportsPdfPayrollPageSupport
