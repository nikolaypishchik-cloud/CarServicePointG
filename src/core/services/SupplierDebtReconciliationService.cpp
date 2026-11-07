#include "infrastructure/db/sqlite/Db.h"
#include "core/services/SupplierDebtReconciliationService.h"
#include "core/services/PurchasePaymentService.h"
#include "core/services/PartsQueryService.h"
#include "core/services/ActivityAuditService.h"
#include "core/services/AccessControlService.h"
#include "core/services/settings/FinanceSettingsService.h"
#include "core/util/StoreNameNormalizer.h"
#include "core/backup/BackupCommitPublisher.h"
#include "runtime/RuntimeServices.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QSqlRecord>
#include <QJsonObject>
#include <QJsonDocument>
#include <QUuid>
#include <QSet>
#include <QDateTime>

namespace SupplierDebtReconciliationService {
namespace {
bool fail(QString* error,const QString& value) { if(error) *error=value; return false; }
QString supplierName(const QString& value) {
    return StoreNameNormalizer::canonicalDisplayNameOrDefault(value,QString::fromUtf8("Без магазину"));
}
}
bool canReconcile(QSqlDatabase db) {
    const auto actor=ActivityAuditService::currentActor();
    const QString role=actor.role.trimmed().toLower();
    return actor.isValid() && (role=="owner" || role=="admin" || role=="administrator")
        && AccessControlService::currentUserHasPermission(db,"parts.order");
}
QMap<QString,qint64> effectiveTotals(const QVector<PurchaseDebt>& debts,
        const QMap<QString,SupplierDebtReconciliation>& reconciliations) {
    QMap<QString,qint64> totals;
    for(const auto& debt:debts) totals[supplierName(debt.supplier)]+=debt.remainingCents();
    for(auto it=reconciliations.cbegin();it!=reconciliations.cend();++it) {
        const auto& r=it.value();
        totals[it.key()]=qMax(qint64(0),totals.value(it.key())+r.actualCents-r.computedCents);
    }
    return totals;
}
QMap<QString,SupplierDebtReconciliation> load(QSqlDatabase db,QString* error) {
    QString localError;
    if(!error) error=&localError;
    if(error) error->clear();
    QMap<QString,SupplierDebtReconciliation> result;
    if(AccessControlService::currentActorIsMaster() || !db.tables().contains("supplier_debt_reconciliations")) return result;
    QSqlQuery q(db);
    if(!q.exec("SELECT supplier,revision,computed_cents,actual_cents,checked_at,actor FROM supplier_debt_reconciliations")) {
        fail(error,q.lastError().text()); return {};
    }
    while(q.next()) {
        SupplierDebtReconciliation r;
        r.supplier=q.value(0).toString(); r.revision=q.value(1).toString();
        r.computedCents=q.value(2).toLongLong(); r.actualCents=q.value(3).toLongLong();
        r.checkedAt=q.value(4).toString(); r.actor=q.value(5).toString(); result.insert(r.supplier,r);
    }
    return result;
}
QStringList suppliers(QSqlDatabase db,QString* error) {
    QString localError;
    if(!error) error=&localError;
    if(error) error->clear();
    if(!canReconcile(db)) { fail(error,QString::fromUtf8("Звірка боргів доступна лише адміністратору.")); return {}; }
    QSet<QString> names;
    if (!db.tables().contains("settings")) return {};
    QSqlQuery q(db);
    q.prepare("SELECT key,value FROM settings WHERE key IN (?,?)");
    q.addBindValue("settings.fin.store_markups");
    q.addBindValue("settings.fin.oil_store_markups");
    if (!q.exec()) { fail(error,q.lastError().text()); return {}; }
    while (q.next()) {
        const auto raw = q.value(1).toString();
        if (q.value(0).toString() == "settings.fin.store_markups") {
            const auto object = QJsonDocument::fromJson(raw.toUtf8()).object();
            for (auto it = object.begin(); it != object.end(); ++it)
                if (!it.key().trimmed().isEmpty()) names.insert(supplierName(it.key()));
        } else {
            for (const auto& rule : FinanceSettingsService::loadOilMarkupRules(raw))
                if (!rule.store.isEmpty()) names.insert(supplierName(rule.store));
        }
    }
    QStringList result;
    for(const auto& name:names) {
        const auto key=name.simplified().toCaseFolded();
        if(key.isEmpty() || key==QString::fromUtf8("без магазину") || key==QString::fromUtf8("(без магазину)")
            || key==QString::fromUtf8("нова пошта") || key==QString::fromUtf8("новая почта")
            || key==QString::fromUtf8("масло") || key==QString::fromUtf8("масла") || key==QString::fromUtf8("база"))continue;
        result.append(name);
    }
    result.sort(); return result;
}
bool save(QSqlDatabase db,const QVector<SupplierDebtReconciliation>& entries,QString* error) {
    QString localError;
    if(!error) error=&localError;
    if(error) error->clear();
    if(!canReconcile(db)) return fail(error,QString::fromUtf8("Недостатньо прав для звірки боргів."));
    if(entries.isEmpty()) return true;
    QSqlQuery tx(db);
    if(!tx.exec("SAVEPOINT supplier_debt_reconciliation")) return fail(error,tx.lastError().text());
    auto abort=[&]() { tx.exec("ROLLBACK TO supplier_debt_reconciliation"); tx.exec("RELEASE supplier_debt_reconciliation"); return false; };
    if(!tx.exec("CREATE TABLE IF NOT EXISTS supplier_debt_reconciliations(supplier TEXT PRIMARY KEY,revision TEXT NOT NULL,"
                "computed_cents INTEGER NOT NULL CHECK(computed_cents>=0),actual_cents INTEGER NOT NULL CHECK(actual_cents>=0),"
                "checked_at TEXT NOT NULL,actor TEXT NOT NULL)")) { fail(error,tx.lastError().text()); return abort(); }
    const auto previous=load(db,error);
    if(error && !error->isEmpty()) return abort();
    QMap<QString,qint64> totals;
    PartsQueryService::invalidateCache();
    const auto debts=PurchasePaymentService::debts(db,error);
    if(error && !error->isEmpty()) return abort();
    for(const auto& debt:debts) totals[supplierName(debt.supplier)]+=debt.remainingCents();
    QSet<QString> seen;
    for(const auto& entry:entries) {
        const auto name=supplierName(entry.supplier);
        if(entry.supplier!=name || seen.contains(name) || entry.actualCents<0 || entry.actualCents>900000000000000LL
                || entry.computedCents!=totals.value(name) || entry.revision!=previous.value(name).revision) {
            fail(error,QString::fromUtf8("Борг або звірка магазину змінилися. Відкрийте звірку повторно.")); return abort();
        }
        seen.insert(name);
        const auto actor=ActivityAuditService::currentActor();
        const QString revision=QUuid::createUuid().toString(QUuid::WithoutBraces);
        const QString checked=QDateTime::currentDateTime().toString("dd.MM.yy HH:mm:ss");
        QSqlQuery q(db);
        q.prepare("INSERT INTO supplier_debt_reconciliations VALUES(?,?,?,?,?,?) ON CONFLICT(supplier) DO UPDATE SET "
                  "revision=excluded.revision,computed_cents=excluded.computed_cents,actual_cents=excluded.actual_cents,"
                  "checked_at=excluded.checked_at,actor=excluded.actor");
        q.addBindValue(name); q.addBindValue(revision); q.addBindValue(entry.computedCents); q.addBindValue(entry.actualCents);
        q.addBindValue(checked); q.addBindValue(actor.displayName.isNull()?QStringLiteral(""):actor.displayName);
        if(!q.exec()) { fail(error,q.lastError().text()); return abort(); }
        const QJsonObject details{{"supplier",name},{"computed_cents",entry.computedCents},{"actual_cents",entry.actualCents},
            {"previous_actual_cents",previous.value(name).actualCents},{"revision",revision}};
        if(!ActivityAuditService::record(db,"payments","supplier_debt_reconciliation",QString::fromUtf8("Звірка боргів магазину"),
                0,"supplier",name,name,QString::fromUtf8(QJsonDocument(details).toJson(QJsonDocument::Compact)),error)) return abort();
    }
    if(!tx.exec("RELEASE supplier_debt_reconciliation")) { fail(error,tx.lastError().text()); return abort(); }
    if(RuntimeServices::backupManager()) BackupCommitPublisher::publish(BackupCommitPublisher::makeEvent(
        "payments","supplier_debt_reconciliation","supplier",QString(),QString(),0,"supplier_debt_reconciliation",QString(),
        {"supplier_debt_reconciliations","activity_audit_events"}));
    return true;
}
}

namespace SupplierDebtReconciliationService {
bool canReconcile() { return canReconcile(Database::db()); }
}
