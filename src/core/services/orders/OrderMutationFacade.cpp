#include "core/services/orders/OrderMutationFacade.h"

#include "infrastructure/db/sqlite/Db.h"
#include "core/repository/PartRepository.h"
#include "core/repository/ServiceRepository.h"
#include "core/services/OrderDetailsRepository.h"
#include "core/services/orders/OrderStatusService.h"
#include "core/services/orders/HistoryPartsAutofillService.h"
#include "core/util/FormatUtils.h"
#include "core/services/MutationExecutor.h"
#include "core/services/AccessControlService.h"
#include "core/cache/OrderSnapshotCache.h"

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

QMap<QString,int> OrderMutationFacade::workshopPosts()
{
    QMap<QString,int> result;
    const auto db=Database::db();
    if(!db.tables().contains("order_workshop_posts")) return result;
    QSqlQuery q(db);
    if(q.exec("SELECT order_uid, post_number FROM order_workshop_posts"))
        while(q.next()) result.insert(q.value(0).toString(),q.value(1).toInt());
    return result;
}

bool OrderMutationFacade::setWorkshopPost(int orderNumber,int post,QString* errorText)
{
    auto db=Database::db();
    if(post<0 || post>3 || !AccessControlService::canAccessOrder(db,orderNumber)) {
        if(errorText)*errorText=QString::fromUtf8("Немає доступу до наряду або номер поста некоректний.");
        return false;
    }
    MutationExecutor::Request request;
    request.sourceModule="OrderMutationFacade"; request.entityType="work_order";
    request.entityId=QString::number(orderNumber); request.operation="set_workshop_post";
    request.description=QString::fromUtf8("Зміна поста наряду");
    request.changedTables={"order_workshop_posts"};
    const auto result=MutationExecutor::execute(db,request,[&](QSqlDatabase& tx,QString* error){
        QSqlQuery q(tx);
        q.prepare("SELECT order_uid FROM work_orders WHERE number=? AND TRIM(order_uid)<>''");
        q.addBindValue(orderNumber);
        if(!q.exec() || !q.next()) {if(error)*error=QString::fromUtf8("Наряд недоступний для зміни поста.");return false;}
        const auto uid=q.value(0).toString();
        if(!q.exec("CREATE TABLE IF NOT EXISTS order_workshop_posts (order_uid TEXT PRIMARY KEY, post_number INTEGER NOT NULL CHECK(post_number BETWEEN 0 AND 3))")) {
            if(error)*error=q.lastError().text();return false;
        }
        q.prepare("INSERT INTO order_workshop_posts(order_uid,post_number) VALUES(?,?) ON CONFLICT(order_uid) DO UPDATE SET post_number=excluded.post_number");
        q.addBindValue(uid);q.addBindValue(post);
        if(!q.exec()){if(error)*error=q.lastError().text();return false;}
        return true;
    });
    if(!result.ok && errorText)*errorText=result.errorText;
    if(result.ok) OrderSnapshotCache::invalidateCurrentOrders("workshop_post_changed");
    return result.ok;
}


namespace {
MutationResult wrapFacadeBoolResult(const QString& entityType,
                                    const QString& entityId,
                                    const QString& operation,
                                    const QString& description,
                                    const QStringList& changedTables,
                                    bool ok,
                                    const QString& errorText)
{
    MutationResult result;
    result.ok = ok;
    result.sourceModule = QStringLiteral("OrderMutationFacade");
    result.entityType = entityType;
    result.entityId = entityId;
    result.operation = operation;
    result.description = description;
    result.changedTables = changedTables;
    result.errorText = errorText;
    return result;
}
}


