#include "core/repository/RepositoryWriteBoundary.h"
#include "core/services/orders/OrderFinancialService.h"
#include "core/repository/ServiceRepository.h"
#include "core/services/OrderIdentityResolver.h"

#include <QSqlError>
#include <QSqlQuery>

namespace {
QString nonNullText(const QString& value)
{
    return value.isNull() ? QStringLiteral("") : value;
}

QString orderUidForNumber(QSqlDatabase database, int orderNumber)
{
    return OrderIdentityResolver::canonicalOrderUidForInternalNumber(database, orderNumber);
}

bool refreshOrderFinancialSums(QSqlDatabase database, const QString& orderUid, int orderNumber, QString* errorText = nullptr)
{
    if (orderUid.trimmed().isEmpty() || orderNumber <= 0) {
        if (errorText) *errorText = QStringLiteral("Missing order identity for financial refresh");
        return false;
    }
    return OrderFinancialService::refreshOrderFinancialSumsByUid(database, orderUid, errorText);
}

QList<WorkOrderServiceRow> listByOrderImpl(QSqlDatabase database, const QString& orderUid, int orderNumber)
{
    QList<WorkOrderServiceRow> rows;
    if (orderUid.trimmed().isEmpty() && orderNumber <= 0) return rows;

    QSqlQuery q(database);
    q.prepare(QStringLiteral(
        "SELECT s.id, COALESCE(s.title,''), COALESCE(s.qty,1), COALESCE(s.price,0), COALESCE(s.amount,0), "
        "COALESCE(MAX(sm.master_id), ''), COALESCE(MAX(NULLIF(m.name,'')), MAX(NULLIF(sm.master_name_closed,'')), ''), "
        "COALESCE(s.order_number,0) "
        "FROM work_order_services s "
        "LEFT JOIN service_masters sm ON sm.service_id=s.id "
        "LEFT JOIN masters m ON m.id=sm.master_id "
        "WHERE TRIM(COALESCE(s.order_uid,''))<>'' AND s.order_uid=? "
        "GROUP BY s.id, s.title, s.qty, s.price, s.amount, s.order_number "
        "ORDER BY s.id"));
    q.addBindValue(orderUid.trimmed());
    if (!q.exec()) return rows;

    while (q.next()) {
        WorkOrderServiceRow row;
        row.id = q.value(0).toInt();
        row.orderNumber = q.value(7).toInt();
        row.title = q.value(1).toString();
        row.qty = q.value(2).toDouble();
        row.price = q.value(3).toDouble();
        row.amount = q.value(4).toDouble();
        row.masterId = q.value(5).toString();
        row.masterName = q.value(6).toString();
        rows.append(row);
    }
    return rows;
}
}

QList<WorkOrderServiceRow> ServiceRepository::listByOrder(QSqlDatabase database, int orderNumber)
{
    return listByOrderImpl(database, orderUidForNumber(database, orderNumber), orderNumber);
}

QList<WorkOrderServiceRow> ServiceRepository::listByOrderUid(QSqlDatabase database, const QString& orderUid)
{
    return listByOrderImpl(database, orderUid.trimmed(), 0);
}

bool ServiceRepository::replaceMasterLink(QSqlDatabase database,
                                          int serviceId,
                                          const QString& masterId,
                                          const QString& masterName,
                                          QString* errorText)
{
    RepositoryWriteBoundary boundary(database, errorText);
    if (!boundary.started()) return false;
    QSqlQuery del(database);
    del.prepare(QStringLiteral("DELETE FROM service_masters WHERE service_id=?"));
    del.addBindValue(serviceId);
    if (!del.exec()) {
        if (errorText) *errorText = del.lastError().text();
        return false;
    }

    if (masterId.trimmed().isEmpty()) return boundary.finish();

    QSqlQuery ins(database);
    ins.prepare(QStringLiteral("INSERT INTO service_masters(service_id, master_id, share_percent, role, master_name_closed) VALUES(?,?,?,?,?)"));
    ins.addBindValue(serviceId);
    ins.addBindValue(nonNullText(masterId));
    ins.addBindValue(100.0);
    ins.addBindValue(QStringLiteral("master"));
    ins.addBindValue(nonNullText(masterName));
    if (!ins.exec()) {
        if (errorText) *errorText = ins.lastError().text();
        return false;
    }
    return boundary.finish();
}

