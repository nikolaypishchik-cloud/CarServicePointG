#include "DbArchiveHydrationSupport.h"
#include "DbArchiveRouterCacheImportSupport.h"

#include <QtSql/QSqlError>
#include <QtSql/QSqlQuery>
#include <QList>
#include <QMap>
#include <QVariant>
#include <QSet>

namespace {

QString routerCacheOrderUidForInternalNumber_local(QSqlDatabase database, int orderNumber)
{
    if (!database.isValid() || orderNumber <= 0) return QString();
    QSqlQuery q(database);
    q.prepare(QStringLiteral("SELECT COALESCE(order_uid,'') FROM router_cache_work_orders WHERE number=? LIMIT 1"));
    q.addBindValue(orderNumber);
    return (q.exec() && q.next()) ? q.value(0).toString().trimmed() : QString();
}

QString ensureMasterExists_local(QSqlDatabase database, const QString& rawMasterId, const QString& fallbackName)
{
    QString candidate = rawMasterId.trimmed();
    auto existsMaster = [&](const QString& id) -> bool {
        if (id.trimmed().isEmpty()) return false;
        QSqlQuery chk(database);
        chk.prepare(QStringLiteral("SELECT 1 FROM masters WHERE id=? LIMIT 1"));
        chk.addBindValue(id);
        return chk.exec() && chk.next();
    };
    if (existsMaster(candidate)) return candidate;

    QString displayName = fallbackName.trimmed();
    if (displayName.isEmpty()) displayName = QString::fromUtf8("Архівний майстер");
    QString syntheticId = candidate;
    if (syntheticId.isEmpty()) {
        syntheticId = QStringLiteral("__archive__%1").arg(QString::number(static_cast<qulonglong>(qHash(displayName))));
    }
    if (!syntheticId.startsWith(QStringLiteral("__archive__"))) {
        syntheticId = QStringLiteral("__archive__%1").arg(syntheticId);
    }

    QSqlQuery ensureFallback(database);
    ensureFallback.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO masters(id, name, role, admin_percent, active, active_from, active_to) "
        "VALUES(?, ?, 'master', 0, 1, '', '')"));
    ensureFallback.addBindValue(syntheticId);
    ensureFallback.addBindValue(displayName);
    if (!ensureFallback.exec()) return QString();
    return syntheticId;
}

bool copyCachedServices_local(QSqlDatabase database,
                              const QString& routerReadUid,
                              const QString& safeOrderUid,
                              int orderNumber,
                              QMap<int, int>* serviceIdMap,
                              QString* errorText)
{
    struct PendingParentLink { int newServiceId = 0; int oldParentId = 0; };
    QList<PendingParentLink> pendingParents;

    QSqlQuery readSvc(database);
    readSvc.prepare(QStringLiteral(
        "SELECT id, COALESCE(title,''), COALESCE(qty,1), COALESCE(price,0), COALESCE(amount,0), "
        "COALESCE(note,''), COALESCE(paid,1), COALESCE(parent_id,0) "
        "FROM router_cache_work_order_services WHERE order_uid=? ORDER BY id ASC"));
    readSvc.addBindValue(routerReadUid);
    if (!readSvc.exec()) {
        if (errorText) *errorText = readSvc.lastError().text();
        return false;
    }

    while (readSvc.next()) {
        const int oldId = readSvc.value(0).toInt();
        QSqlQuery insSvc(database);
        insSvc.prepare(QStringLiteral(
            "INSERT INTO work_order_services(order_uid, order_number, title, qty, price, amount, note, paid, parent_id) "
            "VALUES(?,?,?,?,?,?,?,?,?)"));
        insSvc.addBindValue(safeOrderUid);
        insSvc.addBindValue(orderNumber);
        insSvc.addBindValue(readSvc.value(1).toString());
        insSvc.addBindValue(readSvc.value(2));
        insSvc.addBindValue(readSvc.value(3));
        insSvc.addBindValue(readSvc.value(4));
        insSvc.addBindValue(readSvc.value(5).toString());
        insSvc.addBindValue(readSvc.value(6));
        insSvc.addBindValue(0);
        if (!insSvc.exec() || insSvc.numRowsAffected() != 1) {
            if (errorText) *errorText = QStringLiteral("Cached service insert failed: %1").arg(insSvc.lastError().text());
            return false;
        }
        const int newServiceId = insSvc.lastInsertId().toInt();
        serviceIdMap->insert(oldId, newServiceId);
        pendingParents.append(PendingParentLink{newServiceId, readSvc.value(7).toInt()});
    }

    if (readSvc.lastError().isValid()) {
        if (errorText) *errorText = readSvc.lastError().text();
        return false;
    }
    for (const PendingParentLink& link : pendingParents) {
        if (link.newServiceId <= 0 || link.oldParentId <= 0) continue;
        const int newParentId = serviceIdMap->value(link.oldParentId, 0);
        if (newParentId <= 0) {
            if (errorText) *errorText = QStringLiteral("Cached parent service is missing from hydrated order");
            return false;
        }
        QSqlQuery upParent(database);
        upParent.prepare(QStringLiteral("UPDATE work_order_services SET parent_id=? WHERE id=?"));
        upParent.addBindValue(newParentId);
        upParent.addBindValue(link.newServiceId);
        if (!upParent.exec() || upParent.numRowsAffected() != 1) {
            if (errorText) *errorText = QStringLiteral("Cached service parent update failed: %1").arg(upParent.lastError().text());
            return false;
        }
    }
    return true;
}

