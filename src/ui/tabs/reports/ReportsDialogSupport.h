#pragma once

#include <QList>
#include <QStringList>

#include "core/services/ReportsSyncSupport.h"

class QWidget;
class QTableView;
class QModelIndex;

namespace ReportsDialogSupport {
int chooseLinkedRecord(QTableView* table, const QModelIndex& cell, const QString& title,
                       const QStringList& labels, bool invoices);
QWidget* createInvoicePdfPreview(QWidget* parent, const QString& path);

void openPayrollOrdersDialog(QWidget* parent,
                             const QString& masterId,
                             const QString& masterLabel,
                             const QList<PayrollOrderRow>& orders);

} // namespace ReportsDialogSupport
