#include "OrderClosureJournal.h"
#include <QJsonDocument>
#include <QSqlQuery>
#include <QSqlError>
#include <QDate>
#include <QVariant>
#include <QSet>
#include <limits>
#ifdef ORDER_CLOSURE_TEST
#include <cstdlib>
#endif
#include "core/cache/SnapshotCacheInvalidator.h"

namespace {
QString json(const QJsonObject& object) { return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact)); }
bool fail(QString* error,const QString& text) { if(error)*error=text;return false; }
bool run(QSqlDatabase db,const QString& sql,const QVariantList& args,QString* error) {
    QSqlQuery q(db);q.prepare(sql);for(const auto& value:args)q.addBindValue(value);
    return q.exec() || fail(error,q.lastError().text());
}
struct Transaction {
    QSqlDatabase db; bool done=false;
    ~Transaction(){if(!done){QSqlQuery q(db);q.exec("ROLLBACK TO closure_apply");q.exec("RELEASE closure_apply");}}
    bool commit(QString* error){QSqlQuery q(db);if(!q.exec("RELEASE closure_apply"))return fail(error,q.lastError().text());done=true;return true;}
};
}
namespace OrderClosureJournal {
bool ensureSchema(QSqlDatabase db,QString* error) {
    // Do not rely on cross-file WAL transactions. All four tables must be local.
    QSqlQuery check(db);
    if(!check.exec("SELECT COUNT(*) FROM main.sqlite_master WHERE type='table' AND name IN ('work_orders','work_order_parts')") || !check.next() || check.value(0).toInt()!=2)
        return fail(error,QString::fromUtf8("Для атомарного закриття потрібна робоча БД нарядів і запчастин."));
    return run(db,"CREATE TABLE IF NOT EXISTS main.client_payments(payment_uid TEXT PRIMARY KEY,request_uid TEXT NOT NULL UNIQUE,order_uid TEXT NOT NULL,amount_cents INTEGER NOT NULL CHECK(amount_cents>0),method TEXT NOT NULL,paid_on TEXT NOT NULL,actor TEXT NOT NULL,recorded_ms INTEGER NOT NULL)",{},error)
        && run(db,"CREATE INDEX IF NOT EXISTS main.client_payments_order ON client_payments(order_uid)",{},error)
        && run(db,"CREATE TABLE IF NOT EXISTS main.order_closure_commands(request_uid TEXT PRIMARY KEY,order_uid TEXT NOT NULL,request_json TEXT NOT NULL,response_json TEXT NOT NULL DEFAULT '',state TEXT NOT NULL CHECK(state IN ('pending','applied','rejected')))",{},error)
        && run(db,"CREATE UNIQUE INDEX IF NOT EXISTS main.order_closure_pending ON order_closure_commands(order_uid) WHERE state='pending'",{},error);
}
QJsonObject pending(QSqlDatabase db,const QString& uid,QString* error) {
    if(!ensureSchema(db,error))return {};
    QSqlQuery q(db);q.prepare("SELECT request_json FROM main.order_closure_commands WHERE order_uid=? AND state='pending'");q.addBindValue(uid);
    if(!q.exec()){fail(error,q.lastError().text());return {};}
    return q.next()?QJsonDocument::fromJson(q.value(0).toString().toUtf8()).object():QJsonObject{};
}
bool remember(QSqlDatabase db,const QJsonObject& request,QString* error) {
    const auto id=request["request_uid"].toString(),uid=request["order_uid"].toString();
    if(id.isEmpty() || uid.isEmpty())return fail(error,"Missing closure identity");
    if(!ensureSchema(db,error))return false;
    QSqlQuery q(db);q.prepare("SELECT request_json FROM main.order_closure_commands WHERE request_uid=?");q.addBindValue(id);
    if(!q.exec())return fail(error,q.lastError().text());
    if(q.next())return q.value(0).toString()==json(request) || fail(error,"Closure request ID reused");
    return run(db,"INSERT INTO main.order_closure_commands(request_uid,order_uid,request_json,state) VALUES(?,?,?,'pending')",{id,uid,json(request)},error);
}
bool reject(QSqlDatabase db,const QString& id,QString* error) {
    return run(db,"UPDATE main.order_closure_commands SET state='rejected' WHERE request_uid=? AND state='pending'",{id},error);
}
bool apply(QSqlDatabase db,const QJsonObject& result,QString* error) {
    const QString id=result["request_uid"].toString(),uid=result["order_uid"].toString();
    const auto order=result["order"].toObject();
    if(result["schema_version"].toInt()!=1 || !result["ok"].toBool() || id.isEmpty() || uid.isEmpty()
       || result["status"]!="closed" || order["order_uid"]!=uid || order["closed_at"].toString().isEmpty())
        return fail(error,"Invalid order closure response");
    if(!ensureSchema(db,error))return false;
    if(!run(db,"SAVEPOINT closure_apply",{},error))return false;
    Transaction tx{db};
    QSqlQuery prior(db);prior.prepare("SELECT response_json,state,order_uid FROM main.order_closure_commands WHERE request_uid=?");prior.addBindValue(id);
    if(!prior.exec())return fail(error,prior.lastError().text());
    if(prior.next()) {
        if(prior.value(2).toString()!=uid)return fail(error,"Closure request belongs to another order");
        if(prior.value(1).toString()=="applied")
            return prior.value(0).toString()==json(result) ? tx.commit(error) : fail(error,"Changed response for applied closure");
    }
    QSqlQuery parent(db);parent.prepare("SELECT number FROM main.work_orders WHERE order_uid=?");parent.addBindValue(uid);
    if(!parent.exec() || !parent.next())return fail(error,QString::fromUtf8("Наряд ще не синхронізовано. Повторіть відновлення після синхронізації."));
    if(!result["payments"].isArray() || !result["installed_parts"].isArray())return fail(error,"Incomplete closure response");
    const auto payments=result["payments"].toArray();
    qint64 receiptTotal=0;
    QSet<QString> receiptIds, receiptRequests;
    for(const auto& value:payments) {
        const auto p=value.toObject();const auto cents=p["amount_cents"].toInteger(-1);
        if(p["order_uid"]!=uid || p["payment_uid"].toString().isEmpty() || p["request_uid"].toString().isEmpty()
           || cents<=0 || cents>9000000000000000LL || !QDate::fromString(p["paid_on"].toString(),Qt::ISODate).isValid())
            return fail(error,"Invalid client receipt");
        const QString receiptId=p["payment_uid"].toString(), receiptRequest=p["request_uid"].toString();
        if(receiptIds.contains(receiptId) || receiptRequests.contains(receiptRequest))
            return fail(error,QString::fromUtf8("Відповідь закриття містить повтор платежу. Зміни скасовано; повторіть отримання результату."));
        receiptIds.insert(receiptId);receiptRequests.insert(receiptRequest);
        if(p["amount_cents"].toDouble(-1)!=static_cast<double>(cents))
            return fail(error,QString::fromUtf8("Сума платежу повинна бути цілим числом копійок."));
        if(cents>std::numeric_limits<qint64>::max()-receiptTotal)
            return fail(error,QString::fromUtf8("Переповнення підсумку клієнтських платежів."));
        QVariantList args{p["payment_uid"].toString(),p["request_uid"].toString(),uid,cents,p["method"].toString(),p["paid_on"].toString(),p["actor"].toString(),p["recorded_ms"].toInteger()};
        if(!run(db,"INSERT INTO main.client_payments(payment_uid,request_uid,order_uid,amount_cents,method,paid_on,actor,recorded_ms) VALUES(?,?,?,?,?,?,?,?) ON CONFLICT(payment_uid) DO NOTHING",args,error))return false;
        QSqlQuery verify(db);verify.prepare("SELECT payment_uid,request_uid,order_uid,amount_cents,method,paid_on,actor,recorded_ms FROM main.client_payments WHERE payment_uid=?");verify.addBindValue(args[0]);
        if(!verify.exec() || !verify.next())return fail(error,"Receipt verification failed");
        for(int i=0;i<args.size();++i)if(verify.value(i).toString()!=args[i].toString())return fail(error,"Client receipt conflict");
        receiptTotal+=cents;
    }
    if(receiptTotal!=result["received_cents"].toInteger(-1))return fail(error,"Client receipt total mismatch");
    for(const auto& value:result["installed_parts"].toArray()) {
        const auto part=value.toObject();const QString date=part["installed_at"].toString();
        if(part["order_uid"]!=uid || !QDate::fromString(date,Qt::ISODate).isValid())return fail(error,"Invalid installed part");
        QSqlQuery exists(db);exists.prepare("SELECT installed_at FROM main.work_order_parts WHERE part_uid=? AND order_uid=?");exists.addBindValue(part["part_uid"].toString());exists.addBindValue(uid);
        if(!exists.exec() || !exists.next())return fail(error,"Installed part is missing; synchronize and retry");
        if(exists.value(0).toString().trimmed().isEmpty() && !run(db,"UPDATE main.work_order_parts SET installed_at=? WHERE part_uid=? AND order_uid=?",{date,part["part_uid"].toString(),uid},error))return false;
    }
    if(!run(db,"UPDATE main.work_orders SET status='closed',closed_at=? WHERE order_uid=?",{order["closed_at"].toString(),uid},error))return false;
    if(db.tables().contains("masters")) {
        if(!run(db,"UPDATE main.work_orders SET master_name_closed=COALESCE((SELECT name FROM masters WHERE id=work_orders.master_id),'') WHERE order_uid=?",{uid},error))return false;
        QSqlQuery tables(db);
        if(!tables.exec("SELECT 1 FROM main.sqlite_master WHERE type='table' AND name='service_masters'"))return fail(error,tables.lastError().text());
        if(tables.next() && !run(db,"UPDATE main.service_masters SET master_name_closed=(SELECT name FROM masters WHERE id=service_masters.master_id) WHERE service_id IN (SELECT id FROM main.work_order_services WHERE order_uid=?)",{uid},error))return false;
    }
#ifdef ORDER_CLOSURE_TEST
    if(qEnvironmentVariable("CS_CLOSURE_CRASH")=="before_commit")std::_Exit(87);
#endif
    if(!run(db,"INSERT INTO main.order_closure_commands(request_uid,order_uid,request_json,response_json,state) VALUES(?,?,'{}',?,'applied') ON CONFLICT(request_uid) DO UPDATE SET response_json=excluded.response_json,state='applied'",{id,uid,json(result)},error))return false;
    if(!tx.commit(error))return false;
#ifdef ORDER_CLOSURE_TEST
    if(qEnvironmentVariable("CS_CLOSURE_CRASH")=="after_commit")std::_Exit(87);
#endif
    SnapshotCacheInvalidator::invalidateOrdersAndParts("OrderClosureJournal");
    SnapshotCacheInvalidator::invalidateReportsAndStats("OrderClosureJournal");
    return true;
}
}