bool copyCachedServiceMasters_local(QSqlDatabase database,
                                    const QString& routerReadUid,
                                    int orderNumber,
                                    const QMap<int, int>& serviceIdMap,
                                    QString* errorText)
{
    QSqlQuery readSm(database);
    readSm.prepare(QStringLiteral(
        "SELECT sm.service_id, sm.master_id, COALESCE(sm.share_percent,100), COALESCE(sm.role,'master'), COALESCE(sm.master_name_closed,'') "
        "FROM router_cache_service_masters sm "
        "JOIN router_cache_work_order_services s ON s.id=sm.service_id "
        "WHERE s.order_uid=? ORDER BY sm.service_id ASC"));
    readSm.addBindValue(routerReadUid);
    if (!readSm.exec()) {
        if (errorText) *errorText = readSm.lastError().text();
        return false;
    }
    while (readSm.next()) {
        const int newServiceId = serviceIdMap.value(readSm.value(0).toInt(), 0);
        if (newServiceId <= 0) continue;
        const QString linkedMasterId = ensureMasterExists_local(database, readSm.value(1).toString(), readSm.value(4).toString());
        if (linkedMasterId.trimmed().isEmpty()) {
            if (errorText) *errorText = QStringLiteral("Failed to ensure cached service master for order %1").arg(orderNumber);
            return false;
        }
        QSqlQuery insSm(database);
        insSm.prepare(QStringLiteral(
            "INSERT OR REPLACE INTO service_masters(service_id, master_id, share_percent, role, master_name_closed) "
            "VALUES(?,?,?,?,?)"));
        insSm.addBindValue(newServiceId);
        insSm.addBindValue(linkedMasterId);
        insSm.addBindValue(readSm.value(2));
        insSm.addBindValue(readSm.value(3).toString());
        insSm.addBindValue(readSm.value(4).toString());
        if (!insSm.exec()) {
            if (errorText) *errorText = insSm.lastError().text();
            return false;
        }
    }
    return true;
}

