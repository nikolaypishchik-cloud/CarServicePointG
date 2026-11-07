#pragma once

#include "infrastructure/db/sqlite/OrderArchiveShardSqlSupport.h"
#include "infrastructure/db/sqlite/HistoricalRecordStore.h"
#include "infrastructure/db/sqlite/MutationJournalSchema.h"
#include "infrastructure/db/sqlite/DbFacadeSupport.h"
#include "infrastructure/db/sqlite/SqliteSchemaInspection.h"
#include <QScopeGuard>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

namespace MigrationArchiveCoverage {
inline QString ident(QString value) { return '"'+value.replace('"',"\"\"")+'"'; }
inline bool reconcile(QSqlDatabase db,QString* error,bool versioned=false)
{
    auto fail=[&](const QString& text){if(error)*error="Archive coverage: "+text;return false;};
    QSqlQuery q(db);
    // Graph slices must not omit or reparent an orphan during materialization.
    for(const auto& table:QStringList{"work_order_services","work_order_parts"}) {
        if(!q.exec("SELECT 1 FROM main."+ident(table)+" c LEFT JOIN main.work_orders o ON o.number=c.order_number WHERE o.number IS NULL OR COALESCE(c.order_uid,'')<>COALESCE(o.order_uid,'') LIMIT 1"))return fail(q.lastError().text());
        if(q.next())return fail("Orphan or conflicting owner in "+table);
        if(q.lastError().isValid())return fail(q.lastError().text());q.finish();
    }
    for(const auto& edge:QList<QPair<QString,QString>>{{"work_order_parts","service_id"},{"work_order_services","parent_id"}}) {
        q.prepare("SELECT 1 FROM pragma_table_info(?) WHERE name=?");q.addBindValue(edge.first);q.addBindValue(edge.second);
        if(!q.exec())return fail(q.lastError().text());const bool present=q.next();q.finish();
        if(!present)continue;
        if(!q.exec("SELECT 1 FROM main."+ident(edge.first)+" c LEFT JOIN main.work_order_services s ON s.id=c."+ident(edge.second)+" WHERE COALESCE(c."+ident(edge.second)+",0)>0 AND (s.id IS NULL OR s.order_number<>c.order_number OR COALESCE(s.order_uid,'')<>COALESCE(c.order_uid,'')) LIMIT 1"))return fail(q.lastError().text());
        if(q.next())return fail("Conflicting service owner in "+edge.first);
        if(q.lastError().isValid())return fail(q.lastError().text());q.finish();
    }
    if(!q.exec("SELECT 1 FROM main.service_masters m LEFT JOIN main.work_order_services s ON s.id=m.service_id WHERE s.id IS NULL LIMIT 1"))return fail(q.lastError().text());
    if(q.next())return fail("Orphan service-master link");
    if(q.lastError().isValid())return fail(q.lastError().text());q.finish();
    struct Table {QString name,ddl;};QList<Table> tables;
    if(!q.exec("SELECT name,sql FROM main.sqlite_master WHERE type='table' AND name NOT GLOB 'sqlite_*'"))return fail(q.lastError().text());
    while(q.next())tables.append({q.value(0).toString(),q.value(1).toString()});
    if(q.lastError().isValid())return fail(q.lastError().text());q.finish();
    for(const auto& table:tables)if(table.name=="order_registry") {
        if(!q.exec("SELECT 1 FROM main.order_registry r JOIN main.work_orders w ON w.number=r.order_number WHERE COALESCE(r.order_uid,'')<>COALESCE(w.order_uid,'') OR r.visible_order_number<>w.order_number OR r.order_year<>w.order_year LIMIT 1"))return fail(q.lastError().text());
        if(q.next())return fail("Historical registry identity conflicts with its order");
        if(q.lastError().isValid())return fail(q.lastError().text());q.finish();
    }
    const QSet<QString> graph={"work_orders","work_order_services","work_order_parts","service_masters","work_order_part_attachments","work_order_service_attachments","outsource_jobs","outsource_repairs"};
    const QSet<QString> derived={"order_registry","part_registry","workspace_order_state"};
    for(const auto& table:graph) {
        QStringList sourceColumns;QSet<QString> targetColumns;
        if(!q.exec("PRAGMA main.table_info("+ident(table)+")"))return fail(q.lastError().text());
        while(q.next())sourceColumns<<q.value(1).toString();
        if(q.lastError().isValid())return fail(q.lastError().text());q.finish();
        if(sourceColumns.isEmpty())continue;
        if(!q.exec("SELECT 1 FROM main."+ident(table)+" LIMIT 1"))return fail(q.lastError().text());
        const bool populated=q.next();q.finish();if(!populated)continue;
        if(!q.exec("PRAGMA archive_rollover.table_info("+ident(table)+")"))return fail(q.lastError().text());
        while(q.next())targetColumns.insert(q.value(1).toString());
        if(q.lastError().isValid())return fail(q.lastError().text());q.finish();
        for(const auto& column:sourceColumns)if(!versioned && !targetColumns.contains(column))return fail("Unrepresented historical field: "+table+"."+column);
    }
    for(const auto& table:tables) {
        if(graph.contains(table.name) || derived.contains(table.name) || table.name.startsWith("router_"))continue;
        QStringList columns,expressions;QSet<QString> names;bool linked=false;
        if(!q.exec("PRAGMA main.table_info("+ident(table.name)+")"))return fail(q.lastError().text());
        while(q.next()) {
            const auto name=q.value(1).toString();names.insert(name);columns<<ident(name);
            expressions<<"typeof("+ident(name)+"),"+ident(name)+" COLLATE BINARY";
            linked|=QStringList{"order_uid","order_number","work_order","work_order_id","work_order_number","service_id","part_id"}.contains(name);
        }
        if(q.lastError().isValid())return fail(q.lastError().text());q.finish();
        if(!linked)continue; // Non-order domains remain in the immutable original.
        if(!q.exec("SELECT 1 FROM main."+ident(table.name)+" LIMIT 1"))return fail(q.lastError().text());
        const bool populated=q.next();if(q.lastError().isValid())return fail(q.lastError().text());q.finish();
        if(!populated)continue;
        // A deletion event may outlive its order. Its recorded UID and typed fields
        // belong to the captured source and must never recreate an active order.
        if(versioned && table.name==mutationJournalTableName())continue;
        if(versioned && table.name.startsWith("parts_catalog"))continue; // Catalogue links are source-scoped evidence, not current owners.
        if(names.contains("order_uid")) {
            if(!q.exec("SELECT 1 FROM main."+ident(table.name)+" c WHERE COALESCE(c.order_uid,'')<>'' AND (NOT EXISTS(SELECT 1 FROM main.work_orders s WHERE s.order_uid=c.order_uid) OR NOT EXISTS(SELECT 1 FROM archive_rollover.work_orders t WHERE t.order_uid=c.order_uid)) LIMIT 1"))return fail(q.lastError().text());
            if(q.next())return fail("Supplemental history has an unresolved owner: "+table.name);
            if(q.lastError().isValid())return fail(q.lastError().text());q.finish();
        }
        if(versioned)continue; // Supplemental records are read from their immutable version, never replayed into current accounting.
        q.prepare("SELECT 1 FROM archive_rollover.sqlite_master WHERE type='table' AND name=?");q.addBindValue(table.name);
        if(!q.exec())return fail(q.lastError().text());const bool exists=q.next();q.finish();
        if(!exists) {
            // Unknown executable schema needs a deliberate adapter, not a partial clone.
            q.prepare("SELECT 1 FROM main.sqlite_master WHERE tbl_name=? AND (type='trigger' OR (type='index' AND sql IS NOT NULL)) LIMIT 1");q.addBindValue(table.name);
            if(!q.exec())return fail(q.lastError().text());
            const bool executable=q.next();if(q.lastError().isValid())return fail(q.lastError().text());q.finish();
            if(executable)return fail("Supplemental table has executable/index schema requiring review: "+table.name);
            const int body=table.ddl.indexOf('(');
            if(body<0 || !table.ddl.startsWith("CREATE TABLE",Qt::CaseInsensitive))return fail("Unsupported supplemental table: "+table.name);
            if(!q.exec("CREATE TABLE archive_rollover."+ident(table.name)+table.ddl.mid(body)))return fail(q.lastError().text());
            const auto fields=columns.join(',');
            if(!q.exec("INSERT INTO archive_rollover."+ident(table.name)+"("+fields+") SELECT "+fields+" FROM main."+ident(table.name)))return fail(q.lastError().text());
        }
        // Every source tuple (including duplicate multiplicity, NULL, BLOB and
        // storage class) must already exist. Never update conflicting history.
        const auto fields=expressions.join(',');
        const auto source="SELECT "+fields+",COUNT(*) FROM main."+ident(table.name)+" GROUP BY "+fields;
        const auto target="SELECT "+fields+",COUNT(*) FROM archive_rollover."+ident(table.name)+" GROUP BY "+fields;
        if(!q.exec(source+" EXCEPT "+target))return fail(table.name+": "+q.lastError().text());
        if(q.next())return fail("Unreconciled supplemental history: "+table.name);
        if(q.lastError().isValid())return fail(q.lastError().text());q.finish();
    }
    if(!q.exec("PRAGMA archive_rollover.foreign_key_check"))return fail(q.lastError().text());
    if(q.next())return fail("Destination foreign-key violation after archive reconciliation");
    if(q.lastError().isValid())return fail(q.lastError().text());
    return true;
}
}

