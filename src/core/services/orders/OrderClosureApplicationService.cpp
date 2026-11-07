#include "OrderClosureApplicationService.h"
#include "OrderClosureJournal.h"
#include "LocalWorkshopBackend.h"
#include "core/cloudsync/OrderPartIssueEvidence.h"
#include "core/services/PartsQueryService.h"
#include "infrastructure/db/sqlite/Db.h"
#include <QSqlQuery>
#include <QSqlRecord>
#include <QSqlError>
namespace OrderClosureApplicationService {
QJsonObject readState(QSqlDatabase db,const QString& uid,QString* error) {
    if(error)error->clear();
    QJsonObject result;
    QSqlQuery order(db);order.prepare("SELECT * FROM work_orders WHERE order_uid=?");order.addBindValue(uid);
    if(!order.exec()){if(error)*error=order.lastError().text();return {};}
    if(!order.next()){if(error)*error=order.lastError().isValid()?order.lastError().text():QString::fromUtf8("Наряд не знайдено.");return {};}
    auto object=[](const QSqlQuery& q){QJsonObject row;const auto record=q.record();for(int i=0;i<record.count();++i)row.insert(record.fieldName(i),QJsonValue::fromVariant(q.value(i)));return row;};
    result["order"]=object(order);
    for(const auto& pair:{qMakePair(QString("parts"),QString("work_order_parts")),qMakePair(QString("services"),QString("work_order_services"))}) {
        QSqlQuery q(db);q.prepare("SELECT * FROM "+pair.second+" WHERE order_uid=? ORDER BY "+(pair.first=="parts"?"part_uid":"service_uid"));q.addBindValue(uid);
        if(!q.exec()){if(error)*error=q.lastError().text();return {};}
        QJsonArray rows;while(q.next())rows.append(object(q));
        if(q.lastError().isValid()){if(error)*error=q.lastError().text();return {};}
        result[pair.first]=pair.first=="parts"?OrderPartIssueEvidence::attach(db,rows):rows;
    }
    return result;
}

QJsonObject readState(const QString& uid,QString* error) { return readState(Database::db(),uid,error); }
QStringList pendingOrders(QSqlDatabase db,QString* error) {
    if(error)error->clear();
    QSqlQuery exists(db);
    if(!exists.exec("SELECT 1 FROM main.sqlite_master WHERE type='table' AND name='order_closure_commands'")) {
        if(error)*error=exists.lastError().text();return {};
    }
    if(!exists.next()) {
        if(error && exists.lastError().isValid())*error=exists.lastError().text();return {};
    }
    QSqlQuery query(db);
    if(!query.exec("SELECT order_uid FROM main.order_closure_commands WHERE state='pending' ORDER BY order_uid")) {
        if(error)*error=query.lastError().text();return {};
    }
    QStringList result;
    while(query.next())result.append(query.value(0).toString());
    if(query.lastError().isValid()){if(error)*error=query.lastError().text();return {};}
    return result;
}
QStringList pendingOrders(QString* error) { return pendingOrders(Database::db(),error); }
Reply localRequest(const QString& endpoint,const QJsonObject& payload) {
    const auto reply=LocalWorkshopBackend::request(Database::db(),endpoint,payload);
    return {reply.status,reply.body,reply.ok};
}
QJsonObject pending(const QString& uid,QString* error) { return OrderClosureJournal::pending(Database::db(),uid,error); }
bool remember(const QJsonObject& command,QString* error) { return OrderClosureJournal::remember(Database::db(),command,error); }
bool reject(const QString& requestUid,QString* error) { return OrderClosureJournal::reject(Database::db(),requestUid,error); }
bool apply(const QJsonObject& result,QString* error) {
    if(!OrderClosureJournal::apply(Database::db(),result,error))return false;
    PartsQueryService::invalidateCache();
    return true;
}
bool refreshRegistry(const QString& uid,QString* error) {
    const auto state=readState(uid,error);
    if(state.isEmpty())return false;
    return Database::refreshOrderRegistry(state["order"].toObject()["number"].toInt(),error);
}
}

QString OrderClosureApplicationService::deletionBlockReason(const QString& orderUid) { return LocalWorkshopBackend::deletionBlockReason(Database::db(), orderUid); }
