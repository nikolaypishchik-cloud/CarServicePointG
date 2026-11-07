#pragma once
#include "core/services/PurchasePaymentService.h"
#include <QStringList>
#include <QMap>
#include "core/services/SparePaymentAllocation.h"

struct RegisteredInvoice {
    QString remoteIdentity;
    QString key, number, supplier, filePath;
    QDate date;
    QDate dueDate;
    QDateTime paidAt;
    QVector<PurchaseDebt> parts;
    QVector<SparePaymentAllocation::Distribution> distribution;
    QMap<int, QString> orders;
    QStringList vehicles;
    qint64 totalCents = 0, paidCents = 0;
    bool amountMismatch = false;
    bool apportioned = false;
    qint64 remainingCents() const { return qMax<qint64>(0, totalCents - paidCents); }
    bool paid() const { return remainingCents() == 0 && !amountMismatch; }
};

namespace InvoiceRegistryService {
enum class ReportSort { Date, Supplier, LongestOverdue, NoDebt, DateAscending };
QDate paymentDeadline(const RegisteredInvoice& invoice);
QVector<RegisteredInvoice> sortedForReport(QVector<RegisteredInvoice> invoices, ReportSort mode, const QDate& today);
QVector<RegisteredInvoice> load(QSqlDatabase db, QString* error = nullptr);
}
