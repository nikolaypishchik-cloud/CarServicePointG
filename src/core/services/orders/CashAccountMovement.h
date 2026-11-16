#pragma once
#include <QDate>
#include <QSqlDatabase>
#include <QString>

namespace CashAccountMovement {
struct Command {QString uid,from,to,reason;QDate day;qint64 cents;};
struct Result {QString error;bool replay=false;};
// An empty source records a verified opening balance of an existing non-drawer account.
Result post(const Command& c);
QString displayBalance(const QString& account);
QString balance(QSqlDatabase db,const QString& account,qint64& cents);
}
