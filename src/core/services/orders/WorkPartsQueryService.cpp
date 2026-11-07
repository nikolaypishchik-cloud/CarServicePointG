#include "core/services/orders/WorkPartsQueryService.h"
#include "core/util/StoreNameNormalizer.h"

#include "infrastructure/db/sqlite/Db.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QVariant>

WorkPartsHeaderSnapshot WorkPartsQueryService::loadHeaderSnapshot(const QString& orderUid)
{
    WorkPartsHeaderSnapshot snapshot;
    if (orderUid.trimmed().isEmpty()) {
        snapshot.errorText = QStringLiteral("Empty order_uid");
        return snapshot;
    }

    QSqlQuery q(Database::db());
    QString sql = QStringLiteral(
        "SELECT COALESCE(w.order_number,w.number), COALESCE(w.order_year, CAST(strftime('%Y', (CASE WHEN substr(trim(COALESCE(w.opened_at,'')),3,1)='.' AND substr(trim(COALESCE(w.opened_at,'')),6,1)='.' THEN CASE WHEN substr(trim(COALESCE(w.opened_at,'')),7,4) GLOB '[0-9][0-9][0-9][0-9]' THEN substr(trim(COALESCE(w.opened_at,'')),7,4) ELSE '20'||substr(trim(COALESCE(w.opened_at,'')),7,2) END||'-'||substr(trim(COALESCE(w.opened_at,'')),4,2)||'-'||substr(trim(COALESCE(w.opened_at,'')),1,2) ELSE COALESCE(w.opened_at,'') END)) AS INTEGER)), COALESCE(w.plate,''), COALESCE(w.vin,''), COALESCE(w.car,''), COALESCE(w.client,''), COALESCE(w.client_phone,''), "
        "COALESCE(m.name, w.master_name_closed, ''), COALESCE(w.status,''), COALESCE(w.opened_at,''), "
        "(SELECT COALESCE(SUM(qty*price),0) FROM work_order_services s WHERE s.order_uid=w.order_uid), "
        "(SELECT COALESCE(SUM(qty*price),0) FROM work_order_parts p WHERE p.order_uid=w.order_uid), "
        "COALESCE(w.closed_at,''), __MOBILE_STATE__ "
        "FROM work_orders w LEFT JOIN masters m ON m.id=w.master_id WHERE w.order_uid=?");
    sql.replace(QStringLiteral("__MOBILE_STATE__"), Database::db().record("work_orders").contains("mobile_state_json")
        ? QStringLiteral("COALESCE(w.mobile_state_json,'{}')") : QStringLiteral("'{}'"));
    q.prepare(sql);
    q.addBindValue(orderUid);
    if (!q.exec()) {
        snapshot.errorText = q.lastError().text();
        return snapshot;
    }
    if (!q.next()) {
        snapshot.errorText = QStringLiteral("Order not found");
        return snapshot;
    }

    snapshot.found = true;
    snapshot.displayNumber = q.value(0).toString();
    snapshot.displayYear = q.value(1).toString();
    snapshot.plate = q.value(2).toString();
    snapshot.vin = q.value(3).toString();
    snapshot.car = q.value(4).toString();
    snapshot.client = q.value(5).toString();
    snapshot.phone = q.value(6).toString();
    snapshot.master = q.value(7).toString();
    snapshot.status = q.value(8).toString();
    snapshot.opened = q.value(9).toString();
    snapshot.worksSum = q.value(10).toDouble();
    snapshot.partsSum = q.value(11).toDouble();
    snapshot.closed = q.value(12).toString();
    snapshot.mobileState = q.value(13).toString();
    return snapshot;
}

WorkPartsMasterSelection WorkPartsQueryService::loadMasterSelection(const QString& orderUid)
{
    WorkPartsMasterSelection selection;
    if (orderUid.trimmed().isEmpty()) return selection;

    QSqlQuery q(Database::db());
    q.prepare("SELECT COALESCE(master_id,''), COALESCE(master_name_closed,'') FROM work_orders WHERE order_uid=?");
    q.addBindValue(orderUid);
    if (q.exec() && q.next()) {
        selection.currentMasterId = q.value(0).toString().trimmed();
        selection.currentMasterName = q.value(1).toString().trimmed();
    }
    return selection;
}

QString WorkPartsQueryService::loadPartDeliveryDate(int partId)
{
    if (partId <= 0) return {};
    QSqlQuery q(Database::db());
    q.prepare(QStringLiteral("SELECT COALESCE(delivery_date,'') FROM work_order_parts WHERE id=? LIMIT 1"));
    q.addBindValue(partId);
    if (q.exec() && q.next()) {
        return q.value(0).toString().trimmed();
    }
    return {};
}

