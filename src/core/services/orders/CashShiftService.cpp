#include "CashShiftService.h"
#include "CashBankService.h"
#include "CashDeskPolicy.h"
#include "CashAdvanceService.h"
#include "core/services/AccessControlService.h"
#include "ClientReceiptReader.h"
#include "core/finance/ExactMoney.h"
#include "core/services/ActivityAuditService.h"
#include "infrastructure/db/sqlite/Db.h"
#include <QDateTime>
#include <QScopeGuard>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

namespace CashShiftService {
namespace {
bool run(QSqlQuery& q,const QString& sql,const QVariantList& args={})
{
    if (!q.prepare(sql)) return false;
    for (const auto& value:args) q.addBindValue(value);
    return q.exec();
}
State read(QSqlDatabase db)
{
    State s;
    if (!db.tables().contains("cash_shifts")) return s;
    QSqlQuery q(db);
    if (!q.exec("SELECT shift_uid,state,operator_uid FROM main.cash_shifts ORDER BY rowid DESC LIMIT 1")) {s.error=q.lastError().text();return s;}
    if (!q.next()) {s.error=q.lastError().text();return s;}
    s.uid=q.value(0).toString();s.phase=q.value(1).toString();s.operatorUid=q.value(2).toString();
    if(s.phase=="handover"){
        if (!run(q,"SELECT recipient FROM main.cash_shift_transitions WHERE shift_uid=? AND outcome='handed' ORDER BY rowid DESC LIMIT 1",{s.uid}) || !q.next())
            s.error=QStringLiteral("Не вдалося підтвердити отримувача каси.");
        else s.recipient=q.value(0).toString();
    }
    return s;
}
QString kind(Action value)
{
    switch(value){
    case Action::Freeze: return "freeze";
    case Action::Close: return "close";
    case Action::HandOver: return "handover";
    case Action::Accept: return "accept";
    case Action::OpenNext: return "next";
    case Action::Adjust: return "adjust";
    }
    return {};
}
}
QString balance(QSqlDatabase db,const QString& shift,qint64& amount,bool allowUnlinked)
{
    QSqlQuery q(db);
    if(!run(q,"SELECT opening_cents FROM main.cash_shifts WHERE shift_uid=?",{shift}))return q.lastError().text();
    if(!q.next())return QStringLiteral("Касову зміну не знайдено.");
    std::vector<ExactMoney::Cents> amounts{q.value(0).toLongLong()};
    auto addSum=[&](const QString& sql){
        if(!run(q,sql,{shift}) || !q.next())return false;
        amounts.push_back(q.value(0).toLongLong());return true;
    };
    if(db.tables().contains("cash_advance_events") && !addSum("SELECT COALESCE(SUM(cents),0) FROM main.cash_advance_events WHERE shift_uid=? AND method='cash' AND source_uid IS NULL"))
        return q.lastError().text();
    if(db.tables().contains("client_payments")){
        const QString unmatched=db.tables().contains("cash_shift_receipts")
            ?" AND NOT EXISTS(SELECT 1 FROM main.cash_shift_receipts r WHERE r.payment_uid=p.payment_uid)":"";
        if(!run(q,"SELECT 1 FROM main.client_payments p WHERE p.method='cash' AND p.recorded_ms >= (SELECT opened_ms FROM main.cash_shifts WHERE shift_uid=?)"+unmatched+" LIMIT 1",{shift}))return q.lastError().text();
        if(q.next() && !allowUnlinked)return QStringLiteral("Є готівкова оплата без підтвердженого зв’язку зі зміною. Потрібна звірка.");
    }
    if(db.tables().contains("cash_shift_receipts") && !addSum("SELECT COALESCE(SUM(p.amount_cents),0) FROM main.cash_shift_receipts l JOIN main.client_payments p ON p.payment_uid=l.payment_uid WHERE l.shift_uid=?"))
        return q.lastError().text();
    if(db.tables().contains("cash_shift_transitions") && !addSum("SELECT COALESCE(SUM(delta),0) FROM main.cash_shift_transitions WHERE shift_uid=?"))return q.lastError().text();
    if(db.tables().contains("cash_outflows") && !addSum("SELECT COALESCE(SUM(CASE WHEN reversed_uid IS NULL THEN -cents ELSE cents END),0) FROM main.cash_outflows WHERE shift_uid=?"))return q.lastError().text();
    if(db.tables().contains("cash_account_moves") && !addSum("SELECT COALESCE(SUM(CASE WHEN dest='primary-cash' THEN cents ELSE -cents END),0) FROM main.cash_account_moves WHERE shift_uid=?"))return q.lastError().text();
    if(db.tables().contains("cash_treasury_entries") && !addSum("SELECT COALESCE(SUM(delta),0) FROM main.cash_treasury_entries WHERE shift_uid=?"))return q.lastError().text();
    const auto total=ExactMoney::sum(amounts);
    const auto* balance=std::get_if<ExactMoney::Cents>(&total);
    if(!balance || *balance<0)return QStringLiteral("Некоректний обліковий залишок.");
    amount=*balance;
    return {};
}
QString currentBalance(qint64& amount){return currentBalance(Database::db(),amount);}
QString currentBalance(QSqlDatabase db,qint64& amount)
{
    if(!AccessControlService::currentUserHasPermission(db,"cash.view"))return QStringLiteral("Немає доступу до каси.");
    QSqlQuery q(db);
    if(!q.exec("SAVEPOINT cash_drawer_read"))return q.lastError().text();
    auto cleanup=qScopeGuard([&]{q.exec("ROLLBACK TO cash_drawer_read");q.exec("RELEASE cash_drawer_read");});
    const auto shift=read(db);
    if(!shift.error.isEmpty())return shift.error;
    if(shift.uid.isEmpty())return QStringLiteral("Початковий залишок очікує підтвердження.");
    return balance(db,shift.uid,amount);
}
State current()
{
    if(!AccessControlService::currentUserHasPermission("cash.view")) return {{},{},{},{},QStringLiteral("Немає доступу до каси.")};
    return read(Database::db());
}
QList<QPair<QString,QString>> recipients()
{
    QList<QPair<QString,QString>> result;
    if(!CashAdvanceService::postingAuthorityError("cash.shift").isEmpty())return result;
    QSqlQuery q(Database::db());
    if(run(q,"SELECT id,display_name,role FROM staff_accounts WHERE active=1 AND id<>? ORDER BY display_name,id",
           {ActivityAuditService::currentActor().accountId}))
        while(q.next())if(AccessControlService::hasPermission(Database::db(),q.value(2).toString(),"cash.view") && AccessControlService::hasPermission(Database::db(),q.value(2).toString(),"cash.shift"))result.append({q.value(0).toString(),q.value(1).toString()});
    return result;
}
Result apply(const Command& c)
{
    auto fail=[](QString error){return Result{false,{},error};};
    const auto action=kind(c.action),actor=ActivityAuditService::currentActor().accountId;
    const QString reason=c.reason.trimmed(),recipient=c.recipient.trimmed();
    if (QUuid(c.requestUid).isNull() || c.shiftUid.isEmpty() || action.isEmpty() || c.counted<0 ||
        (c.action==Action::Freeze && c.counted!=0) || ((c.action==Action::HandOver)!=!recipient.isEmpty()))
        return fail(QStringLiteral("Перевірте реквізити запиту зміни."));
    auto db=Database::db();
    QSqlQuery q(db);
    if (!q.exec("BEGIN IMMEDIATE")) return fail(q.lastError().text());
    auto rollback=qScopeGuard([&]{db.rollback();});
    if(auto error=CashAdvanceService::postingAuthorityError(c.action==Action::Adjust?"cash.reconcile":"cash.shift");!error.isEmpty())return fail(error);
    if (!q.exec("CREATE TABLE IF NOT EXISTS main.cash_shift_transitions(request_uid TEXT PRIMARY KEY NOT NULL,shift_uid TEXT NOT NULL,"
                "action TEXT NOT NULL,actor TEXT NOT NULL,recipient TEXT,reason TEXT,counted INTEGER NOT NULL,expected INTEGER NOT NULL,"
                "delta INTEGER NOT NULL,outcome TEXT NOT NULL,phase TEXT NOT NULL,message TEXT NOT NULL,recorded_ms INTEGER NOT NULL)"))
        return fail(q.lastError().text());
    if (!run(q,"SELECT shift_uid,action,actor,recipient,reason,counted,phase,message FROM main.cash_shift_transitions WHERE request_uid=?",{c.requestUid}))
        return fail(q.lastError().text());
    if(q.next()){
        if(q.value(0).toString()!=c.shiftUid || q.value(1).toString()!=action || q.value(2).toString()!=actor ||
           q.value(3).toString()!=recipient || q.value(4).toString()!=reason || q.value(5).toLongLong()!=c.counted)
            return fail(QStringLiteral("Цей запит уже має інші реквізити."));
        return {true,q.value(6).toString(),q.value(7).toString()};
    }
    const auto state=read(db);
    if(!state.error.isEmpty())return fail(state.error);
    if(state.uid!=c.shiftUid)return fail(QStringLiteral("Зміна вже змінилася. Оновіть дані."));
    const bool finishing=c.action==Action::Close || c.action==Action::HandOver;
    const bool starting=c.action==Action::OpenNext || c.action==Action::Accept;
    if(starting){if(auto error=CashBankService::activeAccountError(db,"primary-cash");!error.isEmpty())return fail(error);}
    if ((c.action==Action::Freeze && (state.phase!="open" || actor!=state.operatorUid)) ||
        (finishing && (state.phase!="closing" || actor!=state.operatorUid)) ||
        (c.action==Action::OpenNext && state.phase!="closed") ||
        (c.action==Action::Accept && (state.phase!="handover" || actor!=state.recipient)) ||
        (c.action==Action::Adjust && state.phase=="open"))
        return fail(QStringLiteral("Стан зміни або обліковий запис не дозволяє цю дію."));
    if(c.action==Action::HandOver){
        if(actor==recipient || !run(q,"SELECT role FROM staff_accounts WHERE id=? AND active=1",{recipient}) || !q.next() || !AccessControlService::hasPermission(db,q.value(0).toString(),"cash.view") || !AccessControlService::hasPermission(db,q.value(0).toString(),"cash.shift"))
            return fail(QStringLiteral("Оберіть іншого чинного отримувача каси."));
    }
    qint64 expected=0;
    if(const auto error=balance(db,state.uid,expected,c.action==Action::Freeze);!error.isEmpty())return fail(error);
    const qint64 difference=c.counted-expected;
    if(!run(q,"SELECT outcome,counted FROM main.cash_shift_transitions WHERE shift_uid=? ORDER BY rowid DESC LIMIT 1",{state.uid}))
        return fail(q.lastError().text());
    const bool pending=q.next() && q.value(0).toString()=="discrepancy";
    if(c.action==Action::Adjust && (!pending || reason.isEmpty() || c.counted!=q.value(1).toLongLong()))
        return fail(QStringLiteral("Потрібна зафіксована розбіжність і причина документованого коригування."));
    q.finish();
    const bool mismatch=(starting || finishing) && difference!=0;
    if((mismatch || (pending && (starting || finishing))) && reason.isEmpty())
        return fail(QStringLiteral("Обліково %1, фактично %2, різниця %3. Вкажіть причину.")
            .arg(ClientReceiptReader::money(expected),ClientReceiptReader::money(c.counted),ClientReceiptReader::money(difference)));
    QString phase=state.phase,outcome,message;
    qint64 delta=0;
    if(mismatch){outcome="discrepancy";message=QStringLiteral("Розбіжність зафіксовано. Обліково %1, фактично %2, різниця %3. Готівкові операції заблоковано.")
        .arg(ClientReceiptReader::money(expected),ClientReceiptReader::money(c.counted),ClientReceiptReader::money(difference));}
    else if(c.action==Action::Adjust){outcome="adjusted";delta=difference;message=QStringLiteral("Коригування записано. Потрібен повторний перерахунок.");}
    else if(c.action==Action::Freeze){phase="closing";outcome="frozen";message=QStringLiteral("Операції з готівкою призупинено. Перерахуйте касу.");}
    else if(c.action==Action::HandOver){phase="handover";outcome="handed";message=QStringLiteral("Касу здано. Очікується підтвердження обраного отримувача.");}
    else if(c.action==Action::Close){phase="closed";outcome="closed";message=QStringLiteral("Зміну закрито.");}
    else {phase="closed";outcome="carried";message=QStringLiteral("Наступну зміну відкрито. Перенесено %1.").arg(ClientReceiptReader::money(expected));}
    const auto now=QDateTime::currentMSecsSinceEpoch();
    if(!run(q,"UPDATE main.cash_shifts SET state=? WHERE shift_uid=? AND state=?",{phase,state.uid,state.phase}) || q.numRowsAffected()!=1)
        return fail(QStringLiteral("Не вдалося зафіксувати стан зміни."));
    if(outcome=="carried" && !run(q,"INSERT INTO main.cash_shifts(shift_uid,state,operator_uid,opened_ms,opening_cents) VALUES(?,'open',?,?,?)",
        {c.requestUid,actor,now,expected})) return fail(q.lastError().text());
    if(outcome=="carried"){if(auto err=CashDeskPolicy::bindShift(db,c.requestUid);!err.isEmpty())return fail(err);}
    const QString resultPhase=outcome=="carried"?QStringLiteral("open"):phase;
    if(!run(q,"INSERT INTO main.cash_shift_transitions VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?)",
        {c.requestUid,state.uid,action,actor,recipient,reason,c.counted,expected,delta,outcome,resultPhase,message,now}))
        return fail(q.lastError().text());
    QString error;
    if(!ActivityAuditService::record(db,"cash",action,QStringLiteral("Касова зміна"),0,"cash_shift",state.uid,c.requestUid,"{}",&error))
        return fail(error);
    if(!db.commit())return fail(db.lastError().text());
    rollback.dismiss();
    return {true,resultPhase,message};
}
}
