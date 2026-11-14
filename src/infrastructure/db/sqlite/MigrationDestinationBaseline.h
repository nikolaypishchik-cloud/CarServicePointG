#pragma once
#include "infrastructure/db/sqlite/SqliteSchemaInspection.h"
#include "infrastructure/db/sqlite/MigrationRowOverlap.h"
#include <QSqlError>
#include <QStringList>

namespace MigrationDestinationBaseline {
inline bool capture(QSqlDatabase db, const QString& sourceIdentity, const QString& sourceSchema, QString* error)
{
    QSqlQuery q(db);
    if (!q.exec("CREATE TABLE IF NOT EXISTS migration_destination_counts(source_identity TEXT NOT NULL,entity TEXT NOT NULL,rows_before INTEGER NOT NULL CHECK(rows_before>=0),PRIMARY KEY(source_identity,entity))")) {
        if (error) *error=q.lastError().text();
        return false;
    }
    if (!q.exec("CREATE TABLE IF NOT EXISTS migration_shared_counts(source_identity TEXT NOT NULL,entity TEXT NOT NULL,shared INTEGER NOT NULL CHECK(shared>=0),PRIMARY KEY(source_identity,entity))")) {
        if (error) *error=q.lastError().text();
        return false;
    }
    for (const auto& entity : QStringList{"work_orders","work_order_services","work_order_parts","masters","outsource_repairs"}) {
        q.prepare(QString("INSERT INTO migration_destination_counts SELECT ?,?,COUNT(*) FROM %1 "
                          "WHERE 1 ON CONFLICT(source_identity,entity) DO UPDATE SET rows_before=excluded.rows_before").arg(entity));
        q.addBindValue(sourceIdentity);
        q.addBindValue(entity);
        if (!q.exec()) {
            if (error) *error=q.lastError().text();
            return false;
        }
        // Count exact pre-existing rows before any inserts. Keep the original destination count intact.
        QString count = QStringLiteral("SELECT 0");
        if (!SqliteSchemaInspection::tableAbsent(db,sourceSchema,entity))
            count = "SELECT COUNT(*) FROM "+sqlQuoteIdent(sourceSchema)+"."+sqlQuoteIdent(entity)+" s WHERE "
                +MigrationRowOverlap::match(db,sourceSchema,entity,"s");
        q.prepare("INSERT INTO migration_shared_counts(source_identity,entity,shared) VALUES(?,?,("+count+
                  ")) ON CONFLICT(source_identity,entity) DO UPDATE SET shared=excluded.shared");
        q.addBindValue(sourceIdentity);
        q.addBindValue(entity);
        if (!q.exec()) {
            if (error) *error=q.lastError().text();
            return false;
        }
    }
    return true;
}

// A negative result means the baseline cannot support verification.
inline qint64 before(QSqlDatabase db, const QString& entity)
{
    if (SqliteSchemaInspection::tableAbsent(db,"main","migration_prepared_source")) return 0;
    QSqlQuery q(db);
    const QString retained = SqliteSchemaInspection::tableAbsent(db,"main","migration_shared_counts")
        ? QStringLiteral("c.rows_before")
        : QStringLiteral("c.rows_before-COALESCE((SELECT shared FROM migration_shared_counts x WHERE x.entity=c.entity AND x.source_identity=c.source_identity),0)");
    q.prepare("SELECT "+retained+" FROM migration_destination_counts c JOIN migration_prepared_source p "
              "ON p.singleton=1 AND p.source_identity=c.source_identity WHERE c.entity=?");
    q.addBindValue(entity);
    if (!q.exec() || !q.next()) return -1;
    bool ok=false;
    const qint64 count=q.value(0).toLongLong(&ok);
    return ok && count>=0 && !q.next() && !q.lastError().isValid() ? count : -1;
}
}
