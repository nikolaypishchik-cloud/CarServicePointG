#include "CashOpeningService.h"
#include "CashAdvanceService.h"
#include "core/services/ActivityAuditService.h"
#include "infrastructure/db/sqlite/Db.h"
#include "ClientReceiptReader.h"
#include <QScopeGuard>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
namespace CashOpeningService {
namespace {
bool execute(QSqlQuery& q,const QString& sql,const QVariantList& args)
{
    if (!q.prepare(sql)) return false;
    for (const auto& value:args) q.addBindValue(value);
    return q.exec();
}
}
Result apply(const Command& c)
{
    auto db=Database::db();
    if (const auto error=CashAdvanceService::postingAuthorityError();!error.isEmpty()) return {error};
    const auto actor=ActivityAuditService::currentActor().accountId;
    if (QUuid(c.uid).isNull() || c.counted<0 || c.counted>9000000000000000LL || !c.start.isValid() || c.start>QDate::currentDate())
        return {QStringLiteral("Перевірте дату початку та фактично перераховану суму.")};
    const auto day=c.start.toString(Qt::ISODate);
    const auto kind=c.action==Action::Initial?"initial":(c.action==Action::Count?"count":"adjust");
    QSqlQuery q(db);
    if (!q.exec("BEGIN IMMEDIATE")) return {q.lastError().text()};
    auto rollback=qScopeGuard([&]{db.rollback();});
    if (!q.exec("CREATE TABLE IF NOT EXISTS main.cash_opening_events(uid TEXT PRIMARY KEY NOT NULL,kind TEXT NOT NULL,"
                "effective_on TEXT NOT NULL,expected INTEGER NOT NULL,counted INTEGER NOT NULL,delta INTEGER NOT NULL,reason TEXT NOT NULL,"
                "actor TEXT NOT NULL,recorded_ms INTEGER NOT NULL,outcome TEXT NOT NULL,source_uid TEXT)"))
        return {q.lastError().text()};
    if (!q.exec("CREATE TABLE IF NOT EXISTS main.cash_shifts(shift_uid TEXT PRIMARY KEY NOT NULL,state TEXT NOT NULL,"
                "operator_uid TEXT NOT NULL,opened_ms INTEGER NOT NULL,opening_cents INTEGER NOT NULL)"))
        return {q.lastError().text()};
    if (!execute(q,"SELECT kind,effective_on,counted,reason,actor,outcome FROM main.cash_opening_events WHERE uid=?",{c.uid}))
        return {q.lastError().text()};
    if (q.next()) {
        if(q.value(0).toString()!=kind || q.value(1).toString()!=day || q.value(2).toLongLong()!=c.counted ||
           q.value(3).toString()!=c.reason.trimmed() || q.value(4).toString()!=actor) return {QStringLiteral("Ключ запиту вже має інші реквізити.")};
        return {QStringLiteral("Вже зафіксовано."),true,q.value(5).toString()=="open"};
    }
    if (!q.exec("SELECT 1 FROM main.cash_shifts LIMIT 1")) return {q.lastError().text()};
    if (q.next()) return {QStringLiteral("Перше відкриття зміни вже проведено.")};
    if (!q.exec("SELECT effective_on FROM main.cash_opening_events WHERE kind='initial'")) return {q.lastError().text()};
    const bool initialized=q.next();
    if (initialized==(c.action==Action::Initial))
        return {initialized?QStringLiteral("Початковий залишок уже підтверджено."):QStringLiteral("Спочатку підтвердьте дату переходу та початковий залишок.")};
    if(initialized && c.start!=QDate::currentDate()) return {QStringLiteral("Перерахунок та коригування виконуються поточною датою.")};
    qint64 expected=0;
    if (initialized) {
        if (!q.exec("SELECT SUM(delta) FROM main.cash_opening_events") || !q.next()) return {q.lastError().text()};
        expected=q.value(0).toLongLong();
    }
    const qint64 diff=c.counted-expected;
    QString source;
    if (!q.exec("SELECT uid,outcome,counted FROM main.cash_opening_events ORDER BY rowid DESC LIMIT 1")) return {q.lastError().text()};
    const bool pending=q.next() && q.value(1).toString()=="discrepancy";
    if (pending) source=q.value(0).toString();
    QString outcome;
    qint64 delta=0;
    if (c.action==Action::Initial) {delta=c.counted;outcome="confirmed";}
    else if (c.action==Action::Adjustment) {
        if (!pending || c.counted!=q.value(2).toLongLong() || c.reason.trimmed().isEmpty())
            return {QStringLiteral("Для коригування потрібні зафіксована розбіжність, перевірена сума та причина.")};
        outcome="adjusted";delta=diff;
    } else {
        if ((diff || pending) && c.reason.trimmed().isEmpty())
            return {QStringLiteral("Обліково %1, перераховано %2, різниця %3. Вкажіть причину.")
                .arg(ClientReceiptReader::money(expected),ClientReceiptReader::money(c.counted),ClientReceiptReader::money(diff))};
        outcome=diff?"discrepancy":"open";
    }
    q.finish();
    const auto now=QDateTime::currentMSecsSinceEpoch();
    if (!execute(q,"INSERT INTO cash_opening_events(uid,kind,effective_on,expected,counted,delta,reason,actor,recorded_ms,outcome,source_uid) "
                 "VALUES(?,?,?,?,?,?,?,?,?,?,?)",{c.uid,kind,day,expected,c.counted,delta,c.reason.isNull()?QStringLiteral(""):c.reason.trimmed(),actor,now,outcome,source}))
        return {q.lastError().text()};
    if (outcome=="open" && !execute(q,"INSERT INTO cash_shifts(shift_uid,state,operator_uid,opened_ms,opening_cents) VALUES(?,'open',?,?,?)",
        {c.uid,actor,now,c.counted})) return {q.lastError().text()};
    QString error;
    if (!ActivityAuditService::record(db,"cash",kind,QStringLiteral("Підготовка першої касової зміни"),0,"cash_opening",c.uid,outcome,"{}",&error))
        return {error};
    if (!db.commit()) return {db.lastError().text()};
    rollback.dismiss();
    if(outcome=="discrepancy") return {QStringLiteral("Зміна закрита. Обліково %1, перераховано %2, різниця %3. Причину збережено.")
        .arg(ClientReceiptReader::money(expected),ClientReceiptReader::money(c.counted),ClientReceiptReader::money(diff)),true,false};
    return {outcome=="open"?QStringLiteral("Готівкову зміну відкрито."):QStringLiteral("Запис підтверджено. Для відкриття потрібен окремий перерахунок."),true,outcome=="open"};
}
}
