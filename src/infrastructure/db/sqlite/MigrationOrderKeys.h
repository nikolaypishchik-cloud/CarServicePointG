#pragma once

#include "infrastructure/db/sqlite/DbFacadeSupport.h"
#include "infrastructure/db/sqlite/SqliteSchemaInspection.h"
#include <QFile>
#include <QMap>
#include <QScopeGuard>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <limits>

namespace MigrationOrderKeys {
using Map = QMap<qint64,qint64>;
inline bool fail(QString* error,const QString& message)
{
    if (error) *error = message;
    return false;
}
inline bool plan(QSqlDatabase workspace,const QString& attached,Map* mapping,QString* error)
{
    mapping->clear();
    const QString orders=sqlQuoteIdent(attached)+".work_orders";
    QSqlQuery q(workspace);
    if (!q.exec("SELECT MAX(number) FROM (SELECT number FROM main.work_orders UNION ALL SELECT number FROM "+orders+")") || !q.next())
        return fail(error,q.lastError().text());
    qint64 high=q.value(0).toLongLong();
    if (!q.exec("SELECT s.number FROM "+orders+" s JOIN main.work_orders t ON t.number=s.number ORDER BY s.number"))
        return fail(error,q.lastError().text());
    while (q.next()) {
        if (high>=std::numeric_limits<int>::max()) return fail(error,"No free internal order number");
        const auto key=q.value(0).toLongLong();
        if (key<=0 || mapping->contains(key)) return fail(error,"Invalid source order key");
        mapping->insert(key,++high);
    }
    return !q.lastError().isValid() || fail(error,q.lastError().text());
}
inline bool store(QSqlDatabase db,const QString& identity,const Map& keys,QString* error)
{
    QSqlQuery q(db);
    if (!q.exec("CREATE TABLE IF NOT EXISTS migration_order_key_map(source_identity TEXT NOT NULL,source_number INTEGER NOT NULL,target_number INTEGER NOT NULL,PRIMARY KEY(source_identity,source_number),UNIQUE(source_identity,target_number))"))
        return fail(error,q.lastError().text());
    for (auto it=keys.cbegin();it!=keys.cend();++it) {
        q.prepare("INSERT INTO migration_order_key_map VALUES(?,?,?)");
        q.addBindValue(identity);q.addBindValue(it.key());q.addBindValue(it.value());
        if (!q.exec()) return fail(error,q.lastError().text());
    }
    return true;
}
inline bool load(QSqlDatabase db,const QString& identity,Map* keys,QString* error)
{
    keys->clear(); QSqlQuery q(db);
    if (SqliteSchemaInspection::tableAbsent(db,"main","migration_order_key_map")) return true;
    q.prepare("SELECT source_number,target_number FROM migration_order_key_map WHERE source_identity=? ORDER BY source_number");
    q.addBindValue(identity);
    if (!q.exec()) return fail(error,q.lastError().text());
    while (q.next()) {
        const auto old=q.value(0).toLongLong(), dest=q.value(1).toLongLong();
        if (old<=0 || dest<=old || dest>std::numeric_limits<int>::max()) return fail(error,"Invalid persisted order key mapping");
        keys->insert(old,dest);
    }
    return !q.lastError().isValid() || fail(error,q.lastError().text());
}
// The input is an audited private copy. Projection always writes a separate file.
inline bool project(const QString& input,const QString& output,const Map& keys,QString* error)
{
    if (!QFile::copy(input,output)) return fail(error,"Cannot copy order projection");
    if (keys.isEmpty()) return true;
    const QString name=QUuid::createUuid().toString();
    auto db=QSqlDatabase::addDatabase("QSQLITE",name);
    const auto close=qScopeGuard([&]{db.close();db={};QSqlDatabase::removeDatabase(name);});
    db.setDatabaseName(output);
    if (!db.open()) return fail(error,db.lastError().text());
    QSqlQuery q(db);
    if (!q.exec("SELECT name,type FROM sqlite_master WHERE type IN ('table','trigger') AND name NOT GLOB 'sqlite_*'"))
        return fail(error,q.lastError().text());
    const QStringList known={"work_orders","work_order_services","work_order_parts","outsource_repairs","masters","service_masters","settings","expenses","parts_catalog","planning_calendar"};
    QStringList tables;
    while (q.next()) {
        const QString table=q.value(0).toString();
        if (q.value(1).toString()=="trigger" || !known.contains(table))
            return fail(error,"Cannot safely remap order references in "+table);
        tables<<table;
    }
    if (q.lastError().isValid()) return fail(error,q.lastError().text());
    if (!db.transaction()) return fail(error,db.lastError().text());
    auto rollback=qScopeGuard([&]{db.rollback();});
    auto orderCols=tableColumnsForDb(db,"work_orders");
    for (const auto& col : QStringList{"order_number","order_uid"}) {
        if (!orderCols.contains(col) && !q.exec("ALTER TABLE work_orders ADD COLUMN "+col+(col=="order_uid"?" TEXT":" INTEGER")))
            return fail(error,q.lastError().text());
    }
    for (auto it=keys.cbegin();it!=keys.cend();++it) {
        q.prepare("UPDATE work_orders SET order_number=CASE WHEN COALESCE(order_number,0)>0 THEN order_number ELSE number END, "
                  "order_uid=COALESCE(NULLIF(order_uid,''),'rewrite_'||number), number=? WHERE number=?");
        q.addBindValue(it.value());q.addBindValue(it.key());
        if (!q.exec() || q.numRowsAffected()!=1) return fail(error,"Cannot project source order: "+q.lastError().text());
        q.prepare("SELECT order_uid FROM work_orders WHERE number=?");
        q.addBindValue(it.value());
        if (!q.exec() || !q.next()) return fail(error,"Cannot read projected owner UID");
        const QString uid=q.value(0).toString();
        q.finish();
        for (const auto& child : QStringList{"work_order_services","work_order_parts","outsource_repairs"}) {
            if (!tables.contains(child)) continue;
            QSet<QString> cols;
            if (!q.exec("PRAGMA table_info("+sqlQuoteIdent(child)+")")) return fail(error,q.lastError().text());
            while (q.next()) cols.insert(q.value(1).toString());
            if (cols.isEmpty() || q.lastError().isValid()) return fail(error,"Cannot inspect source child schema");
            QStringList fields,oldMatch,otherParent;
            for (const auto& field : QStringList{"order_number","work_order","work_order_id","work_order_number"}) {
                if (!cols.contains(field)) continue;
                fields<<field;
                const auto quoted=sqlQuoteIdent(field);
                oldMatch<<quoted+"=:previous";
                otherParent<<"(COALESCE("+quoted+",0)<>0 AND "+quoted+"<>:previous)";
            }
            if (fields.isEmpty()) return fail(error,"Unknown order reference in "+child);
            if (cols.contains("order_uid"))
                otherParent<<"(COALESCE(order_uid,'')<>'' AND order_uid<>:uid)";
            q.prepare("SELECT 1 FROM "+sqlQuoteIdent(child)+" WHERE ("+oldMatch.join(" OR ")+") AND ("+otherParent.join(" OR ")+") LIMIT 1");
            q.bindValue(":previous",it.key());
            if (cols.contains("order_uid")) q.bindValue(":uid",uid);
            if (!q.exec()) return fail(error,q.lastError().text());
            if (q.next()) return fail(error,"Conflicting source parent references in "+child);
            if (q.lastError().isValid()) return fail(error,q.lastError().text());
            q.finish();
            for (const auto& field : fields) {
                q.prepare("UPDATE "+sqlQuoteIdent(child)+" SET "+sqlQuoteIdent(field)+"=? WHERE "+sqlQuoteIdent(field)+"=?");
                q.addBindValue(it.value());q.addBindValue(it.key());
                if (!q.exec()) return fail(error,q.lastError().text());
            }
        }
    }
    if (!q.exec("PRAGMA foreign_key_check") || q.next() || q.lastError().isValid())
        return fail(error,"Projected source has invalid foreign keys");
    if (!db.commit()) return fail(error,db.lastError().text());
    rollback.dismiss(); return true;
}
}
