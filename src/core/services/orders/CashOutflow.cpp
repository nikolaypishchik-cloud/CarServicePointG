#include "CashOutflow.h"
#include "CashAdvanceService.h"
#include "CashShiftService.h"
#include "core/services/AccessControlService.h"
#include "core/services/ActivityAuditService.h"
#include "infrastructure/db/sqlite/Db.h"
#include <QDateTime>
#include <QScopeGuard>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

namespace CashOutflow {
namespace {
bool execute(QSqlQuery& q,const QString& sql,const QVariantList& args={})
{
    if(!q.prepare(sql))return false;
    for(const auto& item:args)q.addBindValue(item);
    return q.exec();
}
}
bool allowed(bool expense)
{
    const auto db=Database::db();
    return AccessControlService::currentUserHasPermission(db,"cash.view")
        && AccessControlService::currentUserHasPermission(db,expense?"cash.expense":"cash.refund");
}
bool canReverse()
{
    const auto db=Database::db();
    return AccessControlService::currentUserHasPermission(db,"cash.view") && AccessControlService::currentUserHasPermission(db,"cash.reverse");
}
QList<Entry> ledger(QDate from,QDate to,QString& error)
{
    auto db=Database::db();
    if(!AccessControlService::currentUserHasPermission(db,"cash.view")){error=QStringLiteral("Немає доступу до каси.");return {};}
    if(!db.tables().contains("cash_outflows"))return {};
    QSqlQuery q(db);
    if(!execute(q,"SELECT uid,kind,COALESCE(reversed_uid,source),CASE WHEN kind='expense' THEN category || ' · ' || reason ELSE reason END,method,actor,day,cents FROM main.cash_outflows WHERE day BETWEEN ? AND ? ORDER BY day DESC,rowid DESC",
                {from.toString(Qt::ISODate),to.toString(Qt::ISODate)})){error=q.lastError().text();return {};}
    QList<Entry> entries;
    while(q.next())entries.append({q.value(0).toString(),q.value(1).toString(),q.value(2).toString(),q.value(3).toString(),
        q.value(4).toString(),q.value(5).toString(),q.value(6).toString(),q.value(7).toLongLong()});
    error=q.lastError().text();
    return entries;
}
Result post(const Command& c)
{
    const bool expense=c.kind=="expense";
    const bool reverse=c.kind=="reverse";
    if(!expense && !reverse && c.kind!="payment" && c.kind!="advance")return {QStringLiteral("Невідомий тип операції.")};
    const QString reason=c.reason.trimmed(),category=c.category.trimmed();
    if(expense==category.isEmpty() || category.size()>300)return {QStringLiteral("Перевірте статтю господарської витрати.")};
    if(QUuid(c.uid).isNull() || reason.isEmpty() || reason.size()>300 || c.cents<=0 || !c.date.isValid() || expense!=c.source.isEmpty())
        return {QStringLiteral("Перевірте джерело, суму, дату й причину операції.")};
    auto db=Database::db();
    QSqlQuery q(db);
    if(!q.exec("BEGIN IMMEDIATE"))return {q.lastError().text()};
    auto rollback=qScopeGuard([&]{db.rollback();});
    if(reverse?!canReverse():!allowed(expense))return {QStringLiteral("Недостатньо прав для цієї операції.")};
    if(const auto error=CashAdvanceService::stationError();!error.isEmpty())return {error};
    const auto actor=ActivityAuditService::currentActor().accountId;
    const auto day=c.date.toString(Qt::ISODate);
    if(!q.exec("CREATE TABLE IF NOT EXISTS main.cash_outflows(uid TEXT PRIMARY KEY NOT NULL,kind TEXT NOT NULL,source TEXT,"
               "cents INTEGER NOT NULL CHECK(cents>0),day TEXT NOT NULL,reason TEXT NOT NULL,method TEXT NOT NULL,actor TEXT NOT NULL,"
               "order_uid TEXT,client_uid TEXT,shift_uid TEXT,recorded_ms INTEGER NOT NULL,category TEXT,reversed_uid TEXT UNIQUE REFERENCES cash_outflows(uid))"))return {q.lastError().text()};
    if(!q.exec("CREATE INDEX IF NOT EXISTS main.cash_outflows_source ON cash_outflows(kind,source)"))return {q.lastError().text()};
    if(!execute(q,"SELECT CASE WHEN reversed_uid IS NULL THEN kind ELSE 'reverse' END,COALESCE(reversed_uid,source),cents,day,reason,actor,category FROM main.cash_outflows WHERE uid=?",{c.uid}))return {q.lastError().text()};
    if(q.next()){
        if(q.value(0)!=c.kind || q.value(1).toString()!=c.source || q.value(2).toLongLong()!=c.cents ||
           q.value(3)!=day || q.value(4)!=reason || q.value(5)!=actor || q.value(6).toString()!=category)return {QStringLiteral("Цей запит уже має інші реквізити.")};
        return {{},true};
    }
    if(c.date!=QDate::currentDate())return {QStringLiteral("Нова операція потребує сьогоднішньої дати.")};
    QString method="cash",order,client,shift;
    QString kind=c.kind,source=c.source,reversed;
    if(reverse){
        if(!execute(q,"SELECT kind,source,cents,method,order_uid,client_uid FROM main.cash_outflows WHERE uid=? AND kind IN ('payment','advance')",{c.source}) || !q.next())return {QStringLiteral("Оберіть первинне повернення.")};
        if(q.value(2).toLongLong()!=c.cents)return {QStringLiteral("Сторно доступне тільки на повну суму повернення.")};
        kind="undo_"+q.value(0).toString();source=q.value(1).toString();method=q.value(3).toString();order=q.value(4).toString();client=q.value(5).toString();reversed=c.source;
        if(!execute(q,"SELECT 1 FROM main.cash_outflows WHERE reversed_uid=?",{reversed}))return {q.lastError().text()};
        if(q.next())return {QStringLiteral("Повернення вже сторновано.")};
    }else if(!expense){
        const auto sql=c.kind=="advance"?
            "SELECT method,day,order_uid,client_uid,cents-COALESCE((SELECT SUM(cents) FROM main.cash_advance_events b WHERE b.source_uid=a.uid),0) "
            "FROM main.cash_advance_events a WHERE uid=? AND source_uid IS NULL":
            "SELECT p.method,p.paid_on,p.order_uid,o.client_uid,p.amount_cents FROM main.client_payments p JOIN main.work_orders o ON o.order_uid=p.order_uid "
            "WHERE p.payment_uid=? AND p.method<>'advance_allocation'";
        if(!execute(q,sql,{c.source}))return {q.lastError().text()};
        if(!q.next())return {QStringLiteral("Відсутнє первинне надходження.")};
        method=q.value(0).toString(); const auto received=QDate::fromString(q.value(1).toString(),Qt::ISODate);
        order=q.value(2).toString();client=q.value(3).toString();
        const auto available=q.value(4).toLongLong();
        if(q.next() || client.isEmpty() || !received.isValid() || received>c.date || !QStringList{"cash","card","bank_transfer","transfer"}.contains(method))
            return {QStringLiteral("Потрібна звірка клієнта, дати або способу надходження.")};
        if(!execute(q,"SELECT COALESCE(SUM(CASE WHEN reversed_uid IS NULL THEN cents ELSE -cents END),0) FROM main.cash_outflows WHERE kind IN (?,?) AND source=?",{c.kind,"undo_"+c.kind,c.source}) || !q.next())return {q.lastError().text()};
        if(c.cents>available-q.value(0).toLongLong())return {QStringLiteral("Сума перевищує доступний залишок джерела.")};
    }
    if(method=="cash"){
        if(!db.tables().contains("cash_shifts"))return {QStringLiteral("Потрібна відкрита касова зміна.")};
        if(!execute(q,"SELECT shift_uid FROM main.cash_shifts WHERE state='open' AND operator_uid=?",{actor}))return {q.lastError().text()};
        if(!q.next())return {QStringLiteral("Потрібна відкрита зміна цього оператора.")};
        shift=q.value(0).toString();
        if(q.next())return {QStringLiteral("Потрібна звірка відкритих змін.")};
        qint64 balance=0;
        if(auto error=CashShiftService::balance(db,shift,balance);!error.isEmpty())return {error};
        if(!reverse && balance<c.cents)return {QStringLiteral("У касі недостатньо готівки.")};
    }
    if(!execute(q,"INSERT INTO main.cash_outflows(uid,kind,source,cents,day,reason,method,actor,order_uid,client_uid,shift_uid,recorded_ms,category,reversed_uid)"
        "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?)",{c.uid,kind,source,c.cents,day,reason,method,actor,order,client,shift,QDateTime::currentMSecsSinceEpoch(),category,reversed}))
        return {q.lastError().text()};
    QString error;
    if(!ActivityAuditService::record(db,"cash",reverse?"reverse_refund":expense?"expense":"refund",reason,0,"cash_outflow",c.uid,c.source,"{}",&error))return {error};
    if(!db.commit())return {db.lastError().text()};
    rollback.dismiss();
    return {};
}
}