// Materialize an immutable canonical archive into the isolated workspace. The
// source is main so the existing exact graph copier/verifier can be reused.
// Current workspace copies are authoritative; never overwrite their newer edits.
namespace MigrationArchiveWorkspace {
// A current parent is not proof that all historical children are represented.
// Only stable child UIDs establish identity; neither physical keys nor values do.
inline bool verifyWorkingChildren(QSqlDatabase db,int number,const QString& owner,QString* error,bool versioned=false)
{
    auto fail=[&](const QString& table,const QString& uid,const QString& reason){
        if(error)*error=QString("Historical child ambiguity: owner=%1 table=%2 uid=%3: %4; explicit decision required before publication").arg(owner,table,uid,reason);
        return false;
    };
    for(const auto& table:QStringList{"work_order_services","work_order_parts"}) {
        const QString uidColumn=table=="work_order_parts" ? "part_uid" : "service_uid";
        QSet<QString> sourceColumns,targetColumns;QSqlQuery q(db);
        for(const auto& schema:QStringList{"main","archive_rollover"}) {
            if(!q.exec("PRAGMA "+schema+".table_info("+MigrationArchiveCoverage::ident(table)+")"))return fail(table,{},q.lastError().text());
            while(q.next())(schema=="main" ? sourceColumns : targetColumns).insert(q.value(1).toString());
            if(q.lastError().isValid())return fail(table,{},q.lastError().text());q.finish();
        }
        q.prepare("SELECT COUNT(*) FROM main."+table+" WHERE order_number=?");q.addBindValue(number);
        if(!q.exec() || !q.next())return fail(table,{},q.lastError().text());const auto count=q.value(0).toLongLong();q.finish();
        if(count==0)continue;
        if(!sourceColumns.contains(uidColumn) || !targetColumns.contains(uidColumn))return fail(table,{},"Stable child UID column is missing");
        QSqlQuery children(db);children.prepare("SELECT "+uidColumn+",order_uid FROM main."+table+" WHERE order_number=?");children.addBindValue(number);
        if(!children.exec())return fail(table,{},children.lastError().text());
        while(children.next()) {
            const auto uid=children.value(0).toString();
            if(uid.trimmed().isEmpty() || children.value(1).toString()!=owner)return fail(table,uid,"Unresolved source owner or UID");
            q.prepare("SELECT COUNT(*) FROM main."+table+" WHERE "+uidColumn+"=?");q.addBindValue(uid);
            if(!q.exec() || !q.next() || q.value(0).toLongLong()!=1)return fail(table,uid,"Source child UID is not unique");q.finish();
            q.prepare("SELECT order_uid,order_number FROM archive_rollover."+table+" WHERE "+uidColumn+"=?");q.addBindValue(uid);
            if(!q.exec())return fail(table,uid,q.lastError().text());
            if(!q.next()) {
                if(q.lastError().isValid())return fail(table,uid,q.lastError().text());
                if(versioned){q.finish();continue;}
                return fail(table,uid,"Historical child UID is absent from current workspace");
            }
            if(q.value(0).toString()!=owner || q.value(1).toInt()!=number)return fail(table,uid,"Current child belongs to another owner");
            if(q.next() || q.lastError().isValid())return fail(table,uid,"Current child UID is not unique");q.finish();
            if(versioned)continue; // Historical payment/link fields remain in their own source-scoped record.
            if(sourceColumns.contains("paid")) {
                if(!targetColumns.contains("paid"))return fail(table,uid,"Historical payment field is unrepresented");
                q.prepare("SELECT 1 FROM main."+table+" s JOIN archive_rollover."+table+" t ON t."+uidColumn+"=s."+uidColumn+" WHERE s."+uidColumn+"=? AND (typeof(s.paid)<>typeof(t.paid) OR NOT(s.paid IS t.paid COLLATE BINARY))");q.addBindValue(uid);
                if(!q.exec())return fail(table,uid,q.lastError().text());
                if(q.next())return fail(table,uid,"Historical paid value differs");
                if(q.lastError().isValid())return fail(table,uid,q.lastError().text());q.finish();
            }
            const QString relation=table=="work_order_parts" ? "service_id" : "parent_id";
            if(!sourceColumns.contains(relation))continue;
            if(!targetColumns.contains(relation))return fail(table,uid,"Historical service link is unrepresented");
            q.prepare("SELECT COALESCE(s."+relation+",0),ss.service_uid,COALESCE(t."+relation+",0),ts.service_uid,ss.order_uid,ts.order_uid FROM main."+table+" s JOIN archive_rollover."+table+" t ON t."+uidColumn+"=s."+uidColumn+" LEFT JOIN main.work_order_services ss ON ss.id=s."+relation+" LEFT JOIN archive_rollover.work_order_services ts ON ts.id=t."+relation+" WHERE s."+uidColumn+"=?");q.addBindValue(uid);
            if(!q.exec() || !q.next())return fail(table,uid,q.lastError().text());
            const auto sourceKey=q.value(0).toLongLong(),targetKey=q.value(2).toLongLong();
            const bool bothEmpty=sourceKey==0 && targetKey==0;
            const bool sameService=sourceKey>0 && targetKey>0 && !q.value(1).toString().isEmpty()
                && q.value(1)==q.value(3) && q.value(4).toString()==owner && q.value(5).toString()==owner;
            if(!bothEmpty && !sameService)return fail(table,uid,"Conflicting service owner or stable service UID");
            if(q.next() || q.lastError().isValid())return fail(table,uid,"Ambiguous service reference");q.finish();
        }
        if(children.lastError().isValid())return fail(table,{},children.lastError().text());
    }
    return true;
}
inline bool merge(const QString& archive, const QString& workspace, QString* error, QSet<QString>* materialized = nullptr, const QString& sourceOrigin = {})
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
    QString versionId;
    if(!sourceOrigin.isEmpty() && !HistoricalRecordStore::capture(db,sourceOrigin,HistoricalRecordStore::digestFile(archive),&versionId,error))return false;
    const bool versioned=!versionId.isEmpty();
    for(const auto& order:orders) {
        q.prepare("SELECT number,order_uid,order_number,order_year FROM archive_rollover.work_orders WHERE number=? OR order_uid=?");
        q.addBindValue(order.key);q.addBindValue(order.uid);
        if(!q.exec())return fail(q.lastError().text());
        const bool exists=q.next();
        if(exists && (q.value(0).toInt()!=order.key || q.value(1).toString()!=order.uid || q.value(2).toInt()!=order.visible || q.value(3).toInt()!=order.year))
            return fail("Historical identity conflicts with workspace; explicit reconciliation required");
        if(exists && q.next())return fail("Ambiguous historical UID");
        if(q.lastError().isValid())return fail(q.lastError().text());q.finish();
        if(exists && !verifyWorkingChildren(db,order.key,order.uid,error,versioned))return false;
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
    if(!MigrationArchiveCoverage::reconcile(db,error,versioned))return false;
    if(!db.commit())return fail(db.lastError().text());
    rollback.dismiss();
    if(materialized)materialized->unite(inserted);
    return true;
}
}
