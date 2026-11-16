#pragma once
#include <QDate>
#include <QSqlDatabase>
#include <QString>
#include <QList>

namespace CashBankService {
struct Account {QString uid,name,type,state;};
struct Channel {QString uid,account,name,terms;};
QList<Channel> channels(QString& error);
struct Credit {QString uid,accountUid,account,reference,day; qint64 net,fee,unmatched; QString channelName;};
struct Pending {QString uid,type,day; qint64 cents;};
QList<Pending> pending(QString& error);
struct Result {QString error;bool replay=false;};
struct Command {
    QString uid,action,account,reference,sourceType,source,reason;
    QDate date;
    qint64 net=0,fee=0,matched=0;
};
QList<Account> accounts(QString& error,bool includeInactive=false);
QString activeAccountError(QSqlDatabase db,const QString& account);
QList<Credit> credits(QString& error);
Result post(const Command& command);
}
