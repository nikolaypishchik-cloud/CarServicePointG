#pragma once
#include <QSqlDatabase>
#include <QString>
#include <QList>

namespace CrmFinancialService {
struct Order {
    int number = 0;
    QString uid, car;
    qint64 totalCents = 0, receivedCents = 0;
    qint64 balanceCents() const { return totalCents - receivedCents; }
};
struct Snapshot {
    QList<Order> orders;
    qint64 totalCents = 0, receivedCents = 0, outstandingCents = 0, overpaidCents = 0;
    QString error;
    bool ledgerAvailable = false;
    bool documentedOrderReceipts = false;
    bool ok() const { return error.isEmpty(); }
};
// Recorded receipts only: historical receipts were not recorded and cannot be inferred.
Snapshot read(QSqlDatabase db, const QString& clientUid);
Snapshot readOrder(QSqlDatabase db, const QString& uid);
QString money(qint64 cents);
}
