#pragma once
#include "infrastructure/db/sqlite/SqliteSchemaInspection.h"
#include <QSqlError>
#include <QStringList>

namespace MigrationDestinationBaseline {
inline bool capture(QSqlDatabase db, const QString& sourceIdentity, QString* error)
{
    QSqlQuery q(db);
    if (!q.exec("CREATE TABLE IF NOT EXISTS migration_destination_counts(source_identity TEXT NOT NULL,entity TEXT NOT NULL,rows_before INTEGER NOT NULL CHECK(rows_before>=0),PRIMARY KEY(source_identity,entity))")) {
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
    }
    return true;
}

// A negative result means the baseline cannot support verification.
inline qint64 before(QSqlDatabase db, const QString& entity)
{
    if (SqliteSchemaInspection::tableAbsent(db,"main","migration_prepared_source")) return 0;
    QSqlQuery q(db);
    q.prepare("SELECT c.rows_before FROM migration_destination_counts c JOIN migration_prepared_source p "
              "ON p.singleton=1 AND p.source_identity=c.source_identity WHERE c.entity=?");
    q.addBindValue(entity);
    if (!q.exec() || !q.next()) return -1;
    bool ok=false;
    const qint64 count=q.value(0).toLongLong(&ok);
    return ok && count>=0 && !q.next() && !q.lastError().isValid() ? count : -1;
}
}