bool OrderMutationFacade::insertServiceWithOrderMaster(int orderNumber,
                                                       WorkOrderServiceRow row,
                                                       int* newServiceId,
                                                       QString* errorText)
{
    QSqlDatabase db = Database::db();
    QSqlQuery orderQ(db);
    orderQ.prepare("SELECT master_id, COALESCE(master_name_closed,''), COALESCE((SELECT name FROM masters WHERE id=work_orders.master_id), '') FROM work_orders WHERE number=?");
    orderQ.addBindValue(orderNumber);
    if (orderQ.exec() && orderQ.next()) {
        row.masterId = orderQ.value(0).toString();
        row.masterName = orderQ.value(2).toString();
        if (row.masterName.isEmpty()) row.masterName = orderQ.value(1).toString();
    }

    MutationExecutor::Request request;
    request.sourceModule = QStringLiteral("OrderMutationFacade");
    request.entityType = QStringLiteral("work_order_service");
    request.entityId = QString::number(orderNumber);
    request.operation = QStringLiteral("insert_service");
    request.description = QString::fromUtf8("Додавання роботи в наряд");
    request.changedTables = {QStringLiteral("work_order_services"), QStringLiteral("service_masters"),
                             QStringLiteral("work_order_parts"), QStringLiteral("work_orders")};
    request.beginErrorFallback = QString::fromUtf8("Не вдалося відкрити транзакцію.");
    request.commitErrorFallback = QString::fromUtf8("Не вдалося завершити зміну.");

    int createdServiceId = 0;
    const auto execResult = MutationExecutor::execute(db, request, [&](QSqlDatabase& txDb, QString* txError) {
        if (!ServiceRepository::insert(txDb, orderNumber, row, &createdServiceId, txError)) {
            return false;
        }
        if (!HistoryPartsAutofillService::apply(txDb, orderNumber, createdServiceId, txError)) return false;
        return OrderStatusService::promoteToInProgressIfNeeded(txDb, orderNumber, txError);
    });
    if (!execResult.ok) {
        if (errorText) *errorText = execResult.errorText;
        return false;
    }

    Database::refreshOrderRegistry(orderNumber, nullptr);
    if (newServiceId) *newServiceId = createdServiceId;
    return true;
}

bool OrderMutationFacade::updatePrimaryMaster(int orderNumber,
                                              const QString& masterId,
                                              const QString& masterName,
                                              QString* errorText)
{
    QSqlDatabase db = Database::db();
    MutationExecutor::Request request;
    request.sourceModule = QStringLiteral("OrderMutationFacade");
    request.entityType = QStringLiteral("work_order");
    request.entityId = QString::number(orderNumber);
    request.operation = QStringLiteral("update_primary_master");
    request.description = QString::fromUtf8("Зміна основного майстра наряду");
    request.changedTables = {QStringLiteral("work_orders")};
    request.beginErrorFallback = QString::fromUtf8("Не вдалося відкрити транзакцію.");
    request.commitErrorFallback = QString::fromUtf8("Не вдалося завершити зміну.");
    const auto execResult = MutationExecutor::execute(db, request, [&](QSqlDatabase& txDb, QString* txError) {
        QSqlQuery up(txDb);
        up.prepare("UPDATE work_orders SET master_id=?, master_name_closed=? WHERE number=?");
        up.addBindValue(masterId);
        up.addBindValue(masterName);
        up.addBindValue(orderNumber);
        if (!up.exec()) {
            if (txError) *txError = up.lastError().text();
            return false;
        }
        return true;
    });
    if (!execResult.ok) {
        if (errorText) *errorText = execResult.errorText;
        return false;
    }
    Database::refreshOrderRegistry(orderNumber, nullptr);
    return true;
}

bool OrderMutationFacade::setServiceMaster(int orderNumber,
                                           int serviceId,
                                           const QString& masterId,
                                           const QString& masterName,
                                           QString* errorText)
{
    QSqlDatabase db = Database::db();
    MutationExecutor::Request request;
    request.sourceModule = QStringLiteral("OrderMutationFacade");
    request.entityType = QStringLiteral("work_order_service");
    request.entityId = QString::number(serviceId);
    request.operation = QStringLiteral("set_service_master");
    request.description = QString::fromUtf8("Зміна майстра роботи");
    request.changedTables = {QStringLiteral("work_order_services")};
    request.beginErrorFallback = QString::fromUtf8("Не вдалося відкрити транзакцію.");
    request.commitErrorFallback = QString::fromUtf8("Не вдалося завершити зміну.");
    const auto execResult = MutationExecutor::execute(db, request, [&](QSqlDatabase& txDb, QString* txError) {
        return ServiceRepository::replaceMasterLink(txDb, serviceId, masterId, masterName, txError);
    });
    if (!execResult.ok) {
        if (errorText) *errorText = execResult.errorText;
        return false;
    }
    Database::refreshOrderRegistry(orderNumber, nullptr);
    return true;
}

