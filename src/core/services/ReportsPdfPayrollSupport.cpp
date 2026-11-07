#include "core/services/ReportsPdfPayrollSupport.h"

#include "core/util/FormatUtils.h"

#include <QtMath>

namespace ReportsPdfPayrollSupport {

QStringList payrollHeaders()
{
    return {
        QString::fromUtf8("Дата"),
        QString::fromUtf8("№ наряду"),
        QString::fromUtf8("Держ.номер / марка авто"),
        QString::fromUtf8("Виконані роботи"),
        QString::fromUtf8("Вартість\nробіт")
    };
}

QStringList adminPayrollHeaders()
{
    return {
        QString::fromUtf8("Кіл-ть\nнарядів"),
        QString::fromUtf8("Замовлено\nзапчастин (од)"),
        QString::fromUtf8("Ціна\nзакупки"),
        QString::fromUtf8("Маржа"),
        QString::fromUtf8("Повна\nціна")
    };
}

QString displayMasterName(QString masterName)
{
    if (masterName.startsWith(QString::fromUtf8("Адмін:"))) {
        return masterName.mid(QString::fromUtf8("Адмін:").size()).trimmed();
    }
    return masterName.trimmed();
}

QString roleTitleForMaster(bool isAdmin)
{
    return isAdmin ? QString::fromUtf8("Адмін") : QString::fromUtf8("Майстер");
}

QStringList buildPayrollRowCells(const PayrollOrderRow& row)
{
    return {
        row.dateStart,
        row.orderNumber,
        row.car,
        row.works,
        FormatUtils::money(row.masterAmount, 0) + QString::fromUtf8(" грн")
    };
}

QStringList buildAdminPayrollCells(const ReportsPdfExpenseSupport::AdminSalaryTotals& totals)
{
    const double purchase = totals.purchaseTotal;
    const double margin = totals.margin;
    const double fullSum = purchase + margin;
    return {
        QString::number(totals.ordersCount),
        QString::number(qRound64(totals.partsQty)),
        FormatUtils::money(purchase, 0) + QString::fromUtf8(" грн"),
        FormatUtils::money(margin, 0) + QString::fromUtf8(" грн"),
        FormatUtils::money(fullSum, 0) + QString::fromUtf8(" грн")
    };
}

QList<PayrollSummaryLine> buildMasterSummaryLines(int ordersCount, double accrued)
{
    return {
        {QString::fromUtf8("Виконано нарядів:"), QString::number(ordersCount), false},
        {QString::fromUtf8("Нараховано:"), FormatUtils::money(accrued, 0) + QString::fromUtf8(" грн"), false}
    };
}

ReportsPdfExpenseSupport::ExpenseBreakdown buildSharedExpenseBreakdown(
    const ReportsPdfExpenseSupport::ExpenseBreakdown& totalExpenseBreakdown,
    int activeMasterCount)
{
    const double perMasterScale = activeMasterCount > 0 ? 1.0 / activeMasterCount : 0.0;
    return {
        totalExpenseBreakdown.electricity * perMasterScale,
        totalExpenseBreakdown.cleaning * perMasterScale,
        totalExpenseBreakdown.software * perMasterScale,
        totalExpenseBreakdown.heating * perMasterScale,
        totalExpenseBreakdown.amortization * perMasterScale,
        totalExpenseBreakdown.rent * perMasterScale,
        totalExpenseBreakdown.other * perMasterScale
    };
}

QList<QPair<QString, double>> buildExpenseItems(
    const ReportsPdfExpenseSupport::ExpenseBreakdown& sharedExpenseParts,
    const ReportsPdfExpenseSupport::ExpenseNotes& expenseNotes)
{
    QList<QPair<QString, double>> expenseItems;
    if (sharedExpenseParts.electricity > 0.0) expenseItems.push_back({QString::fromUtf8("Ел.енергія"), sharedExpenseParts.electricity});
    if (sharedExpenseParts.cleaning > 0.0) expenseItems.push_back({ReportsPdfExpenseSupport::expenseNoteJoined(QString::fromUtf8("Миючі засоби"), expenseNotes.cleaning), sharedExpenseParts.cleaning});
    if (sharedExpenseParts.amortization > 0.0) expenseItems.push_back({ReportsPdfExpenseSupport::expenseNoteJoined(QString::fromUtf8("Амортизація"), expenseNotes.amortization), sharedExpenseParts.amortization});
    if (sharedExpenseParts.rent > 0.0) expenseItems.push_back({ReportsPdfExpenseSupport::expenseNoteJoined(QString::fromUtf8("Оренда"), expenseNotes.rent), sharedExpenseParts.rent});
    if (sharedExpenseParts.heating > 0.0) expenseItems.push_back({ReportsPdfExpenseSupport::expenseNoteJoined(QString::fromUtf8("Опалення"), expenseNotes.heating), sharedExpenseParts.heating});
    const QString otherJoinedNote = ReportsPdfExpenseSupport::mergeExpenseNotes({expenseNotes.other, expenseNotes.software});
    if ((sharedExpenseParts.other + sharedExpenseParts.software) > 0.0) {
        expenseItems.push_back({ReportsPdfExpenseSupport::expenseNoteJoined(QString::fromUtf8("Інші витрати"), otherJoinedNote), sharedExpenseParts.other + sharedExpenseParts.software});
    }
    return expenseItems;
}

} // namespace ReportsPdfPayrollSupport