bool copyCachedParts_local(QSqlDatabase database,
                           const QString& routerReadUid,
                           const QString& safeOrderUid,
                           int orderNumber,
                           const QMap<int, int>& serviceIdMap,
                           QString* errorText)
{
    QSqlQuery readPart(database);
    readPart.prepare(QStringLiteral(
        "SELECT COALESCE(ordered_at,''), COALESCE(delivery_date,''), COALESCE(installed_at,''), COALESCE(store,''), part_number, "
        "COALESCE(name,''), COALESCE(manufacturer,''), COALESCE(note,''), COALESCE(qty,1), COALESCE(price,0), "
        "COALESCE(amount,0), COALESCE(service_id,0), COALESCE(ordered_by_admin,1), COALESCE(paid,0), "
        "COALESCE(purchase_price,0), COALESCE(purchase_amount,0), COALESCE(markup_percent,0), "
        "COALESCE(manual_markup_override,0), markup_source, "
        "price_input_mode, COALESCE(pricing_updated_at,''), COALESCE(pricing_calc_version,1), part_uid, status, installed, installed_by, request_uid, requested_at, requested_by, requested_by_uid, ordered_by, ordered_by_uid, order_comment, node_group, component, metadata_json, created_at, updated_at "
        "FROM router_cache_work_order_parts WHERE order_uid=? ORDER BY id ASC"));
    readPart.addBindValue(routerReadUid);
    if (!readPart.exec()) {
        if (errorText) *errorText = readPart.lastError().text();
        return false;
    }
    while (readPart.next()) {
        QSqlQuery insPart(database);
        insPart.prepare(QStringLiteral(
            "INSERT INTO work_order_parts(part_uid, order_uid, order_number, ordered_at, delivery_date, installed_at, store, part_number, name, manufacturer, note, qty, price, amount, service_id, ordered_by_admin, paid, purchase_price, purchase_amount, markup_percent, manual_markup_override, markup_source, price_input_mode, pricing_updated_at, pricing_calc_version, status, installed, installed_by, request_uid, requested_at, requested_by, requested_by_uid, ordered_by, ordered_by_uid, order_comment, node_group, component, metadata_json, created_at, updated_at) "
            "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)"));
        insPart.addBindValue(readPart.value(22));
        insPart.addBindValue(safeOrderUid);
        insPart.addBindValue(orderNumber);
        for (int i = 0; i <= 10; ++i) insPart.addBindValue(readPart.value(i));
        const int oldServiceId = readPart.value(11).toInt();
        const int newServiceId = serviceIdMap.value(oldServiceId, 0);
        if (oldServiceId != 0 && newServiceId <= 0) {
            if (errorText) *errorText = QStringLiteral("Cached part service is missing from hydrated order");
            return false;
        }
        insPart.addBindValue(newServiceId);
        for (int i = 12; i <= 21; ++i) insPart.addBindValue(readPart.value(i));
        for (int i = 23; i <= 37; ++i) insPart.addBindValue(readPart.value(i));
        if (!insPart.exec() || insPart.numRowsAffected() != 1) {
            if (errorText) *errorText = QStringLiteral("Cached part insert failed: %1").arg(insPart.lastError().text());
            return false;
        }
    }
    if (readPart.lastError().isValid()) {
        if (errorText) *errorText = readPart.lastError().text();
        return false;
    }
    return true;
}

bool copyCachedOutsource_local(QSqlDatabase database,
                               const QString& routerReadUid,
                               const QString& safeOrderUid,
                               int orderNumber,
                               const QMap<int, int>& serviceIdMap,
                               QString* errorText)
{
    QSqlQuery readOut(database);
    readOut.prepare(QStringLiteral(
        "SELECT COALESCE(sent_at,''), COALESCE(company,''), COALESCE(part_name,''), COALESCE(repair_cost,0), "
        "COALESCE(client_price,0), COALESCE(note,''), COALESCE(created_at,''), COALESCE(service_id,0) "
        "FROM router_cache_outsource_repairs WHERE order_uid=? ORDER BY id ASC"));
    readOut.addBindValue(routerReadUid);
    if (!readOut.exec()) {
        if (errorText) *errorText = readOut.lastError().text();
        return false;
    }
    while (readOut.next()) {
        const int newServiceId = serviceIdMap.value(readOut.value(7).toInt(), 0);
        QSqlQuery insOut(database);
        insOut.prepare(QStringLiteral(
            "INSERT INTO outsource_repairs(order_uid, order_number, sent_at, company, part_name, repair_cost, client_price, note, created_at, service_id) "
            "VALUES(?,?,?,?,?,?,?,?,?,?)"));
        insOut.addBindValue(safeOrderUid);
        insOut.addBindValue(orderNumber);
        for (int i = 0; i <= 6; ++i) insOut.addBindValue(readOut.value(i));
        insOut.addBindValue(newServiceId);
        if (!insOut.exec()) {
            if (errorText) *errorText = insOut.lastError().text();
            return false;
        }

        if (newServiceId > 0) {
            QSqlQuery insJob(database);
            insJob.prepare(QStringLiteral(
                "INSERT INTO outsource_jobs(order_uid, order_number, service_id, company_id, company_name, sent_date, return_date, part_name, company_price, markup_percent, client_price, note) "
                "VALUES(?,?,?,?,?,?,?,?,?,?,?,?)"));
            insJob.addBindValue(safeOrderUid);
            insJob.addBindValue(orderNumber);
            insJob.addBindValue(newServiceId);
            insJob.addBindValue(0);
            insJob.addBindValue(readOut.value(1).toString());
            insJob.addBindValue(readOut.value(0).toString());
            insJob.addBindValue(QString());
            insJob.addBindValue(readOut.value(2).toString());
            insJob.addBindValue(readOut.value(3));
            insJob.addBindValue(0);
            insJob.addBindValue(readOut.value(4));
            insJob.addBindValue(readOut.value(5).toString());
            if (!insJob.exec()) {
                if (errorText) *errorText = insJob.lastError().text();
                return false;
            }
        }
    }
    return true;
}

} // namespace