bool OrderMutationFacade::clearServiceMaster(int orderNumber,
                                             int serviceId,
                                             QString* errorText)
{
    return setServiceMaster(orderNumber, serviceId, QString(), QString(), errorText);
}

bool OrderMutationFacade::setServicePrice(int orderNumber,
                                          int serviceId,
                                          double price,
                                          QString* errorText)
{
    WorkOrderServiceRow service;
    bool foundService = false;
    const auto rows = OrderDetailsRepository::listServicesByInternalNumber(Database::db(), orderNumber);
    for (const auto& candidate : rows) {
        if (candidate.id == serviceId) { service = candidate; foundService = true; break; }
    }
    if (!foundService) {
        if (errorText) *errorText = QString::fromUtf8("Роботу не знайдено.");
        return false;
    }

    service.price = FormatUtils::roundMoneyValue(qMax(0.0, price));
    service.amount = service.qty * service.price;

    QSqlDatabase db = Database::db();
    MutationExecutor::Request request;
    request.sourceModule = QStringLiteral("OrderMutationFacade");
    request.entityType = QStringLiteral("work_order_service");
    request.entityId = QString::number(serviceId);
    request.operation = QStringLiteral("set_service_price");
    request.description = QString::fromUtf8("Зміна ціни роботи");
    request.changedTables = {QStringLiteral("work_order_services")};
    request.beginErrorFallback = QString::fromUtf8("Не вдалося відкрити транзакцію.");
    request.commitErrorFallback = QString::fromUtf8("Не вдалося завершити зміну.");
    const auto execResult = MutationExecutor::execute(db, request, [&](QSqlDatabase& txDb, QString* txError) {
        return ServiceRepository::update(txDb, orderNumber, serviceId, service, txError);
    });
    if (!execResult.ok) {
        if (errorText) *errorText = execResult.errorText;
        return false;
    }
    return true;
}

bool OrderMutationFacade::deleteService(int orderNumber,
                                        int serviceId,
                                        QString* errorText)
{
    return deleteServices(orderNumber, QList<int>{serviceId}, errorText);
}

bool OrderMutationFacade::deleteServices(int orderNumber,
                                         const QList<int>& serviceIds,
                                         QString* errorText)
{
    if (serviceIds.isEmpty()) return true;
    QSqlDatabase db = Database::db();
    MutationExecutor::Request request;
    request.sourceModule = QStringLiteral("OrderMutationFacade");
    request.entityType = QStringLiteral("work_order_service_batch");
    request.entityId = QString::number(orderNumber);
    request.operation = QStringLiteral("delete_services");
    request.description = QString::fromUtf8("Видалення робіт наряду");
    request.changedTables = {QStringLiteral("work_order_services")};
    request.beginErrorFallback = QString::fromUtf8("Не вдалося відкрити транзакцію.");
    request.commitErrorFallback = QString::fromUtf8("Не вдалося завершити видалення.");
    const auto execResult = MutationExecutor::execute(db, request, [&](QSqlDatabase& txDb, QString* txError) {
        for (int serviceId : serviceIds) {
            if (!ServiceRepository::remove(txDb, orderNumber, serviceId, txError)) {
                return false;
            }
        }
        return true;
    });
    if (!execResult.ok) {
        if (errorText) *errorText = execResult.errorText;
        return false;
    }
    Database::refreshOrderRegistry(orderNumber, nullptr);
    return true;
}

