#include "CashAccountMovement.h"
#include "CashBankService.h"
#include "CashAdvanceService.h"
#include "ClientReceiptReader.h"
#include "CashShiftService.h"
#include "core/services/ActivityAuditService.h"
#include "core/services/AccessControlService.h"
#include "core/finance/ExactMoney.h"
#include "infrastructure/db/sqlite/Db.h"
#include <QSqlError>
#include <QSqlQuery>
#include <QScopeGuard>
#include <QDateTime>
#include <QUuid>
#include <algorithm>

namespace CashAccountMovement {
namespace {
bool run(QSqlQuery& q,const QString& sql,const QVariantList& args)
{
    if(!q.prepare(sql))return false;
    for(const auto& value:args)q.addBindValue(value);
    return q.exec();
}
}
QString balance(QSqlDatabase db,const QString& account,qint64& cents)
{
    if(!AccessControlService::currentUserHasPermission(db,"cash.view"))return QStringLiteral("Немає доступу.");
    if(account=="primary-cash")return CashShiftService::currentBalance(db,cents);
    const auto tables=db.tables();
    if(!tables.contains("cash_account_moves"))return QStringLiteral("Потрібен підтверджений початковий залишок.");
    QSqlQuery q(db);
    if(!run(q,"SELECT day FROM main.cash_account_moves WHERE src IS NULL AND dest=?",{account}))return q.lastError().text();
    if(!q.next())return QStringLiteral("Потрібен підтверджений початковий залишок.");
    const auto start=q.value(0);
    if(!run(q,"SELECT COALESCE(SUM(CASE WHEN dest=? THEN cents ELSE -cents END),0) FROM main.cash_account_moves WHERE src=? OR dest=?",{account,account,account}) || !q.next())return q.lastError().text();
    cents=q.value(0).toLongLong();
    if(tables.contains("cash_transit_events")){
        if(!run(q,"SELECT COALESCE(SUM(delta),0) FROM main.cash_transit_events WHERE account=?",{account}) || !q.next())return q.lastError().text();
        const auto total=ExactMoney::sum({cents,q.value(0).toLongLong()});
        if(auto sum=std::get_if<ExactMoney::Cents>(&total))cents=*sum;
        else return QStringLiteral("Переповнення суми рахунку.");
    }
    if (tables.contains("cash_treasury_entries")) {
        if (!run(q,"SELECT COALESCE(SUM(delta),0) FROM main.cash_treasury_entries WHERE account=? AND day>=?",{account,start}) || !q.next()) return q.lastError().text();
        const auto value = ExactMoney::sum({cents,q.value(0).toLongLong()});
        if (auto result = std::get_if<ExactMoney::Cents>(&value)) cents = *result;
        else return QStringLiteral("Переповнення суми рахунку.");
    }
    if(tables.contains("cash_bank_events")){
        if(!run(q,"SELECT COALESCE(SUM(net),0) FROM main.cash_bank_events WHERE action='credit' AND account=? AND day>=?",{account,start}) || !q.next())return q.lastError().text();
        const auto value=ExactMoney::sum({cents,q.value(0).toLongLong()});
        if(auto result=std::get_if<ExactMoney::Cents>(&value))cents=*result;
        else return QStringLiteral("Переповнення суми рахунку.");
    }
    return {};
}
QList<QPair<QString,QString>> recipients(QString& error)
{
    auto db=Database::db();
    if(!AccessControlService::currentUserHasPermission(db,"cash.transfer")){error=QStringLiteral("Немає права переміщення.");return {};}
    QSqlQuery q(db);
    if(!q.exec("SELECT id,display_name,role FROM main.staff_accounts WHERE active=1 ORDER BY display_name,id")){error=q.lastError().text();return {};}
    QList<QPair<QString,QString>> result;
    while(q.next())if(AccessControlService::hasPermission(db,q.value(2).toString(),"cash.transfer") &&
                      AccessControlService::hasPermission(db,q.value(2).toString(),"cash.view"))
        result.append({q.value(0).toString(),q.value(1).toString()});
    error=q.lastError().text();return result;
}
QList<QPair<QString,QString>> inTransit(QString& error)
{
    auto db=Database::db();
    if(!AccessControlService::currentUserHasPermission(db,"cash.view")){error=QStringLiteral("Немає доступу.");return {};}
    if(!db.tables().contains("cash_transit_events"))return {};
    QSqlQuery q(db);
    if(!q.exec("SELECT s.uid,a.name,b.name,s.cents,s.recipient FROM main.cash_transit_events s JOIN main.cash_accounts a ON a.uid=s.src JOIN main.cash_accounts b ON b.uid=s.dest "
               "WHERE s.dispatch IS NULL AND NOT EXISTS(SELECT 1 FROM main.cash_transit_events f WHERE f.dispatch=s.uid) ORDER BY s.rowid")){error=q.lastError().text();return {};}
    QList<QPair<QString,QString>> result;
    while(q.next())result.append({q.value(0).toString(),q.value(1).toString()+" → "+q.value(2).toString()+" · "+ClientReceiptReader::money(q.value(3).toLongLong())+" · "+q.value(4).toString()+" · "+q.value(0).toString()});
    error=q.lastError().text();return result;
}
Result physical(const Transfer& c)
{
    const bool send=c.action=="send", receive=c.action=="receive";
    if((!send && !receive && c.action!="return") || QUuid(c.uid).isNull() || c.reason.trimmed().isEmpty() || c.reason.size()>300)
        return {QStringLiteral("Перевірте дію та підставу фізичної передачі.")};
    if(send ? (!c.dispatch.isEmpty() || c.from==c.to || c.cents<=0 || c.recipient.isEmpty()) :
              (QUuid(c.dispatch).isNull() || !c.from.isEmpty() || !c.to.isEmpty() || c.cents || !c.recipient.isEmpty()))
        return {QStringLiteral("Перевірте реквізити передачі.")};
    auto db=Database::db();QSqlQuery q(db);
    if(!q.exec("BEGIN IMMEDIATE"))return {q.lastError().text()};
    auto rollback=qScopeGuard([&]{db.rollback();});
    if(auto err=CashAdvanceService::postingAuthorityError("cash.transfer");!err.isEmpty())return {err};
    if(!q.exec("CREATE TABLE IF NOT EXISTS main.cash_transit_events(uid TEXT PRIMARY KEY NOT NULL,action TEXT NOT NULL,dispatch TEXT UNIQUE REFERENCES cash_transit_events(uid),"
               "src TEXT NOT NULL REFERENCES cash_accounts(uid),dest TEXT NOT NULL REFERENCES cash_accounts(uid),recipient TEXT NOT NULL,cents INTEGER NOT NULL CHECK(cents>0),"
               "account TEXT NOT NULL,delta INTEGER NOT NULL,shift_uid TEXT,day TEXT NOT NULL,actor TEXT NOT NULL,reason TEXT NOT NULL,recorded_ms INTEGER NOT NULL)"))return {q.lastError().text()};
    const auto actor=ActivityAuditService::currentActor().accountId,reason=c.reason.trimmed();
    if(!run(q,"SELECT action,dispatch,src,dest,recipient,cents,actor,reason FROM main.cash_transit_events WHERE uid=?",{c.uid}))return {q.lastError().text()};
    if(q.next()){
        if(q.value(0)!=c.action || q.value(1).toString()!=c.dispatch || q.value(6)!=actor || q.value(7)!=reason ||
            (send && (q.value(2)!=c.from || q.value(3)!=c.to || q.value(4)!=c.recipient || q.value(5).toLongLong()!=c.cents)))
            return {QStringLiteral("Ключ запиту має інші реквізити.")};
        return {{},true};
    }
    QString from=c.from,to=c.to,recipient=c.recipient,shift;
    qint64 cents=c.cents;
    if(send){
        QString error;
        const auto staff=recipients(error);
        if(!error.isEmpty())return {error};
        if(std::none_of(staff.cbegin(),staff.cend(),[&](const auto& item){return item.first==recipient;}))return {QStringLiteral("Оберіть уповноваженого отримувача.")};
        if(auto err=CashBankService::activeAccountError(db,to);!err.isEmpty())return {err};
        qint64 target=0;if(auto err=balance(db,to,target);!err.isEmpty())return {err};
    }else{
        if(!run(q,"SELECT src,dest,recipient,cents,actor FROM main.cash_transit_events WHERE uid=? AND dispatch IS NULL",{c.dispatch}))return {q.lastError().text()};
        if(!q.next())return {QStringLiteral("Відправлення не знайдено.")};
        from=q.value(0).toString();to=q.value(1).toString();recipient=q.value(2).toString();cents=q.value(3).toLongLong();
        if(actor!=(receive?recipient:q.value(4).toString()))return {QStringLiteral("Підтвердження має виконати відповідальний за фактичне отримання.")};
        if(!run(q,"SELECT 1 FROM main.cash_transit_events WHERE dispatch=?",{c.dispatch}))return {q.lastError().text()};
        if(q.next())return {QStringLiteral("Передачу вже завершено.")};
    }
    const auto account=receive?to:from;
    if(auto err=CashBankService::activeAccountError(db,account);!err.isEmpty())return {err};
    qint64 available=0;
    if(auto err=balance(db,account,available);!err.isEmpty())return {err};
    const auto delta=send?-cents:cents;
    if((send && cents>available) || !std::holds_alternative<ExactMoney::Cents>(ExactMoney::sum({available,delta})))return {QStringLiteral("Перевірте фактичний залишок.")};
    if(account=="primary-cash"){
        if(!run(q,"SELECT shift_uid FROM main.cash_shifts WHERE state='open' AND operator_uid=?",{actor}))return {q.lastError().text()};
        if(!q.next())return {QStringLiteral("Потрібна відкрита зміна оператора.")};
        shift=q.value(0).toString();if(q.next())return {QStringLiteral("Потрібна звірка змін.")};
    }
    if(!run(q,"INSERT INTO main.cash_transit_events VALUES(?,?,NULLIF(?,''),?,?,?,?,?,?,?,?,?,?,?)",
        {c.uid,c.action,c.dispatch,from,to,recipient,cents,account,delta,shift,QDate::currentDate().toString(Qt::ISODate),actor,reason,QDateTime::currentMSecsSinceEpoch()}))return {q.lastError().text()};
    QString error;
    if(!ActivityAuditService::record(db,"cash","transfer_"+c.action,reason,0,"cash_transit",c.uid,send?c.uid:c.dispatch,"{}",&error))return {error};
    if(!db.commit())return {db.lastError().text()};
    rollback.dismiss();return {};
}
QString displayBalance(const QString& account)
{
    qint64 cents=0;
    const auto error=balance(Database::db(),account,cents);
    return error.isEmpty()?ClientReceiptReader::money(cents):error;
}
Result post(const Command& c)
{
    const bool opening=c.from.isEmpty();
    if(QUuid(c.uid).isNull() || c.from==c.to || c.to.isEmpty() || c.reason.trimmed().isEmpty() || c.reason.size()>300 || !c.day.isValid() ||
        c.cents<0 || (!opening && c.cents==0) || (opening && c.to=="primary-cash"))return {QStringLiteral("Перевірте рахунки, дату, суму й підставу.")};
    auto db=Database::db();QSqlQuery q(db);
    if(!q.exec("BEGIN IMMEDIATE"))return {q.lastError().text()};
    auto rollback=qScopeGuard([&]{db.rollback();});
    if(auto err=CashAdvanceService::postingAuthorityError(opening?"cash.reconcile":"cash.transfer");!err.isEmpty())return {err};
    if(!q.exec("CREATE TABLE IF NOT EXISTS main.cash_account_moves(uid TEXT PRIMARY KEY NOT NULL,src TEXT REFERENCES cash_accounts(uid),dest TEXT NOT NULL REFERENCES cash_accounts(uid),cents INTEGER NOT NULL CHECK(cents>=0),day TEXT NOT NULL,reason TEXT NOT NULL,actor TEXT NOT NULL,shift_uid TEXT,recorded_ms INTEGER NOT NULL)") ||
       !q.exec("CREATE UNIQUE INDEX IF NOT EXISTS main.cash_account_opening ON cash_account_moves(dest) WHERE src IS NULL"))return {q.lastError().text()};
    const auto actor=ActivityAuditService::currentActor().accountId,day=c.day.toString(Qt::ISODate),reason=c.reason.trimmed();
    if(!run(q,"SELECT src,dest,cents,day,reason,actor FROM main.cash_account_moves WHERE uid=?",{c.uid}))return {q.lastError().text()};
    if(q.next()){
        if(q.value(0).toString()!=c.from || q.value(1)!=c.to || q.value(2).toLongLong()!=c.cents || q.value(3)!=day || q.value(4)!=reason || q.value(5)!=actor)return {QStringLiteral("Реквізити запиту змінилися.")};
        return {{},true};
    }
    if((!opening && c.day!=QDate::currentDate()) || c.day>QDate::currentDate())return {QStringLiteral("Проведіть поточною датою.")};
    if(!db.tables().contains("cash_accounts"))return {QStringLiteral("Спочатку налаштуйте рахунки.")};
    for(const auto& account:{c.from,c.to}){
        if(account.isEmpty())continue;
        if(auto error=CashBankService::activeAccountError(db,account);!error.isEmpty())return {error};
        if(!run(q,"SELECT 1 FROM main.cash_accounts WHERE uid=?",{account}) || !q.next())return {QStringLiteral("Рахунок не знайдено.")};
    }
    QString shift;
    if(!opening){
        qint64 source=0,target=0;
        if(auto err=balance(db,c.from,source);!err.isEmpty())return {err};
        if(auto err=balance(db,c.to,target);!err.isEmpty())return {err};
        if(c.cents>source || !std::holds_alternative<ExactMoney::Cents>(ExactMoney::sum({target,c.cents})))return {QStringLiteral("Перевірте залишки рахунків.")};
        if(c.from=="primary-cash" || c.to=="primary-cash"){
            if(!run(q,"SELECT shift_uid FROM main.cash_shifts WHERE state='open' AND operator_uid=?",{actor}) || !q.next())return {QStringLiteral("Потрібна відкрита зміна цього оператора.")};
            shift=q.value(0).toString();if(q.next())return {QStringLiteral("Потрібна звірка касових змін.")};
        }
    }
    if(!run(q,"INSERT INTO main.cash_account_moves VALUES(?,NULLIF(?,''),?,?,?,?,?,?,?)",{c.uid,c.from,c.to,c.cents,day,reason,actor,shift,QDateTime::currentMSecsSinceEpoch()}))return {q.lastError().text()};
    QString error;
    if(!ActivityAuditService::record(db,"cash",opening?"opening_balance":"transfer",reason,0,"cash_account_move",c.uid,c.to,"{}",&error))return {error};
    if(!db.commit())return {db.lastError().text()};
    rollback.dismiss();return {};
}
}
