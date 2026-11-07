#pragma once

#include "infrastructure/db/sqlite/OrderArchiveShardSqlSupport.h"
#include "infrastructure/db/sqlite/DbFacadeSupport.h"
#include "infrastructure/db/sqlite/SqliteSchemaInspection.h"
#include <QScopeGuard>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

// Materialize an immutable canonical archive into the isolated workspace. The
// source is main so the existing exact graph copier/verifier can be reused.
// Current workspace copies are authoritative; never overwrite their newer edits.
namespace MigrationArchiveWorkspace {
inline bool merge(const QString& archive, const QString& workspace, QString* error, QSet<QString>* materialized = nullptr)
{
    auto fail=[&](const QString& detail){if(error)*error="Previous archive reconciliation: "+detail;return false;};
    const QString name=QUuid::createUuid().toString();
    auto db=QSqlDatabase::addDatabase("QSQLITE",name);
    const auto close=qScopeGuard([&]{db.close();db={};QSqlDatabase::removeDatabase(name);});
    db.setDatabaseName(archive);
    if(!QFileInfo::exists(archive))return fail("Previous archive file is missing");
    // SQLite inherits READONLY into ATTACH; use a writable connection, but all
    // main-schema statements below and in the graph copier are strictly reads.
    if(!db.open())return fail(db.lastError().text());
    QSqlQuery q(db);
    q.prepare("ATTACH DATABASE ? AS archive_rollover");q.addBindValue(workspace);
    if(!q.exec())return fail(q.lastError().text());q.finish();
    if(!q.exec("SELECT number,order_uid,order_number,order_year FROM main.work_orders ORDER BY number"))return fail(q.lastError().text());
    struct Order { int key; QString uid; int visible; int year; };
    QList<Order> orders;
    while(q.next()) {
        Order order{q.value(0).toInt(),q.value(1).toString(),q.value(2).toInt(),q.value(3).toInt()};
        if(order.key<=0 || order.uid.trimmed().isEmpty() || order.visible<=0 || order.year<=0)return fail("Invalid historical identity");
        orders.append(order);
    }
    if(q.lastError().isValid())return fail(q.lastError().text());q.finish();
    if(!db.transaction())return fail(db.lastError().text());
    auto rollback=qScopeGuard([&]{db.rollback();});
    QSet<QString> inserted;
    for(const auto& order:orders) {
        q.prepare("SELECT number,order_uid,order_number,order_year FROM archive_rollover.work_orders WHERE number=? OR order_uid=?");
        q.addBindValue(order.key);q.addBindValue(order.uid);
        if(!q.exec())return fail(q.lastError().text());
        const bool exists=q.next();
        if(exists && (q.value(0).toInt()!=order.key || q.value(1).toString()!=order.uid || q.value(2).toInt()!=order.visible || q.value(3).toInt()!=order.year))
            return fail("Historical identity conflicts with workspace; explicit reconciliation required");
        if(exists && q.next())return fail("Ambiguous historical UID");
        if(q.lastError().isValid())return fail(q.lastError().text());q.finish();
        if(exists && materialized && materialized->contains(order.uid)
            && !OrderArchiveShardSqlSupport::verifyCopiedOrderGraph(db,order.key,error))return false;
        if(!exists) {
            // Do not let downstream normalization invent an absent master.
            q.prepare("SELECT sm.master_id FROM main.service_masters sm JOIN main.work_order_services s ON s.id=sm.service_id WHERE s.order_number=? AND NOT EXISTS(SELECT 1 FROM archive_rollover.masters m WHERE m.id=sm.master_id) LIMIT 1");
            q.addBindValue(order.key);
            if(!q.exec())return fail(q.lastError().text());
            if(q.next())return fail("Archived service master is absent from the workspace");
            if(q.lastError().isValid())return fail(q.lastError().text());q.finish();
            if(!OrderArchiveShardSqlSupport::copyCompleteOrderGraph(db,order.key,error))return false;
            inserted.insert(order.uid);
        }
        // Normal startup now routes the complete working graph by its stable UID.
        // The immutable shard remains available as historical evidence.
        q.prepare("INSERT INTO archive_rollover.workspace_order_state(order_number,order_uid,origin,source_db) VALUES(?,?,'native','') ON CONFLICT(order_number) DO UPDATE SET origin='native',source_db='' WHERE order_uid=excluded.order_uid");
        q.addBindValue(order.key);q.addBindValue(order.uid);
        if(!q.exec() || q.numRowsAffected()!=1)return fail("Workspace state identity conflict: "+q.lastError().text());q.finish();
    }
    if(!db.commit())return fail(db.lastError().text());
    rollback.dismiss();
    if(materialized)materialized->unite(inserted);
    return true;
}
}
