#pragma once
#include <QSqlDatabase>
#include <QTime>
#include <QStringList>
namespace CashDeskPolicy {
struct Policy {
    QString uid, responsible, branch, zone, reason;
    QTime start{8,0}, end{18,0};
    qint64 changeCents=0;
};
Policy latest(QString& error);
QString validate(const Policy& policy);
Policy latest(QSqlDatabase db,QString& error,const QString& shift={});
QList<QPair<QString,QString>> operators(QString& error);
QString save(const Policy& policy);
QString validate(QSqlDatabase db,const Policy& policy);
QString bindShift(QSqlDatabase db,const QString& shift);
}
