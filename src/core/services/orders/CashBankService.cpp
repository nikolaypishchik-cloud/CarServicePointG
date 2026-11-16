#include "CashBankService.h"
#include "CashAdvanceService.h"
#include "CashAccountMovement.h"
#include "core/services/AccessControlService.h"
#include "core/services/ActivityAuditService.h"
#include "core/finance/ExactMoney.h"
#include "infrastructure/db/sqlite/Db.h"
#include <QDateTime>
#include <QScopeGuard>
#include <QSqlQuery>
#include <QSqlError>
#include <QUuid>

namespace CashBankService {
namespace {
bool run(QSqlQuery& q,const QString& sql,const QVariantList& args={})
{
    if(!q.prepare(sql))return false;
    for(const auto& arg:args)q.addBindValue(arg);
    return q.exec();
}
QString schema(QSqlQuery& q)
{
    for(const auto& sql:{
        "CREATE TABLE IF NOT EXISTS main.cash_accounts(uid TEXT PRIMARY KEY NOT NULL,name TEXT NOT NULL UNIQUE,type TEXT NOT NULL)",
        "INSERT OR IGNORE INTO main.cash_accounts(uid,name,type) VALUES('primary-cash','Каса','cash')",
        "CREATE TABLE IF NOT EXISTS main.cash_bank_events(uid TEXT PRIMARY KEY NOT NULL,action TEXT NOT NULL,account TEXT NOT NULL,reference TEXT NOT NULL,source_type TEXT,source TEXT,net INTEGER NOT NULL,fee INTEGER NOT NULL,matched INTEGER NOT NULL,day TEXT NOT NULL,reason TEXT NOT NULL,actor TEXT NOT NULL,recorded_ms INTEGER NOT NULL)",
        "CREATE UNIQUE INDEX IF NOT EXISTS main.cash_bank_credit_ref ON cash_bank_events(account,reference) WHERE action='credit'",
        "CREATE INDEX IF NOT EXISTS main.cash_bank_source ON cash_bank_events(source_type,source) WHERE action='match'"}){
        if(!q.exec(QString::fromUtf8(sql)))return q.lastError().text();
    }
    return {};
}
}
QString activeAccountError(QSqlDatabase db,const QString& account)
{
    if(!db.tables().contains("cash_bank_events"))return {};
    QSqlQuery q(db);
    if(!run(q,"SELECT action FROM main.cash_bank_events WHERE account=? AND action IN ('block','activate','archive') ORDER BY rowid DESC LIMIT 1",{account}))
        return q.lastError().text();
    if(q.next() && q.value(0)!="activate")return QStringLiteral("Рахунок заблокований або архівний.");
    return {};
}
QList<Account> accounts(QString& error,bool includeInactive)
{
    auto db=Database::db();
    if(!AccessControlService::currentUserHasPermission(db,"cash.view")){error=QStringLiteral("Немає доступу.");return {};}
    if(!db.tables().contains("cash_accounts"))return {{"primary-cash",QStringLiteral("Каса"),"cash","activate"}};
    QSqlQuery q(db);
    const auto state=db.tables().contains("cash_bank_events") ? QStringLiteral("COALESCE((SELECT action FROM main.cash_bank_events e WHERE e.account=a.uid AND e.action IN ('activate','archive','block') ORDER BY rowid DESC LIMIT 1),'activate')") : QStringLiteral("'activate'");
    if(!q.exec("SELECT uid,name,type,"+state+" FROM main.cash_accounts a ORDER BY name")){error=q.lastError().text();return {};}
    QList<Account> items;
    while(q.next())if(includeInactive || q.value(3)=="activate")items.append({q.value(0).toString(),q.value(1).toString(),q.value(2).toString(),q.value(3).toString()});
    error=q.lastError().text();return items;
}
QList<Credit> credits(QString& error)
{
    auto db=Database::db();
    if(!AccessControlService::currentUserHasPermission(db,"cash.view")){error=QStringLiteral("Немає доступу.");return {};}
    if(!db.tables().contains("cash_bank_events"))return {};
    QSqlQuery q(db);
    if(!q.exec("SELECT b.uid,b.account,a.name,b.reference,b.day,b.net,b.fee,b.net+b.fee-COALESCE((SELECT SUM(m.matched) FROM main.cash_bank_events m WHERE m.action='match' AND m.account=b.account AND m.reference=b.reference),0) "
               "FROM main.cash_bank_events b JOIN main.cash_accounts a ON a.uid=b.account WHERE b.action='credit' ORDER BY b.day DESC,b.rowid DESC")){error=q.lastError().text();return {};}
    QList<Credit> items;
    while(q.next())items.append({q.value(0).toString(),q.value(1).toString(),q.value(2).toString(),q.value(3).toString(),q.value(4).toString(),q.value(5).toLongLong(),q.value(6).toLongLong(),q.value(7).toLongLong()});
    error=q.lastError().text();return items;
}
QList<Pending> pending(QString& error)
{
    auto db=Database::db();
    if(!AccessControlService::currentUserHasPermission(db,"cash.view")){error=QStringLiteral("Немає доступу.");return {};}
    const auto tables=db.tables();
    QStringList sources;
    if(tables.contains("client_payments"))sources<<"SELECT payment_uid AS uid,'payment' AS type,paid_on AS day,amount_cents AS cents FROM main.client_payments WHERE method='card'";
    if(tables.contains("cash_advance_events"))sources<<"SELECT uid,'advance' AS type,day,cents FROM main.cash_advance_events WHERE source_uid IS NULL AND method='card'";
    if(sources.isEmpty())return {};
    const QString valid=tables.contains("cash_outflows")?" WHERE NOT EXISTS(SELECT 1 FROM main.cash_outflows v WHERE v.source=s.uid AND v.kind='void_'||s.type)":"";
    const QString matched=tables.contains("cash_bank_events")?"COALESCE((SELECT SUM(matched) FROM main.cash_bank_events m WHERE m.action='match' AND m.source_type=s.type AND m.source=s.uid),0)":"0";
    QSqlQuery q(db);
    if(!q.exec("WITH source_rows AS ("+sources.join(" UNION ALL ")+"), pending AS (SELECT uid,type,day,cents-"+matched+" AS remaining FROM source_rows s"+valid+") SELECT * FROM pending WHERE remaining>0 ORDER BY day,uid")){error=q.lastError().text();return {};}
    QList<Pending> result;
    while(q.next())result.append({q.value(0).toString(),q.value(1).toString(),q.value(2).toString(),q.value(3).toLongLong()});
    error=q.lastError().text();return result;
}
Result post(const Command& c)
{
    const bool lifecycle=QStringList{"block","activate","archive"}.contains(c.action);
    const bool configure=c.action=="account" || c.action=="rename" || lifecycle;
    if(!configure && c.action!="credit" && c.action!="match")return {QStringLiteral("Невідома дія.")};
    const auto reason=c.reason.trimmed(),reference=c.reference.trimmed();
    if(QUuid(c.uid).isNull() || c.account.isEmpty() || reference.isEmpty() || reason.isEmpty() || reason.size()>300 || !c.date.isValid() || c.date>QDate::currentDate() || c.net<0 || c.fee<0 || c.matched<0)
        return {QStringLiteral("Перевірте реквізити й причину.")};
    auto db=Database::db();QSqlQuery q(db);
    if(!q.exec("BEGIN IMMEDIATE"))return {q.lastError().text()};
    auto rollback=qScopeGuard([&]{db.rollback();});
    if(auto error=CashAdvanceService::postingAuthorityError(configure?"cash.configure":"cash.bank");!error.isEmpty())return {error};
    if(auto error=schema(q);!error.isEmpty())return {error};
    const auto actor=ActivityAuditService::currentActor().accountId;
    const auto day=c.date.toString(Qt::ISODate);
    const QVariantList args{c.action,c.account,reference,c.sourceType,c.source,c.net,c.fee,c.matched,day,reason,actor};
    if(!run(q,"SELECT action,account,reference,source_type,source,net,fee,matched,day,reason,actor FROM main.cash_bank_events WHERE uid=?",{c.uid}))return {q.lastError().text()};
    if(q.next()){
        for(int i=0;i<args.size();++i)if(q.value(i).toString()!=args[i].toString())return {QStringLiteral("Запит уже має інші реквізити.")};
        return {{},true};
    }
    if(configure){
        if(c.net || c.fee || c.matched || !c.source.isEmpty() || reference.size()>100)return {QStringLiteral("Перевірте назву рахунку.")};
        if(c.action=="account"){
            if(QUuid(c.account).isNull() || (c.sourceType!="bank" && c.sourceType!="safe"))return {QStringLiteral("Перевірте тип та UID рахунку.")};
            if(!run(q,"INSERT INTO main.cash_accounts(uid,name,type) VALUES(?,?,?)",{c.account,reference,c.sourceType}))return {q.lastError().text()};
        }else if(lifecycle){
            if(!run(q,"SELECT type FROM main.cash_accounts WHERE uid=?",{c.account}) || !q.next())return {QStringLiteral("Рахунок не знайдено.")};
            if(!run(q,"SELECT action FROM main.cash_bank_events WHERE account=? AND action IN ('activate','block','archive') ORDER BY rowid DESC LIMIT 1",{c.account}))return {q.lastError().text()};
            const auto previous=q.next()?q.value(0).toString():QStringLiteral("activate");
            if((previous=="archive" && c.action!="activate") || previous==c.action)return {QStringLiteral("Перевірте поточний стан рахунку.")};
            if(c.action!="activate" && c.account=="primary-cash" && db.tables().contains("cash_shifts")){
                if(!q.exec("SELECT 1 FROM main.cash_shifts WHERE state<>'closed' LIMIT 1"))return {q.lastError().text()};
                if(q.next())return {QStringLiteral("Спочатку завершіть відкриту зміну або передачу каси.")};
            }
            if(c.action=="archive"){
                qint64 funds=0;
                if(auto error=CashAccountMovement::balance(db,c.account,funds);!error.isEmpty())return {error};
                if(funds)return {QStringLiteral("Архівація потребує нульового підтвердженого залишку.")};
                if(!run(q,"SELECT 1 FROM main.cash_bank_events e WHERE e.account=? AND e.action='credit' AND e.net+e.fee<>COALESCE((SELECT SUM(m.matched) FROM main.cash_bank_events m WHERE m.action='match' AND m.account=e.account AND m.reference=e.reference),0) LIMIT 1",{c.account}))return {q.lastError().text()};
                if(q.next())return {QStringLiteral("Завершіть звіряння банківських рядків.")};
            }
        }else if(!run(q,"UPDATE main.cash_accounts SET name=? WHERE uid=?",{reference,c.account}) || q.numRowsAffected()!=1)return {QStringLiteral("Не вдалося перейменувати рахунок.")};
    }else{
        if(auto error=activeAccountError(db,c.account);!error.isEmpty())return {error};
        if(!run(q,"SELECT type FROM main.cash_accounts WHERE uid=?",{c.account}) || !q.next() || q.value(0)!="bank")return {QStringLiteral("Оберіть налаштований банківський рахунок.")};
        if(c.action=="credit"){
            if(c.matched || !c.source.isEmpty() || !c.sourceType.isEmpty() || (c.net==0 && c.fee==0) || !std::holds_alternative<ExactMoney::Cents>(ExactMoney::sum({c.net,c.fee})))
                return {QStringLiteral("Перевірте фактичне зарахування й комісію.")};
        }else{
            if(c.net || c.fee || c.matched<=0 || (c.sourceType!="advance" && c.sourceType!="payment"))return {QStringLiteral("Оберіть первинну карткову оплату і суму звірки.")};
            if(db.tables().contains("cash_outflows")){
                if(!run(q,"SELECT 1 FROM main.cash_outflows WHERE source=? AND kind=?",{c.source,"void_"+c.sourceType}))return {q.lastError().text()};
                if(q.next())return {QStringLiteral("Оплату сторновано.")};
            }
            const QString sql=c.sourceType=="advance"?"SELECT cents,day FROM main.cash_advance_events WHERE uid=? AND source_uid IS NULL AND method='card'":
                "SELECT amount_cents,paid_on FROM main.client_payments WHERE payment_uid=? AND method='card'";
            if(!run(q,sql,{c.source}) || !q.next())return {QStringLiteral("Карткове надходження не знайдено.")};
            const auto gross=q.value(0).toLongLong();const auto paid=QDate::fromString(q.value(1).toString(),Qt::ISODate);
            if(!run(q,"SELECT net+fee,day FROM main.cash_bank_events WHERE account=? AND reference=? AND action='credit'",{c.account,reference}) || !q.next())return {QStringLiteral("Банківський рядок не знайдено.")};
            const auto bankGross=q.value(0).toLongLong();
            const auto bankDay=QDate::fromString(q.value(1).toString(),Qt::ISODate);
            if(!paid.isValid() || paid>bankDay || bankDay>c.date)return {QStringLiteral("Перевірте дати оплати, банку та звірки.")};
            if(!run(q,"SELECT COALESCE(SUM(matched),0) FROM main.cash_bank_events WHERE action='match' AND account=? AND reference=?",{c.account,reference}) || !q.next())return {q.lastError().text()};
            if(c.matched>bankGross-q.value(0).toLongLong())return {QStringLiteral("Сума перевищує незвірений залишок банківського рядка.")};
            if(!run(q,"SELECT COALESCE(SUM(matched),0) FROM main.cash_bank_events WHERE action='match' AND source_type=? AND source=?",{c.sourceType,c.source}) || !q.next())return {q.lastError().text()};
            if(c.matched>gross-q.value(0).toLongLong())return {QStringLiteral("Сума перевищує незвірений залишок карткової оплати.")};
        }
    }

    if(!run(q,"INSERT INTO main.cash_bank_events(action,account,reference,source_type,source,net,fee,matched,day,reason,actor,recorded_ms,uid) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?)",
        args+QVariantList{QDateTime::currentMSecsSinceEpoch(),c.uid}))return {q.lastError().text()};
    QString error;
    if(!ActivityAuditService::record(db,"cash",c.action,reason,0,"cash_bank_event",c.uid,reference,"{}",&error))return {error};
    if(!db.commit())return {db.lastError().text()};
    rollback.dismiss();return {};
}
}
