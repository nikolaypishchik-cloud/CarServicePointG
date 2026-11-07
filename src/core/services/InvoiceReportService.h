#pragma once
#include "core/services/InvoiceRegistryService.h"

namespace InvoiceReportService {
struct Context { QString databasePath, scope, account; bool allowed=false; };
Context context();
QString activeAccount();
bool isCurrent(const Context& context);
QVector<RegisteredInvoice> load(const Context& context, QString* error);
QVector<RegisteredInvoice> localInvoices(QString* error);
bool canPayFullInvoice(const RegisteredInvoice& invoice);
bool payOrders(const RegisteredInvoice& invoice, const QVector<int>& orders, QString* error, qint64 amount);
struct OrderSummary {
    QVector<PurchaseDebt> debts;
    QVector<RegisteredInvoice> invoices;
    qint64 remainingCents=0;
    QDate dueDate, paidDate;
    bool paymentDateKnown=false;
};
bool canViewOrder(int orderNumber);
OrderSummary orderSummary(int orderNumber, QString* error);
}