bool ServiceRepository::insert(QSqlDatabase database,
                               int orderNumber,
                               const WorkOrderServiceRow& row,
                               int* newServiceId,
                               QString* errorText)
{
    if (newServiceId) *newServiceId = 0;
    RepositoryWriteBoundary boundary(database, errorText);
    if (!boundary.started()) return false;
    const QString orderUid = orderUidForNumber(database, orderNumber);
    if (orderUid.isEmpty()) {
        if (errorText) *errorText = QStringLiteral("Order uid not found for order %1").arg(orderNumber);
        return false;
    }

    QSqlQuery q(database);
    q.prepare(QStringLiteral("INSERT INTO work_order_services(order_uid, order_number, title, qty, price, amount) VALUES(?,?,?,?,?,?)"));
    q.addBindValue(orderUid);
    q.addBindValue(orderNumber);
    q.addBindValue(nonNullText(row.title));
    q.addBindValue(row.qty);
    q.addBindValue(row.price);
    q.addBindValue(row.amount);
    if (!q.exec()) {
        if (errorText) *errorText = q.lastError().text();
        return false;
    }
    const int serviceId = q.lastInsertId().toInt();
    if (!replaceMasterLink(database, serviceId, row.masterId, row.masterName, errorText)) return false;
    if (!refreshOrderFinancialSums(database, orderUid, orderNumber, errorText)) {
        return false;
    }
    if (!boundary.finish()) return false;
    if (newServiceId) *newServiceId = serviceId;
    return true;
}

bool ServiceRepository::update(QSqlDatabase database,
                               int orderNumber,
                               int serviceId,
                               const WorkOrderServiceRow& row,
                               QString* errorText)
{
    RepositoryWriteBoundary boundary(database, errorText);
    if (!boundary.started()) return false;
    const QString orderUid = orderUidForNumber(database, orderNumber);
    QSqlQuery owner(database);
    if (!owner.prepare("SELECT 1 FROM work_order_services WHERE id=? AND order_uid=?")) {
        if (errorText) *errorText = owner.lastError().text();
        return false;
    }
    owner.addBindValue(serviceId);owner.addBindValue(orderUid);
    if (!owner.exec() || !owner.next()) {
        if (errorText) *errorText = owner.lastError().isValid() ? owner.lastError().text()
            : QStringLiteral("Робота не належить вибраному наряду або вже видалена.");
        return false;
    }
    owner.finish();
    if (orderUid.isEmpty()) {
        if (errorText) *errorText = QStringLiteral("Order uid not found for order %1").arg(orderNumber);
        return false;
    }

    QSqlQuery q(database);
    q.prepare(QStringLiteral("UPDATE work_order_services SET order_uid=?, order_number=?, title=?, qty=?, price=?, amount=? WHERE id=? AND order_uid=?"));
    q.addBindValue(orderUid);
    q.addBindValue(orderNumber);
    q.addBindValue(nonNullText(row.title));
    q.addBindValue(row.qty);
    q.addBindValue(row.price);
    q.addBindValue(row.amount);
    q.addBindValue(serviceId);
    q.addBindValue(orderUid);
    if (!q.exec()) {
        if (errorText) *errorText = q.lastError().text();
        return false;
    }
    if (!replaceMasterLink(database, serviceId, row.masterId, row.masterName, errorText)) return false;
    if (!refreshOrderFinancialSums(database, orderUid, orderNumber, errorText)) {
        return false;
    }
    return boundary.finish();
}

bool ServiceRepository::remove(QSqlDatabase database,
                               int orderNumber,
                               int serviceId,
                               QString* errorText)
{
    RepositoryWriteBoundary boundary(database, errorText);
    if (!boundary.started()) return false;
    const QString orderUid = orderUidForNumber(database, orderNumber);
    QSqlQuery owner(database);
    if (!owner.prepare("SELECT 1 FROM work_order_services WHERE id=? AND order_uid=?")) {
        if (errorText) *errorText = owner.lastError().text();
        return false;
    }
    owner.addBindValue(serviceId);owner.addBindValue(orderUid);
    if (!owner.exec() || !owner.next()) {
        if (errorText) *errorText = owner.lastError().isValid() ? owner.lastError().text()
            : QStringLiteral("Робота не належить вибраному наряду або вже видалена.");
        return false;
    }
    owner.finish();
    QSqlQuery delSm(database);
    delSm.prepare(QStringLiteral("DELETE FROM service_masters WHERE service_id=?"));
    delSm.addBindValue(serviceId);
    if (!delSm.exec()) {
        if (errorText) *errorText = delSm.lastError().text();
        return false;
    }

    QSqlQuery del(database);
    del.prepare(QStringLiteral("DELETE FROM work_order_services WHERE id=? AND order_uid=?"));
    del.addBindValue(serviceId);
    del.addBindValue(orderUid);
    if (!del.exec()) {
        if (errorText) *errorText = del.lastError().text();
        return false;
    }
    if (!refreshOrderFinancialSums(database, orderUid, orderNumber, errorText)) {
        return false;
    }
    return boundary.finish();
}
