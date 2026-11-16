#pragma once
#include <QDate>
#include <QString>
#include <QList>

namespace CashOutflow {
struct Command {
    QString uid, kind, source, reason;
    QDate date;
    qint64 cents;
    QString category;
};
struct Result {QString error; bool replay=false;};
struct Entry {QString uid,kind,source,reason,method,actor,date; qint64 cents;};
struct Totals {
    qint64 received=0,refunded=0,net=0,freeAdvances=0,allNet=0;
    QString error;
};
Totals totals(QDate first,QDate last);
bool allowed(bool expense);
bool canReverse();
Result post(const Command& command);
QList<Entry> ledger(QDate from,QDate to,QString& error);
}
