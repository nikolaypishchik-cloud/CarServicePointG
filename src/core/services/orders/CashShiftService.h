#pragma once
#include <QString>
#include <QSqlDatabase>
#include <QList>
#include <QPair>
namespace CashShiftService {
enum class Action { Freeze, Close, HandOver, Accept, OpenNext, Adjust };
struct State { QString uid, phase, operatorUid, recipient, error; };
struct Command { QString requestUid, shiftUid; Action action; qint64 counted; QString recipient, reason; };
struct Result { bool recorded=false; QString phase, message; };
QString balance(QSqlDatabase db,const QString& shift,qint64& amount,bool allowUnlinked=false);
State current();
QList<QPair<QString,QString>> recipients();
Result apply(const Command& command);
}
