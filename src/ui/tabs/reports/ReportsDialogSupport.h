#pragma once

#include <QList>
#include <QString>

#include "core/services/ReportsSyncSupport.h"

class QWidget;

namespace ReportsDialogSupport {
QWidget* createInvoicePdfPreview(QWidget* parent, const QString& path);

void openPayrollOrdersDialog(QWidget* parent,
                             const QString& masterId,
                             const QString& masterLabel,
                             const QList<PayrollOrderRow>& orders);

} // namespace ReportsDialogSupport
