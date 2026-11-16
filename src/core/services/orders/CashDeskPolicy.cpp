#include "CashDeskPolicy.h"
#include "CashAdvanceService.h"
#include "CashBankService.h"
#include "core/services/ActivityAuditService.h"
#include "core/services/AccessControlService.h"
#include "infrastructure/db/sqlite/Db.h"
#include <QDateTime>
#include <QSqlError>
#include <QSqlQuery>
#include <QScopeGuard>
#include <QTimeZone>
#include <QUuid>

namespace CashDeskPolicy {
namespace {
bool run(QSqlQuery& q,const QString& sql,const QVariantList& args){
    if(!q.prepare(sql))return false;
    for(const auto& value:args)q.addBindValue(value);
    return q.exec();
}
}
Policy latest(QString& error){return latest(Database::db(),error);}
QString validate(const Policy& p){return validate(Database::db(),p);}
Policy latest(QSqlDatabase db,QString& error,const QString& shift)
{
    Policy policy;
    policy.zone=QString::fromUtf8(QTimeZone::systemTimeZoneId());
    if(!AccessControlService::currentUserHasPermission(db,"cash.view")){error=QStringLiteral("Немає доступу.");return policy;}
    if(!db.tables().contains("cash_desk_policies"))return policy;
    QSqlQuery q(db);
    QString sql="SELECT uid,responsible,branch,zone,day_start,day_end,change_cents,reason FROM main.cash_desk_policies";
    QVariantList args;
    if(!shift.isEmpty()){
        if(!db.tables().contains("cash_shift_policies"))return policy;
        sql+=" WHERE uid=(SELECT policy_uid FROM main.cash_shift_policies WHERE shift_uid=?)";args<<shift;
    }
    if(!run(q,sql+" ORDER BY rowid DESC LIMIT 1",args)){error=q.lastError().text();return policy;}
    if(q.next())policy={q.value(0).toString(),q.value(1).toString(),q.value(2).toString(),q.value(3).toString(),q.value(7).toString(),
        QTime::fromString(q.value(4).toString(),"HH:mm"),QTime::fromString(q.value(5).toString(),"HH:mm"),q.value(6).toLongLong()};
    return policy;
}
QList<QPair<QString,QString>> operators(QString& error)
{
    auto db=Database::db();
    if(!AccessControlService::currentUserHasPermission(db,"cash.configure")){error=QStringLiteral("Немає прав налаштування каси.");return {};}
    QSqlQuery q(db);
    if(!q.exec("SELECT id,display_name,role FROM main.staff_accounts WHERE active=1 ORDER BY display_name,id")){error=q.lastError().text();return {};}
    QList<QPair<QString,QString>> result;
    while(q.next()){
        const auto role=q.value(2).toString();
        if(AccessControlService::hasPermission(db,role,"cash.shift") && AccessControlService::hasPermission(db,role,"cash.view"))
            result.append({q.value(0).toString(),q.value(1).toString()});
    }
    error=q.lastError().text();return result;
}
QString validate(QSqlDatabase db,const Policy& p)
{
    if(!AccessControlService::currentUserHasPermission(db,"cash.view"))return QStringLiteral("Немає прав перегляду каси.");
    if(p.zone.isEmpty() || !QTimeZone(p.zone.toUtf8()).isValid())return QStringLiteral("Оберіть чинний часовий пояс СТО.");
    if(!p.start.isValid() || !p.end.isValid() || p.start==p.end || p.changeCents<0)return QStringLiteral("Перевірте робочі години й розмінний залишок.");
    if(auto err=CashBankService::activeAccountError(db,"primary-cash");!err.isEmpty())return err;
    QSqlQuery q(db);
    if(!run(q,"SELECT role FROM main.staff_accounts WHERE id=? AND active=1",{p.responsible}))return q.lastError().text();
    if(!q.next() || !AccessControlService::hasPermission(db,q.value(0).toString(),"cash.shift") || !AccessControlService::hasPermission(db,q.value(0).toString(),"cash.view"))
        return QStringLiteral("Оберіть чинного відповідального з правом касової зміни.");
    return {};
}
QString save(const Policy& p)
{
    if(QUuid(p.uid).isNull() || p.reason.trimmed().isEmpty() || p.reason.size()>300 || p.branch.size()>100)return QStringLiteral("Перевірте назву підрозділу й підставу.");
    auto db=Database::db();QSqlQuery q(db);
    if(!q.exec("BEGIN IMMEDIATE"))return q.lastError().text();
    auto rollback=qScopeGuard([&]{db.rollback();});
    if(auto err=CashAdvanceService::postingAuthorityError("cash.configure");!err.isEmpty())return err;
    if(!q.exec("CREATE TABLE IF NOT EXISTS main.cash_desk_policies(uid TEXT PRIMARY KEY NOT NULL,responsible TEXT NOT NULL,branch TEXT,zone TEXT NOT NULL,day_start TEXT NOT NULL,day_end TEXT NOT NULL,change_cents INTEGER NOT NULL,reason TEXT NOT NULL,actor TEXT NOT NULL,recorded_ms INTEGER NOT NULL)"))return q.lastError().text();
    const auto actor=ActivityAuditService::currentActor().accountId;
    const QVariantList args{p.responsible,p.branch.trimmed(),p.zone,p.start.toString("HH:mm"),p.end.toString("HH:mm"),p.changeCents,p.reason.trimmed(),actor};
    if(!run(q,"SELECT responsible,branch,zone,day_start,day_end,change_cents,reason,actor FROM main.cash_desk_policies WHERE uid=?",{p.uid}))return q.lastError().text();
    if(q.next()){
        for(int i=0;i<args.size();++i)if(args[i].toString()!=q.value(i).toString())return QStringLiteral("Ключ має інші реквізити.");
        return {};
    }
    if(auto err=validate(db,p);!err.isEmpty())return err;
    if(!run(q,"INSERT INTO main.cash_desk_policies VALUES(?,?,?,?,?,?,?,?,?,?)",QVariantList{p.uid}+args+QVariantList{QDateTime::currentMSecsSinceEpoch()}))return q.lastError().text();
    QString err;
    if(!ActivityAuditService::record(db,"cash","configure_shift",p.reason,0,"cash_desk_policy",p.uid,p.responsible,"{}",&err))return err;
    if(!db.commit())return db.lastError().text();rollback.dismiss();return {};
}
QString bindShift(QSqlDatabase db,const QString& shift)
{
    QString error;
    const auto p=latest(db,error);
    if(!error.isEmpty())return error;
    if(p.uid.isEmpty())return QStringLiteral("Спочатку збережіть відповідального й робочий день у налаштуваннях фінансів.");
    if(auto err=validate(db,p);!err.isEmpty())return err;
    QSqlQuery q(db);
    if(!q.exec("CREATE TABLE IF NOT EXISTS main.cash_shift_policies(shift_uid TEXT PRIMARY KEY NOT NULL REFERENCES cash_shifts(shift_uid),policy_uid TEXT NOT NULL REFERENCES cash_desk_policies(uid))") ||
       !run(q,"INSERT INTO main.cash_shift_policies VALUES(?,?)",{shift,p.uid}))return q.lastError().text();
    return {};
}
}
