#pragma once
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QSet>
#include <QStringList>
#include <QVariant>

namespace MigrationArchiveCoverage {
inline QString ident(QString value) { return '"'+value.replace('"',"\"\"")+'"'; }
inline bool reconcile(QSqlDatabase db,QString* error)
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
        for(const auto& column:sourceColumns)if(!targetColumns.contains(column))return fail("Unrepresented historical field: "+table+"."+column);
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
        if(names.contains("order_uid")) {
            if(!q.exec("SELECT 1 FROM main."+ident(table.name)+" c WHERE COALESCE(c.order_uid,'')<>'' AND (NOT EXISTS(SELECT 1 FROM main.work_orders s WHERE s.order_uid=c.order_uid) OR NOT EXISTS(SELECT 1 FROM archive_rollover.work_orders t WHERE t.order_uid=c.order_uid)) LIMIT 1"))return fail(q.lastError().text());
            if(q.next())return fail("Supplemental history has an unresolved owner: "+table.name);
            if(q.lastError().isValid())return fail(q.lastError().text());q.finish();
        }
        q.prepare("SELECT 1 FROM archive_rollover.sqlite_master WHERE type='table' AND name=?");q.addBindValue(table.name);
        if(!q.exec())return fail(q.lastError().text());const bool exists=q.next();q.finish();
        if(!exists) {
            // Unknown executable schema needs a deliberate adapter, not a partial clone.
            q.prepare("SELECT 1 FROM main.sqlite_master WHERE tbl_name=? AND (type='trigger' OR (type='index' AND sql IS NOT NULL)) LIMIT 1");q.addBindValue(table.name);
            if(!q.exec())return fail(q.lastError().text());
            if(q.next())return fail("Supplemental table has executable/index schema requiring review: "+table.name);q.finish();
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
