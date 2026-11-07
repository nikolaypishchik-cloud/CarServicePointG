#pragma once

#include <functional>
#include <QString>

class QWidget;
class QTableView;
class QModelIndex;

namespace ReportsInteractionSupport {

void openInvoiceFromReport(QWidget* parent, const QModelIndex& index);

void configurePayrollTableContextMenu(
    QWidget* parent,
    QTableView* payrollTable,
    const std::function<void()>& refreshPayroll,
    const std::function<void()>& queueRefreshAll,
    const std::function<QString()>& archiveDateIso);

void openOrderFromReportTable(
    QWidget* parent,
    QTableView* table,
    int orderColumn,
    int row,
    const std::function<void()>& onAccepted);

void openOutsourceOrOrderFromReportTable(
    QWidget* parent,
    QTableView* table,
    int row,
    int orderColumn,
    const std::function<void()>& onAccepted);

void publishExpensesBackupEvent(
    const QString& dateFromIso,
    const QString& dateToIso,
    double totalAmount,
    double electricityTariff);

} // namespace ReportsInteractionSupport