bool OrderMutationFacade::deletePart(int orderNumber,
                                     int partId,
                                     QString* errorText)
{
    QSqlDatabase db = Database::db();
    MutationExecutor::Request request;
    request.sourceModule = QStringLiteral("OrderMutationFacade");
    request.entityType = QStringLiteral("work_order_part");
    request.entityId = QString::number(partId);
    request.operation = QStringLiteral("delete_part");
    request.description = QString::fromUtf8("Видалення запчастини наряду");
    request.changedTables = {QStringLiteral("work_order_parts")};
    request.beginErrorFallback = QString::fromUtf8("Не вдалося відкрити транзакцію.");
    request.commitErrorFallback = QString::fromUtf8("Не вдалося завершити видалення.");
    const auto execResult = MutationExecutor::execute(db, request, [&](QSqlDatabase& txDb, QString* txError) {
        return PartRepository::remove(txDb, orderNumber, partId, txError);
    });
    if (!execResult.ok) {
        if (errorText) *errorText = execResult.errorText;
        return false;
    }
    Database::refreshOrderRegistry(orderNumber, nullptr);
    return true;
}

MutationResult OrderMutationFacade::insertServiceWithOrderMasterDetailed(int orderNumber, WorkOrderServiceRow row, int* newServiceId)
{
    QString errorText;
    const bool ok = insertServiceWithOrderMaster(orderNumber, row, newServiceId, &errorText);
    return wrapFacadeBoolResult(QStringLiteral("work_order_service"), QString::number(orderNumber), QStringLiteral("insert_service"), QString::fromUtf8("Додавання роботи в наряд"), {QStringLiteral("work_order_services"), QStringLiteral("work_orders")}, ok, errorText);
}

MutationResult OrderMutationFacade::updatePrimaryMasterDetailed(int orderNumber, const QString& masterId, const QString& masterName)
{
    QString errorText;
    const bool ok = updatePrimaryMaster(orderNumber, masterId, masterName, &errorText);
    return wrapFacadeBoolResult(QStringLiteral("work_order"), QString::number(orderNumber), QStringLiteral("update_primary_master"), QString::fromUtf8("Зміна основного майстра наряду"), {QStringLiteral("work_orders")}, ok, errorText);
}

MutationResult OrderMutationFacade::setServiceMasterDetailed(int orderNumber, int serviceId, const QString& masterId, const QString& masterName)
{
    QString errorText;
    const bool ok = setServiceMaster(orderNumber, serviceId, masterId, masterName, &errorText);
    return wrapFacadeBoolResult(QStringLiteral("work_order_service"), QString::number(serviceId), QStringLiteral("set_service_master"), QString::fromUtf8("Зміна майстра роботи"), {QStringLiteral("work_order_services")}, ok, errorText);
}

MutationResult OrderMutationFacade::setServicePriceDetailed(int orderNumber, int serviceId, double price)
{
    QString errorText;
    const bool ok = setServicePrice(orderNumber, serviceId, price, &errorText);
    return wrapFacadeBoolResult(QStringLiteral("work_order_service"), QString::number(serviceId), QStringLiteral("set_service_price"), QString::fromUtf8("Зміна ціни роботи"), {QStringLiteral("work_order_services")}, ok, errorText);
}

MutationResult OrderMutationFacade::deleteServicesDetailed(int orderNumber, const QList<int>& serviceIds)
{
    QString errorText;
    const bool ok = deleteServices(orderNumber, serviceIds, &errorText);
    return wrapFacadeBoolResult(QStringLiteral("work_order_service_batch"), QString::number(orderNumber), QStringLiteral("delete_services"), QString::fromUtf8("Видалення робіт з наряду"), {QStringLiteral("work_order_services")}, ok, errorText);
}

MutationResult OrderMutationFacade::deletePartDetailed(int orderNumber, int partId)
{
    QString errorText;
    const bool ok = deletePart(orderNumber, partId, &errorText);
    return wrapFacadeBoolResult(QStringLiteral("work_order_part"), QString::number(partId), QStringLiteral("delete_part"), QString::fromUtf8("Видалення запчастини з наряду"), {QStringLiteral("work_order_parts")}, ok, errorText);
}
