#pragma once
#include <QDate>
#include <QList>
#include <QStringList>
namespace CashReport {
enum class Type { Shift, Account, Period, Advances, Orders, Refunds, Transfers, Discrepancies, Acquiring, Employees };
struct Table { QString title, scope, error; QStringList columns; QList<QStringList> rows; QStringList totals; };
QString title(Type type);
Table read(Type type,QDate first,QDate last,const QString& selector={});
QList<QPair<QString,QString>> shifts(QString& error);
}
