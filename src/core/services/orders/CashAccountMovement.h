#pragma once
#include <QDate>
#include <QSqlDatabase>
#include <QString>

namespace CashAccountMovement {
struct Command {QString uid,from,to,reason;QDate day;qint64 cents;};
struct Result {QString error;bool replay=false;};
// An empty source records a verified opening balance of an existing non-drawer account.
Result post(const Command& c);
struct Transfer {QString uid,action,dispatch,from,to,recipient,reason;qint64 cents=0;};
Result physical(const Transfer& command);
QList<QPair<QString,QString>> recipients(QString& error);
QList<QPair<QString,QString>> inTransit(QString& error);
QString displayBalance(const QString& account);
QString balance(QSqlDatabase db,const QString& account,qint64& cents);
}
