#pragma once
#include <QSqlDatabase>
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
struct Total {
    qint64 cents = 0;
    QString error;
    bool ok() const { return error.isEmpty(); }
};
Total total(const QList<qint64>& values);
QString money(qint64 cents);
}
