#pragma once
#include <QSqlDatabase>
#include <QDate>
#include <QHash>
#include <QSet>
#include <QString>
#include <QList>

namespace ClientReceiptReader {
bool beginTrackingNewOrder(QSqlDatabase db, const QString& uid, QString* error);
struct Result {
    QHash<QString,qint64> centsByOrder;
    bool available = false;
    QString error;
    bool ok() const { return error.isEmpty(); }
};
// Reads only recorded customer receipts. Missing history is never inferred.
Result read(QSqlDatabase db, const QSet<QString>& orderUids);
struct Payment {
    QString uid;
    QDate paidOn;
    qint64 cents;
    QString method;
};
struct JournalEntry {
    Payment payment;
    QString orderUid;
    QString actor;
};
struct Journal {
    QList<JournalEntry> entries;
    qint64 directPaymentsCents = 0;
    bool available = false;
    QString error;
};
Journal readJournal(QSqlDatabase db, QDate firstDay, QDate lastDay);
struct PaymentHistory {
    QList<Payment> payments;
    qint64 receivedBefore = 0;
    bool available = false;
    QString error;
};
PaymentHistory readPeriod(QSqlDatabase db, const QString& orderUid, QDate firstDay, QDate lastDay);
struct Total {
    qint64 cents = 0;
    QString error;
    bool ok() const { return error.isEmpty(); }
};
Total total(const QList<qint64>& values);
QString money(qint64 cents);
}
