#pragma once
#include <QDate>
#include <QList>
#include <QString>
namespace CashAdvanceService {
struct Choice { QString uid, label; };
struct Command {
    QString requestUid, clientUid, orderUid, advanceUid, method, note;
    QDate day;
    qint64 cents = 0;
};
struct Result { QString operationUid, error; bool replay = false; };
struct Choices { QList<Choice> rows; QString error; };
QString designateSingleWorkstation();
struct Entry { QString uid, order, source; QDate day; qint64 cents; };
struct Ledger { QList<Entry> entries; QString error; };
Ledger ledger(QDate firstDay, QDate lastDay);
Choices clients();
Choices orders(const QString& clientUid);
Choices advances(const QString& clientUid);
Result post(const Command& command);
}