static bool hydrateCachedOrderBody_local(QSqlDatabase database,
                                const QString& requestedCanonicalUid,
                                int orderNumber,
                                QString* errorText)
{
    const QString requestedUid = requestedCanonicalUid.trimmed();

    QSqlQuery readOrder(database);
    readOrder.prepare(QStringLiteral(
        "SELECT number, COALESCE(order_uid,''), COALESCE(order_number,number), "
        "COALESCE(order_year, CAST(strftime('%Y', (CASE WHEN substr(trim(COALESCE(opened_at,'')),3,1)='.' AND substr(trim(COALESCE(opened_at,'')),6,1)='.' THEN CASE WHEN substr(trim(COALESCE(opened_at,'')),7,4) GLOB '[0-9][0-9][0-9][0-9]' THEN substr(trim(COALESCE(opened_at,'')),7,4) ELSE '20'||substr(trim(COALESCE(opened_at,'')),7,2) END||'-'||substr(trim(COALESCE(opened_at,'')),4,2)||'-'||substr(trim(COALESCE(opened_at,'')),1,2) ELSE COALESCE(opened_at,'') END)) AS INTEGER)), "
        "opened_at, closed_at, status, plate, plate_norm, vin, car_year, odometer, engine_type, engine_volume, "
        "fuel_type, car, client, client_phone, master_id, works_sum, parts_sum, discount, master_name_closed, "
        "car_model, car_brand, car_kind, color, car_color, owner_name, discount_mode "
        "FROM router_cache_work_orders WHERE number=?"));
    readOrder.addBindValue(orderNumber);
    if (!readOrder.exec() || !readOrder.next()) {
        if (errorText) *errorText = readOrder.lastError().text().isEmpty() ? QStringLiteral("Cached order row not found") : readOrder.lastError().text();
        return false;
    }

    const QString routerReadUid = readOrder.value(1).toString();
    const QString safeOrderUid = routerReadUid;
    auto refuse = [&](const QString& reason) {
        if (errorText) *errorText = reason;
        return false;
    };
    if (orderNumber <= 0 || routerReadUid.trimmed().isEmpty()
        || routerReadUid != routerReadUid.trimmed()
        || (!requestedUid.isEmpty() && requestedUid != routerReadUid)
        || readOrder.value(2).toInt() <= 0 || readOrder.value(3).toInt() <= 0) {
        return refuse(QStringLiteral("Cached order canonical identity is invalid or conflicts with requested UID"));
    }
    QSqlQuery existing(database);
    existing.prepare(QStringLiteral(
        "SELECT number,order_uid,order_number,order_year FROM work_orders "
        "WHERE number=? OR order_uid=? OR (order_year=? AND order_number=?)"));
    existing.addBindValue(orderNumber);
    existing.addBindValue(routerReadUid);
    existing.addBindValue(readOrder.value(3));
    existing.addBindValue(readOrder.value(2));
    if (!existing.exec()) return refuse(existing.lastError().text());
    bool alreadyPresent = false;
    while (existing.next()) {
        if (alreadyPresent || existing.value(0).toInt() != orderNumber
            || existing.value(1).toString() != routerReadUid
            || existing.value(2).toInt() != readOrder.value(2).toInt()
            || existing.value(3).toInt() != readOrder.value(3).toInt())
            return refuse(QStringLiteral("Existing order canonical identity conflicts with cache"));
        alreadyPresent = true;
    }
    if (existing.lastError().isValid()) return refuse(existing.lastError().text());

    // Both stored parent references must identify the same canonical order.
    for (const QString& table : {QStringLiteral("router_cache_work_order_services"),
                                  QStringLiteral("router_cache_work_order_parts"),
                                  QStringLiteral("router_cache_outsource_repairs")}) {
        QSqlQuery parent(database);
        parent.prepare(QStringLiteral(
            "SELECT 1 FROM %1 WHERE (order_uid=? OR order_number=?) "
            "AND (COALESCE(order_uid,'')<>? OR COALESCE(order_number,0)<>?) LIMIT 1").arg(table));
        parent.addBindValue(routerReadUid);
        parent.addBindValue(orderNumber);
        parent.addBindValue(routerReadUid);
        parent.addBindValue(orderNumber);
        if (!parent.exec()) return refuse(parent.lastError().text());
        if (parent.next()) return refuse(QStringLiteral("Cached child has conflicting parent identity: %1").arg(table));
        if (parent.lastError().isValid()) return refuse(parent.lastError().text());
    }
    QSqlQuery duplicatePart(database);
    duplicatePart.prepare(QStringLiteral(
        "SELECT 1 FROM router_cache_work_order_parts selected "
        "JOIN router_cache_work_order_parts other ON other.part_uid=selected.part_uid AND other.id<>selected.id "
        "WHERE selected.order_uid=? OR selected.order_number=? LIMIT 1"));
    duplicatePart.addBindValue(routerReadUid);
    duplicatePart.addBindValue(orderNumber);
    if (!duplicatePart.exec()) return refuse(duplicatePart.lastError().text());
    if (duplicatePart.next()) return refuse(QStringLiteral("Cached part UID is claimed by multiple rows"));
    if (duplicatePart.lastError().isValid()) return refuse(duplicatePart.lastError().text());

    QSet<QString> cachedPartUids;
    QSqlQuery parts(database);
    parts.prepare(QStringLiteral(
        "SELECT part_uid,order_uid,order_number FROM router_cache_work_order_parts "
        "WHERE order_uid=? OR order_number=?"));
    parts.addBindValue(routerReadUid);
    parts.addBindValue(orderNumber);
    if (!parts.exec()) return refuse(parts.lastError().text());
    while (parts.next()) {
        const QString partUid = parts.value(0).toString();
        if (partUid.trimmed().isEmpty() || partUid != partUid.trimmed()
            || cachedPartUids.contains(partUid)
            || parts.value(1).toString() != routerReadUid || parts.value(2).toInt() != orderNumber)
            return refuse(QStringLiteral("Cached part identity is missing, duplicated or has conflicting parent"));
        cachedPartUids.insert(partUid);
        QSqlQuery owner(database);
        owner.prepare(QStringLiteral("SELECT order_uid,order_number FROM work_order_parts WHERE part_uid=?"));
        owner.addBindValue(partUid);
        if (!owner.exec()) return refuse(owner.lastError().text());
        int matches = 0;
        while (owner.next()) {
            ++matches;
            if (!alreadyPresent || matches > 1 || owner.value(0).toString() != routerReadUid
                || owner.value(1).toInt() != orderNumber)
                return refuse(QStringLiteral("Cached part UID already belongs to another working identity"));
        }
        if (owner.lastError().isValid()) return refuse(owner.lastError().text());
        if (alreadyPresent && matches != 1)
            return refuse(QStringLiteral("Existing working order has an incomplete part UID set"));
    }
    if (parts.lastError().isValid()) return refuse(parts.lastError().text());
    if (alreadyPresent) {
        QSqlQuery localParts(database);
        localParts.prepare(QStringLiteral(
            "SELECT part_uid,order_uid,order_number FROM work_order_parts WHERE order_uid=? OR order_number=?"));
        localParts.addBindValue(routerReadUid);
        localParts.addBindValue(orderNumber);
        if (!localParts.exec()) return refuse(localParts.lastError().text());
        while (localParts.next()) {
            if (!cachedPartUids.remove(localParts.value(0).toString())
                || localParts.value(1).toString() != routerReadUid || localParts.value(2).toInt() != orderNumber)
                return refuse(QStringLiteral("Existing working part graph conflicts with cache"));
        }
        if (localParts.lastError().isValid()) return refuse(localParts.lastError().text());
        if (!cachedPartUids.isEmpty()) return refuse(QStringLiteral("Existing working part graph is incomplete"));
        // An existing canonical working copy is authoritative; never overwrite its edits on retry.
        return true;
    }
    const QString safeMasterId = ensureMasterExists_local(database, readOrder.value(18).toString(), readOrder.value(22).toString());
    if (safeMasterId.trimmed().isEmpty()) {
        if (errorText) *errorText = QStringLiteral("Failed to ensure order master for cached order %1").arg(orderNumber);
        return false;
    }

    QSqlQuery insOrder(database);
    insOrder.prepare(QStringLiteral(
        "INSERT INTO work_orders(number, order_uid, order_number, order_year, opened_at, closed_at, status, plate, plate_norm, vin, car_year, odometer, engine_type, engine_volume, fuel_type, car, client, client_phone, master_id, works_sum, parts_sum, discount, master_name_closed, car_model, car_brand, car_kind, color, car_color, owner_name, discount_mode) "
        "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)"));
    insOrder.addBindValue(readOrder.value(0));
    insOrder.addBindValue(safeOrderUid);
    insOrder.addBindValue(readOrder.value(2));
    insOrder.addBindValue(readOrder.value(3));
    for (int i = 4; i <= 17; ++i) insOrder.addBindValue(readOrder.value(i));
    insOrder.addBindValue(safeMasterId);
    for (int i = 19; i <= 29; ++i) insOrder.addBindValue(readOrder.value(i));
    if (!insOrder.exec() || insOrder.numRowsAffected() != 1) {
        if (errorText) *errorText = QStringLiteral("Cached order insert failed: %1").arg(insOrder.lastError().text());
        return false;
    }

    QMap<int, int> serviceIdMap;
    if (!copyCachedServices_local(database, routerReadUid, safeOrderUid, orderNumber, &serviceIdMap, errorText)) return false;
    if (!copyCachedServiceMasters_local(database, routerReadUid, orderNumber, serviceIdMap, errorText)) return false;
    if (!copyCachedParts_local(database, routerReadUid, safeOrderUid, orderNumber, serviceIdMap, errorText)) return false;
    if (!copyCachedOutsource_local(database, routerReadUid, safeOrderUid, orderNumber, serviceIdMap, errorText)) return false;

    return true;
}

