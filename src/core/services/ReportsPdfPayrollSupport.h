#pragma once

#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

#include "core/services/ReportsPdfExpenseSupport.h"
#include "core/services/ReportsPdfSupport.h"

namespace ReportsPdfPayrollSupport {

struct PayrollSummaryLine {
    QString label;
    QString value;
    bool emphasize = false;
};

QStringList payrollHeaders();
QStringList adminPayrollHeaders();
QString displayMasterName(QString masterName);
QString roleTitleForMaster(bool isAdmin);
QStringList buildPayrollRowCells(const PayrollOrderRow& row);
QStringList buildAdminPayrollCells(const ReportsPdfExpenseSupport::AdminSalaryTotals& totals);
QList<PayrollSummaryLine> buildMasterSummaryLines(int ordersCount, double accrued);
ReportsPdfExpenseSupport::ExpenseBreakdown buildSharedExpenseBreakdown(
    const ReportsPdfExpenseSupport::ExpenseBreakdown& totalExpenseBreakdown,
    int activeMasterCount);
QList<QPair<QString, double>> buildExpenseItems(
    const ReportsPdfExpenseSupport::ExpenseBreakdown& sharedExpenseParts,
    const ReportsPdfExpenseSupport::ExpenseNotes& expenseNotes);

} // namespace ReportsPdfPayrollSupport
