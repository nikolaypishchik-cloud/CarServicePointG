#include "CashAccountMovement.h"
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
    if(account=="primary-cash")return CashShiftService::currentBalance(cents);
    const auto tables=db.tables();
    if(!tables.contains("cash_account_moves"))return QStringLiteral("Потрібен підтверджений початковий залишок.");
    QSqlQuery q(db);
    if(!run(q,"SELECT day FROM main.cash_account_moves WHERE src IS NULL AND dest=?",{account}))return q.lastError().text();
    if(!q.next())return QStringLiteral("Потрібен підтверджений початковий залишок.");
    const auto start=q.value(0);
    if(!run(q,"SELECT COALESCE(SUM(CASE WHEN dest=? THEN cents ELSE -cents END),0) FROM main.cash_account_moves WHERE src=? OR dest=?",{account,account,account}) || !q.next())return q.lastError().text();
    cents=q.value(0).toLongLong();
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