bool hydrateCachedOrderToActive(QSqlDatabase database,
                                const QString& requestedCanonicalUid,
                                int orderNumber,
                                QString* errorText)
{
    if (errorText) errorText->clear();
    if (!database.isValid() || !database.isOpen()) {
        if (errorText) *errorText = QStringLiteral("Hydration requires an open database");
        return false;
    }
    QSqlQuery boundary(database);
    if (!boundary.exec(QStringLiteral("SAVEPOINT archive_hydration"))) {
        if (errorText) *errorText = boundary.lastError().text();
        return false;
    }
    const bool copied = routerCacheContentReady(database, errorText)
        && hydrateCachedOrderBody_local(database, requestedCanonicalUid, orderNumber, errorText);
    if (copied && boundary.exec(QStringLiteral("RELEASE SAVEPOINT archive_hydration"))) return true;
    if (copied && errorText) *errorText = boundary.lastError().text();
    const bool rolledBack = boundary.exec(QStringLiteral("ROLLBACK TO SAVEPOINT archive_hydration"));
    const QString rollbackError = boundary.lastError().text();
    const bool released = boundary.exec(QStringLiteral("RELEASE SAVEPOINT archive_hydration"));
    if ((!rolledBack || !released) && errorText)
        *errorText += QStringLiteral("; hydration rollback failed: %1; %2").arg(rollbackError, boundary.lastError().text());
    return false;
}

bool copyCachedOrderToActive(QSqlDatabase database,
                             const QString& requestedCanonicalUid,
                             int orderNumber,
                             QString* errorText)
{
    return hydrateCachedOrderToActive(database, requestedCanonicalUid, orderNumber, errorText);
}
