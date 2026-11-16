#include "CashTreasury.h"
#include "CashAccountMovement.h"
#include "CashAdvanceService.h"
#include "core/finance/ExactMoney.h"
#include "core/services/AccessControlService.h"
#include "core/services/ActivityAuditService.h"
#include "infrastructure/db/sqlite/Db.h"
#include <QDateTime>
#include <QScopeGuard>
#include <QSqlQuery>
#include <QSqlError>
#include <QUuid>
#include <algorithm>

namespace CashTreasury {
namespace {
bool run(QSqlQuery& query, const QString& sql, const QVariantList& args)
{
    if (!query.prepare(sql)) return false;
    for (const auto& value : args) query.addBindValue(value);
    return query.exec();
}
}
QVector<PurchaseDebt> openDebts(QString& error)
{
    auto db = Database::db();
    if (!AccessControlService::currentUserHasPermission(db,"cash.view") ||
        !AccessControlService::currentUserHasPermission(db,"cash.expense")) {
        error = QStringLiteral("Недостатньо прав для оплати постачальнику.");
        return {};
    }
    return PurchasePaymentService::debts(db, &error);
}
Result post(const Command& c)
{
    const bool supplier = c.kind == "supplier";
    const bool deposit = c.kind == "deposit";
    const QString reason = c.reason.trimmed();
    if ((!deposit && !supplier && c.kind != "withdrawal") || QUuid(c.uid).isNull() ||
        c.account.isEmpty() || c.cents <= 0 || !c.day.isValid() || reason.isEmpty() || reason.size() > 300 ||
        (!supplier && c.account != "primary-cash"))
        return {QStringLiteral("Перевірте операцію, суму, рахунок та підставу.")};
    auto db = Database::db();
    QSqlQuery query(db);
    if (!query.exec("BEGIN IMMEDIATE")) return {query.lastError().text()};
    auto guard = qScopeGuard([&]{ db.rollback(); });
    if (auto error = CashAdvanceService::postingAuthorityError(supplier ? "cash.expense" : "cash.transfer"); !error.isEmpty()) return {error};
    if (supplier && !PurchasePaymentService::canPay(db, c.debt.source)) return {QStringLiteral("Потрібне право зміни оплати придбання.")};
    if (!query.exec("CREATE TABLE IF NOT EXISTS main.cash_treasury_entries("
        "uid TEXT PRIMARY KEY NOT NULL,kind TEXT NOT NULL,account TEXT NOT NULL,delta INTEGER NOT NULL,"
        "day TEXT NOT NULL,reason TEXT NOT NULL,actor TEXT NOT NULL,shift_uid TEXT,source TEXT,order_uid TEXT,recorded_ms INTEGER NOT NULL)"))
        return {query.lastError().text()};
    const auto actor = ActivityAuditService::currentActor().accountId;
    const auto day = c.day.toString(Qt::ISODate);
    const auto source = supplier ? PurchasePaymentService::key(c.debt.source,c.debt.id,c.debt.orderUid) : QString();
    const qint64 delta = deposit ? c.cents : -c.cents;
    if (!run(query,"SELECT kind,account,delta,day,reason,actor,source FROM main.cash_treasury_entries WHERE uid=?",{c.uid})) return {query.lastError().text()};
    if (query.next()) {
        if (query.value(0) != c.kind || query.value(1) != c.account || query.value(2).toLongLong() != delta ||
            query.value(3) != day || query.value(4) != reason || query.value(5) != actor || query.value(6).toString() != source)
            return {QStringLiteral("Реквізити проведеного документа відрізняються.")};
        return {{},true};
    }
    if (c.day != QDate::currentDate()) return {QStringLiteral("Проведіть операцію поточною датою.")};
    QString shift;
    if (c.account == "primary-cash") {
        if (!run(query,"SELECT shift_uid FROM main.cash_shifts WHERE state='open' AND operator_uid=?",{actor}) || !query.next())
            return {QStringLiteral("Потрібна відкрита зміна цього працівника.")};
        shift = query.value(0).toString();
        if (query.next()) return {QStringLiteral("Потрібна звірка відкритих змін.")};
    } else if (!run(query,"SELECT 1 FROM main.cash_accounts WHERE uid=? AND type='bank'",{c.account}) || !query.next()) {
        return {QStringLiteral("Оберіть готівкову касу або банківський рахунок.")};
    }
    qint64 available = 0;
    if (auto error = CashAccountMovement::balance(db,c.account,available); !error.isEmpty()) return {error};
    if (available < -delta || !std::holds_alternative<ExactMoney::Cents>(ExactMoney::sum({available,delta})))
        return {QStringLiteral("Недостатньо коштів або перевищено допустиму суму рахунку.")};
    QString error;
    if (supplier) {
        // Recheck eligible purchasing sources under the same write lock as both ledger entries.
        const auto debts = openDebts(error);
        if (!error.isEmpty()) return {error};
        const auto match = std::find_if(debts.cbegin(),debts.cend(),[&](const PurchaseDebt& debt){
            return debt.id == c.debt.id && debt.source == c.debt.source && debt.orderUid == c.debt.orderUid;
        });
        if (match == debts.cend() || match->supplier != c.debt.supplier) return {QStringLiteral("Джерело боргу змінилося. Оновіть список.")};
        if (!PurchasePaymentService::payInsideCashTransaction(db,c.debt,c.cents,&error))
            return {error.isEmpty() ? QStringLiteral("Оплату придбання відхилено.") : error};
    }
    if (!run(query,"INSERT INTO main.cash_treasury_entries VALUES(?,?,?,?,?,?,?,?,?,?,?)",
        {c.uid,c.kind,c.account,delta,day,reason,actor,shift,source,c.debt.orderUid,QDateTime::currentMSecsSinceEpoch()}))
        return {query.lastError().text()};
    if (!ActivityAuditService::record(db,"cash",c.kind,reason,c.debt.orderNumber,"cash_treasury",c.uid,c.account,"{}",&error)) return {error};
    if (!db.commit()) return {db.lastError().text()};
    guard.dismiss();
    if (supplier) PurchasePaymentService::notifyPaymentCommitted(c.debt);
    return {};
}
}