QList<WorkPartsServiceEditorRow> WorkPartsQueryService::loadServiceRowsForOrder(const QString& orderUid)
{
    QList<WorkPartsServiceEditorRow> rows;
    if (orderUid.trimmed().isEmpty()) return rows;

    QSqlQuery q(Database::db());
    q.prepare(
        "SELECT s.id, COALESCE(s.title,''), COALESCE(s.price,0), COALESCE(s.qty,1), "
        "COALESCE(GROUP_CONCAT(COALESCE(NULLIF(m.name,''), NULLIF(sm.master_name_closed,'')), CHAR(10)), ''), "
        "COALESCE(s.status,''), "
        "COALESCE((SELECT COALESCE(NULLIF(oj.company_name,''), '') FROM outsource_jobs oj WHERE oj.service_id=s.id ORDER BY oj.id DESC LIMIT 1), '') "
        "FROM work_order_services s "
        "LEFT JOIN service_masters sm ON sm.service_id=s.id "
        "LEFT JOIN masters m ON m.id=sm.master_id "
        "WHERE s.order_uid=? "
        "GROUP BY s.id, s.title, s.price, s.qty, s.status "
        "ORDER BY s.id");
    q.addBindValue(orderUid);
    if (!q.exec()) return rows;
    while (q.next()) {
        WorkPartsServiceEditorRow row;
        row.id = q.value(0).toInt();
        row.title = q.value(1).toString();
        row.price = q.value(2).toDouble();
        row.qty = q.value(3).toDouble();
        row.masterName = q.value(4).toString();
        row.status = q.value(5).toString();
        row.outsourceCompany = q.value(6).toString();
        rows.push_back(row);
    }
    return rows;
}

QList<WorkPartsPartEditorRow> WorkPartsQueryService::loadPartRowsForOrder(const QString& orderUid)
{
    QList<WorkPartsPartEditorRow> rows;
    if (orderUid.trimmed().isEmpty()) return rows;

    QSqlQuery q(Database::db());
    q.prepare("SELECT id, COALESCE(name,''), COALESCE(qty,1), COALESCE(price,0), COALESCE(amount,0), COALESCE(store,''), COALESCE(part_number,''), COALESCE(ordered_at,''), COALESCE(purchase_price,0), COALESCE(purchase_amount,0), COALESCE(markup_percent,0), COALESCE(manual_markup_override,0), COALESCE(markup_source,'policy'), COALESCE(manufacturer,'') FROM work_order_parts WHERE order_uid=? ORDER BY id");
    q.addBindValue(orderUid);
    if (!q.exec()) return rows;
    while (q.next()) {
        WorkPartsPartEditorRow row;
        row.id = q.value(0).toInt();
        row.name = q.value(1).toString();
        row.qty = q.value(2).toDouble();
        row.price = q.value(3).toDouble();
        row.amount = q.value(4).toDouble();
        row.store = StoreNameNormalizer::canonicalDisplayName(q.value(5).toString());
        row.partNumber = q.value(6).toString();
        row.orderedAt = q.value(7).toString();
        row.purchasePrice = q.value(8).toDouble();
        row.purchaseAmount = q.value(9).toDouble();
        row.markupPercent = q.value(10).toDouble();
        row.manualMarkupOverride = q.value(11).toInt() != 0;
        row.markupSource = q.value(12).toString().trimmed().isEmpty() ? QStringLiteral("policy") : q.value(12).toString().trimmed();
        row.manufacturer = q.value(13).toString();
        rows.push_back(row);
    }
    return rows;
}

QList<int> WorkPartsQueryService::loadPartIdsForOrder(const QString& orderUid)
{
    QList<int> ids;
    if (orderUid.trimmed().isEmpty()) return ids;

    QSqlQuery q(Database::db());
    q.prepare("SELECT id FROM work_order_parts WHERE order_uid=? ORDER BY id");
    q.addBindValue(orderUid);
    if (!q.exec()) return ids;
    while (q.next()) {
        ids.push_back(q.value(0).toInt());
    }
    return ids;
}

WorkPartsOutsourceJobSnapshot WorkPartsQueryService::loadOutsourceJob(const QString& orderUid, int serviceId)
{
    WorkPartsOutsourceJobSnapshot snapshot;
    if (orderUid.trimmed().isEmpty() || serviceId <= 0) return snapshot;

    QSqlQuery q(Database::db());
    q.prepare(QStringLiteral(
        "SELECT COALESCE(company_id,''), COALESCE(company_name,''), COALESCE(sent_date,''), COALESCE(return_date,''), "
        "COALESCE(part_name,''), COALESCE(company_price,0), COALESCE(markup_percent,0), COALESCE(client_price,0), COALESCE(note,'') "
        "FROM outsource_jobs WHERE order_uid=? AND service_id=? ORDER BY id DESC LIMIT 1"));
    q.addBindValue(orderUid);
    q.addBindValue(serviceId);
    if (!q.exec() || !q.next()) return snapshot;

    snapshot.found = true;
    snapshot.companyId = q.value(0).toString();
    snapshot.companyName = q.value(1).toString();
    snapshot.sentDate = q.value(2).toString();
    snapshot.returnDate = q.value(3).toString();
    snapshot.partName = q.value(4).toString();
    snapshot.companyPrice = q.value(5).toDouble();
    snapshot.markupPercent = q.value(6).toDouble();
    snapshot.clientPrice = q.value(7).toDouble();
    snapshot.note = q.value(8).toString().trimmed();
    return snapshot;
}
