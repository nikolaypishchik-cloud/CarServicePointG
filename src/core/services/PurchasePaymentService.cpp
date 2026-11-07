#include "core/util/DateFormats.h"
#include "core/util/StoreNameNormalizer.h"
#include "core/services/PurchasePaymentService.h"
#include "core/services/InvoiceRegistryService.h"
#include "core/services/SparePaymentAllocation.h"
#include "infrastructure/db/sqlite/Db.h"
#include "core/services/PartsQueryService.h"
#include "core/services/ActivityAuditService.h"
#include "core/services/AccessControlService.h"
#include "core/services/orders/PartMarkupPolicyService.h"
#include "core/cache/SnapshotCacheInvalidator.h"
#include "core/backup/BackupCommitPublisher.h"
#include "runtime/RuntimeServices.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QSqlRecord>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <cmath>
#include <algorithm>

namespace PurchasePaymentService {
OrderPaymentPreparation prepareOrderPayment(int orderNumber, QString* error) {
    return prepareOrderPayment(Database::db(), orderNumber, error);
}
OrderPaymentPreparation prepareOrderPayment(QSqlDatabase db, int orderNumber, QString* error) {
    QString diagnostic;
    OrderPaymentPreparation result;
    result.expectedDebts = orderPartDebts(db, orderNumber, &diagnostic);
    if (diagnostic.isEmpty()) {
        const auto rows = PartsQueryService::loadRows(db, {}, {}, {}, &diagnostic);
        for (const auto& row : rows)
            if (row.orderNumber == orderNumber && !PartMarkupPolicyService::isInventoryStore(row.store)
                && !PartMarkupPolicyService::isCustomerSupplied(row.store))
                result.displayedPurchases << fromPart(row);
    }
    if (error) *error = diagnostic;
    return diagnostic.isEmpty() ? result : OrderPaymentPreparation{};
}
bool canPayInvoice(const RegisteredInvoice& invoice) {
    const bool stock = std::any_of(invoice.parts.cbegin(), invoice.parts.cend(), [](const auto& part) { return part.source != "part"; });
    const auto db = Database::db();
    return !invoice.parts.isEmpty() && !invoice.apportioned && !invoice.amountMismatch && !stock
        && !AccessControlService::currentOrderScope(db).restricted
        && AccessControlService::currentUserHasPermission(db, "nav.reports");
}
bool payOrderParts(int number, const QVector<PurchaseDebt>& expected, QString* error, qint64 amount) {
    return payOrderParts(Database::db(), number, expected, error, amount);
}
bool payInvoice(const RegisteredInvoice& expected, QString* error, qint64 amount) {
    return payInvoice(Database::db(), expected, error, amount);
}
PurchaseDebt detail(const QString& source, qint64 id, const QString& uid, QString* error) {
    return detail(Database::db(), source, id, uid, error);
}
bool canPay(const QString& source) { return canPay(Database::db(), source); }
QVector<PurchasePaymentEntry> history(const PurchaseDebt& debt,QString* error) {
    return history(Database::db(),debt,error);
}
QVector<PurchasePaymentEntry> groupHistory(const QVector<PurchaseDebt>& debts,QString* error) {
    return groupHistory(Database::db(),debts,error);
}
bool pay(const PurchaseDebt& expected, qint64 amount, QString* error, int deferredDays) {
    return pay(Database::db(), expected, amount, error, deferredDays);
}
namespace {
bool fail(QString* error, const QString& message) { if (error) *error = message; return false; }
bool exists(QSqlDatabase db, const QString& name) { return db.tables().contains(name); }
QHash<QString, QDate> terms(QSqlDatabase db, QString* error) {
    QHash<QString, QDate> result;
    if (!exists(db, "purchase_payment_terms")) return result;
    QSqlQuery q(db);
    if (!q.exec("SELECT payment_key,due_date FROM purchase_payment_terms")) { fail(error,q.lastError().text()); return result; }
    while (q.next()) result.insert(q.value(0).toString(), DateFormats::parseDate(q.value(1).toString()));
    if(q.lastError().isValid()){fail(error,q.lastError().text());return {};}
    return result;
}
QHash<QString,qint64> rawBalances(QSqlDatabase db, QString* error) {
    QHash<QString,qint64> result;
    if (!exists(db,"purchase_payment_balances")) return result;
    QSqlQuery q(db);
    if(!q.exec("SELECT payment_key,paid_cents FROM purchase_payment_balances")) {fail(error,q.lastError().text());return {};}
    while(q.next()) result.insert(q.value(0).toString(),q.value(1).toLongLong());
    if(q.lastError().isValid()){fail(error,q.lastError().text());return {};}
    return result;
}
bool reconcileLegacyStockPayments(QSqlDatabase db, QString* error) {
    if(!exists(db,"workshop_spare_movements")) return true;
    QSqlQuery q(db);
    if(!q.exec("SELECT m.id FROM workshop_spare_movements m JOIN work_order_parts p ON p.id=m.part_id AND p.order_number=m.order_number "
               "JOIN purchase_payment_balances b ON b.payment_key='part:'||p.id||':'||COALESCE(p.order_uid,'') "
               "WHERE m.operation='issue' AND b.paid_cents>0 AND NOT EXISTS "
               "(SELECT 1 FROM workshop_spare_payment_allocations a WHERE a.issue_id=m.id)")) return fail(error,q.lastError().text());
    QSet<qint64> legacy;
    while(q.next()) legacy.insert(q.value(0).toLongLong());
    if(legacy.isEmpty()) return true;
    QSqlQuery tx(db);
    if(!tx.exec("SAVEPOINT legacy_stock_payment")) return fail(error,tx.lastError().text());
    const auto abort=[&] {tx.exec("ROLLBACK TO legacy_stock_payment");tx.exec("RELEASE legacy_stock_payment");return false;};
    auto raw=rawBalances(db,error);
    QVector<SparePaymentAllocation::Slice> slices;
    if(!SparePaymentAllocation::load(db,raw,&slices,error)) return abort();
    QHash<qint64,qint64> required;
    for(auto& slice:slices) {
        if(legacy.contains(slice.issueId)) {
            const auto k=key("part",slice.partId,slice.orderUid);
            const auto portion=qMin(raw.value(k),slice.total);
            raw[k]-=portion;slice.paid=qMax(slice.paid,portion);
        }
        required[slice.purchaseId]+=slice.paid;
    }
    // Preserve already recorded payments without creating a second cash event.
    for(auto it=required.cbegin();it!=required.cend();++it) {
        const auto k=key("inventory",it.key());
        q.prepare("INSERT INTO purchase_payment_balances(payment_key,paid_cents) VALUES(?,?) "
                  "ON CONFLICT(payment_key) DO UPDATE SET paid_cents=MAX(paid_cents,excluded.paid_cents)");
        q.addBindValue(k);q.addBindValue(qMax(raw.value(k),it.value()));
        if(!q.exec()) {fail(error,q.lastError().text());return abort();}
    }
    if(!SparePaymentAllocation::save(db,slices,error)) return abort();
    if(!tx.exec("RELEASE legacy_stock_payment")) {fail(error,tx.lastError().text());return abort();}
    PartsQueryService::invalidateCache();
    return true;
}
bool writeBalance(QSqlDatabase db, const PurchaseDebt& debt, qint64 paid, const QDate& dueDate, QString* error, bool linkStock = true) {
    QVector<SparePaymentAllocation::Slice> slices;
    if(linkStock) {
        const auto raw=rawBalances(db,error);
        if((error && !error->isEmpty()) || !SparePaymentAllocation::load(db,raw,&slices,error)) return false;
    }
    if(linkStock && debt.source=="part") {
        qint64 total=0,previousPaid=0;bool linked=false;
        for(const auto& slice:slices) if(slice.partId==debt.id && slice.orderUid==debt.orderUid) {
            linked=true;total+=slice.total;previousPaid+=slice.paid;
        }
        if(linked) {
            if(total!=debt.totalCents || previousPaid!=debt.paidCents)
                return fail(error,QString::fromUtf8("Сума або оплата виданої запчастини не збігається зі складом. Оновіть дані та виконайте звірку."));
            qint64 delta=paid-previousPaid;
            for(auto& slice:slices) if(slice.partId==debt.id && slice.orderUid==debt.orderUid) {
                const qint64 portion=delta>=0?qMin(delta,slice.total-slice.paid):-qMin(-delta,slice.paid);
                if(portion==0) continue;
                auto receipt=detail(db,"inventory",slice.purchaseId,{},error);
                if(!receipt.id || (error && !error->isEmpty())) return false;
                const auto receiptDue=receipt.paidCents+portion>=receipt.totalCents?QDate():receipt.dueDate;
                if(!writeBalance(db,receipt,receipt.paidCents+portion,receiptDue,error,false)) return false;
                slice.paid+=portion;delta-=portion;
            }
            if(delta!=0) return fail(error,QString::fromUtf8("Не вдалося розподілити оплату за надходженнями складу."));
            // Only receipt events represent money; part balances are projections, not a second payment.
            return SparePaymentAllocation::save(db,slices,error);
        }
    }
    QSqlQuery q(db);
    q.prepare("INSERT INTO purchase_payment_balances(payment_key,paid_cents) VALUES(?,?) "
              "ON CONFLICT(payment_key) DO UPDATE SET paid_cents=excluded.paid_cents");
    q.addBindValue(key(debt.source, debt.id, debt.orderUid)); q.addBindValue(paid);
    if (!q.exec()) return fail(error, q.lastError().text());
    q.prepare("INSERT INTO purchase_payment_terms(payment_key,due_date) VALUES(?,?) "
              "ON CONFLICT(payment_key) DO UPDATE SET due_date=excluded.due_date");
    q.addBindValue(key(debt.source,debt.id,debt.orderUid));
    q.addBindValue(dueDate.isValid() ? dueDate.toString(Qt::ISODate) : QStringLiteral(""));
    if (!q.exec()) return fail(error,q.lastError().text());
    q.prepare("INSERT INTO purchase_payment_events(payment_key,amount_cents,actor,due_date) VALUES(?,?,?,?)");
    q.addBindValue(key(debt.source, debt.id, debt.orderUid));
    q.addBindValue(paid - debt.paidCents);
    const QString actor = ActivityAuditService::currentActor().displayName;
    q.addBindValue(actor.isNull() ? QStringLiteral("") : actor);
    q.addBindValue(dueDate.isValid() ? dueDate.toString(Qt::ISODate) : QStringLiteral(""));
    if (!q.exec()) return fail(error, q.lastError().text());
    if (debt.source == "part") {
        q.prepare("UPDATE work_order_parts SET paid=? WHERE id=? AND (COALESCE(order_uid,'')=? "
                  "OR (TRIM(COALESCE(order_uid,''))='' AND order_number=?))");
        q.addBindValue(paid >= debt.totalCents ? 1 : 0); q.addBindValue(debt.id);
        q.addBindValue(debt.orderUid.isNull() ? QStringLiteral("") : debt.orderUid);
        q.addBindValue(debt.orderNumber);
        if (!q.exec()) return fail(error, q.lastError().text());
    }
    if(linkStock && debt.source=="inventory" && !slices.isEmpty()) {
        qint64 allocated=0;
        for(const auto& slice:slices) if(slice.purchaseId==debt.id) allocated+=slice.paid;
        qint64 delta=paid-allocated;
        for(auto& slice:slices) if(slice.purchaseId==debt.id) {
            const auto portion=delta>=0?qMin(delta,slice.total-slice.paid):-qMin(-delta,slice.paid);
            slice.paid+=portion;delta-=portion;
        }
        if(!SparePaymentAllocation::save(db,slices,error)) return false;
    }
    const QJsonObject payload{{"total_cents", debt.totalCents}, {"paid_before_cents", debt.paidCents},
        {"paid_after_cents", paid}, {"remaining_cents", qMax<qint64>(0, debt.totalCents - paid)},
        {"due_date_before",debt.dueDate.toString(Qt::ISODate)}, {"due_date_after",dueDate.toString(Qt::ISODate)}};
    return ActivityAuditService::record(db, "payments", "purchase_payment",
        QString::fromUtf8("Оплата придбання: %1").arg(debt.name), debt.orderNumber, debt.source,
        key(debt.source, debt.id, debt.orderUid), debt.name,
        QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact)), error);
}
bool change(QSqlDatabase db, const PurchaseDebt& expected, qint64 amount, bool reset, QString* error, int deferredDays = -1) {
    if (error) error->clear();
    if (!canPay(db, expected.source)) return fail(error, QString::fromUtf8("Недостатньо прав для зміни оплати."));
    if (deferredDays < -1 || deferredDays > 3650) return fail(error, QString::fromUtf8("Відтермінування має бути від 1 до 3650 днів."));
    if (!ensureSchema(db, error)) return false;
    QSqlQuery tx(db);
    if (!tx.exec("SAVEPOINT purchase_payment")) return fail(error, tx.lastError().text());
    auto abort = [&]() { tx.exec("ROLLBACK TO purchase_payment"); tx.exec("RELEASE purchase_payment"); return false; };
    PartsQueryService::invalidateCache();
    const auto current = detail(db, expected.source, expected.id, expected.orderUid, error);
    if (!current.id || (error && !error->isEmpty())) return abort();
    if (current.totalCents != expected.totalCents || current.paidCents != expected.paidCents || current.dueDate != expected.dueDate) {
        fail(error, QString::fromUtf8("Стан оплати або вартість змінилися. Відкрийте оплату повторно.")); return abort();
    }
    if (!reset && (amount < 0 || amount > current.remainingCents())) {
        fail(error, QString::fromUtf8("Сума платежу має бути від нуля до залишку боргу.")); return abort();
    }
    const auto paid = reset ? 0 : current.paidCents + amount;
    QDate dueDate = deferredDays < 0 ? current.dueDate : (deferredDays == 0 ? QDate() : QDate::currentDate().addDays(deferredDays));
    if (paid >= current.totalCents) dueDate = {};
    if (!writeBalance(db, current, paid, dueDate, error)) return abort();
    if (!tx.exec("RELEASE purchase_payment")) { fail(error, tx.lastError().text()); return abort(); }
    PartsQueryService::invalidateCache();
    SnapshotCacheInvalidator::invalidateOrdersAndParts("purchase_payment");
    if (RuntimeServices::backupManager()) {
        BackupCommitPublisher::publish(BackupCommitPublisher::makeEvent(
            "payments", "purchase_payment", current.source, QString::number(current.id),
            current.orderUid, current.orderNumber, "purchase_payment", current.name,
            {"purchase_payment_balances", "purchase_payment_events", "purchase_payment_terms", "work_order_parts", "activity_audit_events", "workshop_spare_payment_allocations"}));
    }
    return true;
}
}
qint64 cents(double amount) { return std::isfinite(amount) && amount >= 0 && amount < 9e13 ? qRound64(amount * 100) : -1; }
QString key(const QString& source, qint64 id, const QString& uid) { return source + ':' + QString::number(id) + ':' + uid; }
bool ensureSchema(QSqlDatabase db, QString* error) {
    QSqlQuery q(db);
    for (const QString& sql : {
        QStringLiteral("CREATE TABLE IF NOT EXISTS purchase_payment_balances(payment_key TEXT PRIMARY KEY,paid_cents INTEGER NOT NULL CHECK(paid_cents>=0))"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS purchase_payment_terms(payment_key TEXT PRIMARY KEY,due_date TEXT NOT NULL DEFAULT '')"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS returned_inventory_purchases(purchase_id INTEGER PRIMARY KEY,spare_id INTEGER NOT NULL)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS purchase_payment_events(id INTEGER PRIMARY KEY AUTOINCREMENT,payment_key TEXT NOT NULL,amount_cents INTEGER NOT NULL,actor TEXT NOT NULL DEFAULT '',created_at TEXT NOT NULL DEFAULT (datetime('now')) )"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS purchase_payment_events_key ON purchase_payment_events(payment_key,id)")})
        if (!q.exec(sql)) return fail(error, q.lastError().text());
    if(!q.exec("CREATE TABLE IF NOT EXISTS workshop_spare_payment_allocations (issue_id INTEGER NOT NULL,purchase_id INTEGER NOT NULL,paid_cents INTEGER NOT NULL CHECK(paid_cents>=0),PRIMARY KEY(issue_id,purchase_id))"))
        return fail(error,q.lastError().text());
    if (db.record("purchase_payment_events").indexOf("due_date") < 0 &&
        !q.exec("ALTER TABLE purchase_payment_events ADD COLUMN due_date TEXT NOT NULL DEFAULT ''"))
        return fail(error,q.lastError().text());
    return ActivityAuditService::ensureSchema(db, error) && reconcileLegacyStockPayments(db,error);
}
QHash<QString, qint64> balances(QSqlDatabase db, QString* error) {
    auto result=rawBalances(db,error);
    QVector<SparePaymentAllocation::Slice> slices;
    if((error && !error->isEmpty()) || !SparePaymentAllocation::load(db,result,&slices,error)) return {};
    SparePaymentAllocation::project(slices,&result);
    return result;
}
void apply(PartRowSnapshot& row, const QHash<QString, qint64>& values) {
    const auto total = cents(row.purchaseAmount > 0 ? row.purchaseAmount : row.purchasePrice * row.qty);
    row.paidAmount = values.value(key("part", row.id, row.orderUid), row.paid ? qMax<qint64>(0,total) : 0) / 100.0;
    if (values.contains(key("part", row.id, row.orderUid))) row.paid = cents(row.paidAmount) >= total;
}
PurchaseDebt fromPart(const PartRowSnapshot& row) {
    PurchaseDebt d;
    d.source="part"; d.id=row.id; d.orderUid=row.orderUid; d.date=row.dateRaw; d.name=row.name;
    d.supplier=row.store; d.oem=row.article; d.orderNumber=row.orderNumber; d.car=row.plate + " " + row.carModel;
    d.quantity=row.qty;
    d.totalCents=qMax<qint64>(0,cents(row.purchaseAmount > 0 ? row.purchaseAmount : row.purchasePrice * row.qty));
    d.paidCents=row.paid ? qMax(d.totalCents,cents(row.paidAmount)) : qMax<qint64>(0,cents(row.paidAmount));
    return d;
}
int unpaidDays(const PurchaseDebt& debt, const PartRowSnapshot* part, const QDate& today) {
    if (debt.remainingCents()<=0) return 0;
    if (!today.isValid()) return -1;
    const QDate ordered=PartsQueryService::parseBusinessDate(part ? part->dateRaw : debt.date);
    if (!ordered.isValid()) return -1;
    return qMax(0,int(ordered.daysTo(today)));
}
PurchaseDebt detail(QSqlDatabase db, const QString& source, qint64 id, const QString& uid, QString* error) {
    if(error) error->clear();
    QString localError;
    PurchaseDebt d;
    if (source == "part") {
        const auto rows=PartsQueryService::loadRows(db, {}, {}, {}, &localError);
        if(!localError.isEmpty()){fail(error,localError);return {};}
        for (const auto& row: rows) if (row.id == id && row.orderUid == uid) { d=fromPart(row); break; }
        if (d.id && exists(db, "work_order_part_attachments")) {
            QSqlQuery q(db); q.prepare("SELECT invoice_number FROM work_order_part_attachments WHERE part_id=?"); q.addBindValue(id);
            if(!q.exec()){fail(error,q.lastError().text());return {};}
            if(q.next()) d.invoice=q.value(0).toString();
            if(q.lastError().isValid()){fail(error,q.lastError().text());return {};}
        }
    } else if (source == "inventory") {
        QSqlQuery q(db);
        QString sql="SELECT purchased_on,material_name,quantity,unit_cost,supplier,note,unit FROM workshop_inventory_purchases WHERE id=?";
        if(exists(db,"returned_inventory_purchases"))
            sql+=" AND id NOT IN (SELECT purchase_id FROM returned_inventory_purchases)";
        q.prepare(sql);
        q.addBindValue(id);
        if (!q.exec()) { fail(error,q.lastError().text()); return {}; }
        if (q.next()) {
            d.source=source; d.id=id; d.date=q.value(0).toString(); d.name=q.value(1).toString();
            d.quantity=q.value(2).toDouble(); d.totalCents=qMax<qint64>(0,cents(d.quantity*q.value(3).toDouble()));
            d.supplier=q.value(4).toString(); d.note=q.value(5).toString();
            d.unit=q.value(6).toString();
            d.paidCents=balances(db,&localError).value(key(source,id), d.totalCents);
        }
        if(q.lastError().isValid())localError=q.lastError().text();
    }
    if(!localError.isEmpty()){fail(error,localError);return {};}
    if (d.id) {
        d.dueDate=terms(db,&localError).value(key(d.source,d.id,d.orderUid));
        if(!localError.isEmpty()){fail(error,localError);return {};}
    }
    else fail(error,QString::fromUtf8("Придбання не знайдено."));
    return d;
}
QVector<PurchaseDebt> debts(QSqlDatabase db, QString* error) {
    if(error)error->clear();
    QString localError;
    QVector<PurchaseDebt> result;
    // Receipt debt already includes issued stock. Determine provenance from
    // movements, not the editable supplier label on the order line.
    QVector<SparePaymentAllocation::Distribution> distribution;
    if (!SparePaymentAllocation::distribution(db,&distribution,&localError)) {
        fail(error,localError);return {};
    }
    QSet<QPair<int,int>> stockParts;
    for (const auto& slice : distribution)
        if (slice.partId) stockParts.insert({slice.orderNumber,slice.partId});
    const auto rows=PartsQueryService::loadRows(db, {}, {}, {}, &localError);
    if(!localError.isEmpty()){fail(error,localError);return {};}
    for(const auto& row:rows) {
        const auto d=fromPart(row);
        if (stockParts.contains({row.orderNumber,static_cast<int>(d.id)})) continue;
        if(d.remainingCents()>0 && !PartMarkupPolicyService::isInventoryStore(row.store)
            && !PartMarkupPolicyService::isCustomerSupplied(row.store)) result.append(d);
    }
    if(exists(db,"workshop_inventory_purchases")) {
        const auto paid = balances(db, &localError);
        if(!localError.isEmpty()){fail(error,localError);return {};}
        QSqlQuery q(db);
        QString sql = QStringLiteral("SELECT id,purchased_on,material_name,quantity,unit_cost,supplier,note FROM workshop_inventory_purchases");
        // Older databases and read-only report connections may predate returns.
        if (exists(db, "returned_inventory_purchases"))
            sql += QStringLiteral(" WHERE id NOT IN (SELECT purchase_id FROM returned_inventory_purchases)");
        if(!q.exec(sql)) { fail(error,q.lastError().text()); return {}; }
        while(q.next()) {
            PurchaseDebt d;
            d.source="inventory"; d.id=q.value(0).toLongLong();
            d.date=q.value(1).toString(); d.name=q.value(2).toString();
            d.quantity=q.value(3).toDouble();
            d.totalCents=qMax<qint64>(0,cents(d.quantity*q.value(4).toDouble()));
            d.supplier=q.value(5).toString(); d.note=q.value(6).toString();
            d.paidCents=paid.value(key(d.source,d.id),d.totalCents);
            if(d.remainingCents()>0) result.append(d);
        }
        if(q.lastError().isValid()){fail(error,q.lastError().text());return {};}
    }
    const auto deadlines=terms(db,&localError);
    if(!localError.isEmpty()){fail(error,localError);return {};}
    for(auto& d:result) {
        d.dueDate=deadlines.value(key(d.source,d.id,d.orderUid));
        d.supplier=StoreNameNormalizer::canonicalDisplayName(d.supplier);
    }
    std::stable_sort(result.begin(), result.end(), [](const auto& a,const auto& b){
        return a.supplier==b.supplier ? PartsQueryService::parseBusinessDate(a.date)<PartsQueryService::parseBusinessDate(b.date) : a.supplier<b.supplier;
    });
    return result;
}
QVector<PurchasePaymentEntry> history(QSqlDatabase db,const PurchaseDebt& debt,QString* error) {
    return groupHistory(db,{debt},error);
}
QVector<PurchasePaymentEntry> groupHistory(QSqlDatabase db,const QVector<PurchaseDebt>& debts,QString* error) {
    if(error)error->clear();
    QVector<PurchasePaymentEntry> result;
    QSet<QString> selected;
    for(const auto& debt:debts)selected.insert(key(debt.source,debt.id,debt.orderUid));
    if(selected.isEmpty())return result;
    if(!db.isValid() || !db.isOpen()){fail(error,QStringLiteral("БД історії оплат недоступна."));return {};}
    if(!exists(db,"purchase_payment_events")){fail(error,QStringLiteral("Журнал оплат відсутній. Історія недоступна."));return {};}
    const QString dueColumn=db.record("purchase_payment_events").indexOf("due_date")>=0 ? "due_date" : "''";
    // One SELECT gives every selected purchase the same SQLite read snapshot.
    // Filter in memory to avoid SQLite bind limits for large groups; duplicate keys count once.
    const bool single = selected.size() == 1;
    QSqlQuery q(db); q.prepare("SELECT datetime(created_at,'localtime'),actor,amount_cents,"+dueColumn+",typeof(amount_cents),id,payment_key FROM purchase_payment_events"
        + QString(single ? " WHERE payment_key=?" : "") + " ORDER BY id");
    if(single) q.addBindValue(*selected.cbegin());
    if(!q.exec()){fail(error,q.lastError().text());return {};}
    while(q.next()) {
        if(!selected.contains(q.value(6).toString()))continue;
        bool valid=false;
        const auto amount=q.value(2).toLongLong(&valid);
        // Zero records defer payment; negative records reverse payments. Both are valid.
        if(!valid || q.value(4).toString()!=QStringLiteral("integer")) {
            fail(error,QStringLiteral("Некоректна сума в записі історії оплат №%1. Очікується ціла кількість копійок.").arg(q.value(5).toString()));
            return {};
        }
        result.append({q.value(0).toString(),q.value(1).toString(),amount,DateFormats::parseDate(q.value(3).toString())});
    }
    if(q.lastError().isValid()){fail(error,q.lastError().text());return {};}
    return result;
}
bool canPay(QSqlDatabase db,const QString& source) {
    const auto actor=ActivityAuditService::currentActor();
    if(!actor.isValid()) return true;
    return actor.role.trimmed().compare("master", Qt::CaseInsensitive) != 0 && AccessControlService::currentUserHasPermission(db,
        source=="inventory" ? QStringLiteral("inventory.purchase") : QStringLiteral("parts.order"));
}
bool pay(QSqlDatabase db,const PurchaseDebt& expected,qint64 amount,QString* error,int deferredDays) { return change(db,expected,amount,false,error,deferredDays); }
QVector<PurchaseDebt> orderPartDebts(QSqlDatabase db, int orderNumber, QString* error) {
    if (error) error->clear();
    QVector<PurchaseDebt> result;
    if (!AccessControlService::canAccessOrder(db, orderNumber)) {
        fail(error, QString::fromUtf8("Наряд недоступний.")); return result;
    }
    for (const auto& debt : debts(db, error))
        if (debt.source == "part" && debt.orderNumber == orderNumber) result.append(debt);
    return result;
}
bool payOrderParts(QSqlDatabase db, int orderNumber, const QVector<PurchaseDebt>& expected, QString* error, qint64 amountCents) {
    if (error) error->clear();
    if (!canPay(db, "part") || !AccessControlService::canAccessOrder(db, orderNumber))
        return fail(error, QString::fromUtf8("Недостатньо прав для оплати запчастин наряду."));
    if (!ensureSchema(db, error)) return false;
    QSqlQuery tx(db);
    if (!tx.exec("SAVEPOINT order_purchase_payment")) return fail(error, tx.lastError().text());
    auto abort = [&] { tx.exec("ROLLBACK TO order_purchase_payment"); tx.exec("RELEASE order_purchase_payment"); PartsQueryService::invalidateCache(); return false; };
    // Acquire the SQLite write lock before comparing the confirmation snapshot.
    if (!tx.exec("UPDATE purchase_payment_balances SET paid_cents=paid_cents WHERE 0")) {
        fail(error, tx.lastError().text()); return abort();
    }
    PartsQueryService::invalidateCache();
    const auto current = orderPartDebts(db, orderNumber, error);
    if (error && !error->isEmpty()) return abort();
    QHash<QString, PurchaseDebt> confirmed;
    for (const auto& debt : expected) {
        const QString paymentKey = key(debt.source, debt.id, debt.orderUid);
        if (debt.source != "part" || debt.orderNumber != orderNumber || confirmed.contains(paymentKey)) {
            fail(error, QString::fromUtf8("Некоректний перелік запчастин для оплати.")); return abort();
        }
        confirmed.insert(paymentKey, debt);
    }
    bool same = confirmed.size() == current.size();
    for (const auto& debt : current) {
        const auto previous = confirmed.value(key(debt.source, debt.id, debt.orderUid));
        same = same && previous.id == debt.id && previous.totalCents == debt.totalCents
            && previous.paidCents == debt.paidCents && previous.dueDate == debt.dueDate;
    }
    if (!same) { fail(error, QString::fromUtf8("Склад запчастин або суми змінилися. Повторіть звірку перед оплатою.")); return abort(); }
    qint64 remaining = 0;
    for (const auto& debt : current) remaining += debt.remainingCents();
    if (amountCents < -1 || amountCents > remaining || amountCents == 0) {
        fail(error, QString::fromUtf8("Вкажіть суму платежу, яка не перевищує борг.")); return abort();
    }
    qint64 unallocated = amountCents == -1 ? remaining : amountCents;
    for (const auto& debt : current) {
        const auto payment = qMin(unallocated, debt.remainingCents());
        if (payment > 0 && !writeBalance(db, debt, debt.paidCents + payment,
                payment == debt.remainingCents() ? QDate() : debt.dueDate, error)) return abort();
        unallocated -= payment;
    }
    if (!tx.exec("RELEASE order_purchase_payment")) { fail(error, tx.lastError().text()); return abort(); }
    PartsQueryService::invalidateCache();
    SnapshotCacheInvalidator::invalidateOrdersAndParts("order_purchase_payment");
    if (!current.isEmpty() && RuntimeServices::backupManager())
        BackupCommitPublisher::publish(BackupCommitPublisher::makeEvent(
            "payments", "purchase_payment", "work_order", QString::number(orderNumber),
            current.first().orderUid, orderNumber, "order_purchase_payment",
            QString::fromUtf8("Оплата запчастин наряду"),
            {"purchase_payment_balances", "purchase_payment_events", "purchase_payment_terms", "work_order_parts", "activity_audit_events", "workshop_spare_payment_allocations"}));
    return true;
}
bool payInvoice(QSqlDatabase db, const RegisteredInvoice& expected, QString* error, qint64 amountCents) {
    return payInvoiceOrders(db, expected, {}, error, amountCents);
}
bool payInvoiceOrders(QSqlDatabase db, const RegisteredInvoice& expected, const QVector<int>& orderNumbers, QString* error, qint64 amountCents) {
    if(expected.apportioned) {
        if(error)*error=QString::fromUtf8("Розподілена накладна містить розрахункові суми. Сплатіть через первинний облік закупівлі.");
        return false;
    }
    if (error) error->clear();
    if (!canPay(db, "part") || !AccessControlService::currentUserHasPermission(db, "nav.reports"))
        return fail(error, QString::fromUtf8("Недостатньо прав для оплати накладної."));
    if (AccessControlService::currentOrderScope(db).restricted)
        return fail(error, QString::fromUtf8("Для оплати всієї накладної потрібен доступ до всіх пов'язаних нарядів."));
    if (!ensureSchema(db, error)) return false;
    QSqlQuery tx(db);
    if (!tx.exec("SAVEPOINT invoice_payment")) return fail(error, tx.lastError().text());
    auto abort = [&] { tx.exec("ROLLBACK TO invoice_payment"); tx.exec("RELEASE invoice_payment"); PartsQueryService::invalidateCache(); return false; };
    if (!tx.exec("UPDATE purchase_payment_balances SET paid_cents=paid_cents WHERE 0")) {
        fail(error, tx.lastError().text()); return abort();
    }
    PartsQueryService::invalidateCache();
    QString localError;
    const auto invoices = InvoiceRegistryService::load(db, &localError);
    if (!localError.isEmpty()) { fail(error, localError); return abort(); }
    const auto it = std::find_if(invoices.cbegin(), invoices.cend(), [&](const auto& invoice) { return invoice.key == expected.key; });
    if (it == invoices.cend() || it->apportioned || it->amountMismatch || expected.amountMismatch) {
        fail(error, QString::fromUtf8("Накладну не знайдено або суми її позицій не збігаються з підтвердженими даними. Звірте накладну перед оплатою.")); return abort();
    }
    QHash<QString, PurchaseDebt> confirmed;
    for (const auto& part : expected.parts) {
        const auto k = key(part.source, part.id, part.orderUid);
        if (part.source != "part" || confirmed.contains(k)) {
            fail(error, QString::fromUtf8("Некоректний склад накладної.")); return abort();
        }
        confirmed.insert(k, part);
    }
    bool same = it->totalCents == expected.totalCents && it->paidCents == expected.paidCents
        && it->parts.size() == confirmed.size();
    for (const auto& part : it->parts) {
        const auto previous = confirmed.value(key(part.source, part.id, part.orderUid));
        same &= previous.id == part.id && previous.orderNumber == part.orderNumber
            && previous.totalCents == part.totalCents && previous.paidCents == part.paidCents;
        if (!AccessControlService::canAccessOrder(db, part.orderNumber)) {
            fail(error, QString::fromUtf8("Один із нарядів накладної недоступний.")); return abort();
        }
    }
    if (!same) { fail(error, QString::fromUtf8("Позиції або оплати накладної змінилися. Оновіть реєстр і повторіть оплату.")); return abort(); }
    for (int number : orderNumbers) {
        if (!it->orders.contains(number)) {
            fail(error, QString::fromUtf8("Вибраний наряд відсутній у накладній.")); return abort();
        }
    }
    qint64 remaining = 0;
    for (const auto& part : it->parts)
        if (orderNumbers.isEmpty() || orderNumbers.contains(part.orderNumber)) remaining += part.remainingCents();
    if (amountCents < -1 || amountCents > remaining || amountCents == 0) {
        fail(error, QString::fromUtf8("Вкажіть суму платежу, яка не перевищує борг.")); return abort();
    }
    qint64 unallocated = amountCents == -1 ? remaining : amountCents;
    qint64 paidNow = 0;
    for (const auto& part : it->parts)
        if (orderNumbers.isEmpty() || orderNumbers.contains(part.orderNumber)) {
            const auto payment = qMin(unallocated, part.remainingCents());
            paidNow += payment;
            if (payment > 0 && !writeBalance(db, part, part.paidCents + payment,
                    payment == part.remainingCents() ? QDate() : part.dueDate, error)) return abort();
            unallocated -= payment;
        }
    if (!tx.exec("RELEASE invoice_payment")) { fail(error, tx.lastError().text()); return abort(); }
    PartsQueryService::invalidateCache();
    SnapshotCacheInvalidator::invalidateOrdersAndParts("invoice_payment");
    if (paidNow > 0 && RuntimeServices::backupManager())
        BackupCommitPublisher::publish(BackupCommitPublisher::makeEvent("payments", "invoice_payment", "invoice",
            it->key, {}, 0, "invoice_payment", it->number,
            {"purchase_payment_balances", "purchase_payment_events", "purchase_payment_terms", "work_order_parts", "activity_audit_events", "workshop_spare_payment_allocations"}));
    return true;
}
bool resetPart(QSqlDatabase db,int id,QString* error) {
    const auto row=PartsQueryService::loadRowById(db,id,error);
    if(!row.id) return fail(error,QString::fromUtf8("Запчастину не знайдено."));
    return change(db,detail(db,"part",id,row.orderUid,error),0,true,error);
}
bool initializeInventory(QSqlDatabase db,qint64 id,qint64 paid,QString* error,int deferredDays) {
    const auto d=detail(db,"inventory",id,{},error);
    if(!d.id || paid<0 || paid>d.totalCents || deferredDays<0 || deferredDays>3650) return fail(error,QString::fromUtf8("Некоректна сума оплати або відтермінування придбання."));
    auto initial=d; initial.paidCents=0;
    const QDate dueDate=deferredDays>0 && paid<d.totalCents ? QDate::currentDate().addDays(deferredDays) : QDate();
    return writeBalance(db,initial,paid,dueDate,error);
}
}

namespace PurchasePaymentService {
bool resetPart(int id,QString* error) {return resetPart(Database::db(),id,error);}
bool refreshPayment(PartRowSnapshot& row,QString* error) {
    QString failure;const auto values=balances(Database::db(),&failure);
    if(error)*error=failure;if(!failure.isEmpty())return false;
    apply(row,values);return true;
}
}
