#include "infrastructure/db/sqlite/Db.h"
#include "infrastructure/db/sqlite/DbFacadeSupport.h"
#include "infrastructure/db/sqlite/DbArchiveInternals.h"
#include "infrastructure/db/sqlite/LegacyCanonicalImportNormalization.h"
#include "infrastructure/db/sqlite/LegacyCanonicalAncillaryImportSupport.h"
#include "infrastructure/db/sqlite/DbSchemaAuditSupport.h"
#include "infrastructure/db/sqlite/DbSettingsCacheSupport.h"
#include "infrastructure/db/sqlite/DbThreadConnectionSupport.h"
#include "infrastructure/db/sqlite/RuntimeDbPreflightValidator.h"
#include "infrastructure/db/sqlite/OrderArchiveShardSqlSupport.h"
#include "core/services/LegacyMutationLogSupport.h"
#include "core/services/OrderIdentityService.h"
#include "core/services/OrderStoragePolicyService.h"
#include "core/services/legacyimport/RuntimeRecoveryCoordinator.h"
#include "core/services/OrderArchiveRotationService.h"
#include "core/services/OrderDetailIntegrityValidator.h"
#include "core/services/MutationExecutor.h"
#include "core/services/legacyimport/LegacyOrderChildMapper.h"
#include "infrastructure/security/EncryptedStorage.h"
#include "core/backup/BackupSnapshotService.h"

#include <QCryptographicHash>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QTextStream>
#include <QUuid>
#include <QVariant>
#include <QVector>

#include <algorithm>
#include <limits>

namespace {

bool ensureSchemaDestructiveOperationSnapshot(const QString& entityName, QString* errorText = nullptr)
{
    SnapshotJobContext ctx;
    ctx.requestedBy = QStringLiteral("DbDiagnostics");
    ctx.source = QStringLiteral("schema_migration_guard");
    ctx.emergency = true;
    ctx.event.module = QStringLiteral("DbDiagnostics");
    ctx.event.entityType = QStringLiteral("schema");
    ctx.event.entityId = entityName;
    ctx.event.operationType = QStringLiteral("destructive_schema_operation_guard");
    ctx.event.userVisibleDescription = QStringLiteral("Snapshot before destructive schema operation: %1").arg(entityName);
    ctx.event.changedTables = {entityName};
    SnapshotMetadata meta;
    QString localError;
    const bool ok = BackupSnapshotService::createSnapshot(ctx, &meta, &localError);
    if (!ok) {
        if (errorText) {
            *errorText = QStringLiteral("Blocked destructive schema operation for %1 because pre-migration snapshot failed: %2")
                .arg(entityName, localError);
        }
        qWarning() << "DbDiagnostics: blocked destructive schema operation without validated snapshot" << entityName << localError;
        return false;
    }
    return true;
}


bool execSchemaGuardedSql(QSqlQuery& query, const QString& sql, const QString& operation, QString* errorText = nullptr)
{
    if (query.exec(sql)) return true;
    const QString err = query.lastError().text();
    qWarning() << "DbDiagnostics schema SQL failed" << operation << sql << err;
    if (errorText) *errorText = err;
    return false;
}

} // namespace

bool rebuildActiveServicesTableToOrderUid(QSqlDatabase database)
{
    if (!database.isValid()) return false;
    if (!database.isOpen() && !database.open()) return false;
    if (!tableExistsInDb(database, QStringLiteral("work_order_services"))) return true;

    dropLegacyCompatibilityViews(database);
    dropViewsReferencingTable(database, QStringLiteral("work_order_services"));
    const QSet<QString> cols = tableColumnsForDb(database, QStringLiteral("work_order_services"));
    const bool needsRebuild =
        !cols.contains(QStringLiteral("order_uid")) ||
        !cols.contains(QStringLiteral("note")) ||
        !cols.contains(QStringLiteral("paid")) ||
        !cols.contains(QStringLiteral("parent_id")) ||
        !cols.contains(QStringLiteral("service_uid")) ||
        !cols.contains(QStringLiteral("status")) ||
        !cols.contains(QStringLiteral("completed_at")) ||
        !cols.contains(QStringLiteral("completed_by")) ||
        !cols.contains(QStringLiteral("assigned_to")) ||
        !cols.contains(QStringLiteral("assigned_to_name")) ||
        !cols.contains(QStringLiteral("source")) ||
        !cols.contains(QStringLiteral("diagnostic_uid")) ||
        !cols.contains(QStringLiteral("source_suggestion_uid")) ||
        !cols.contains(QStringLiteral("metadata_json")) ||
        !cols.contains(QStringLiteral("created_at")) ||
        !cols.contains(QStringLiteral("updated_at")) ||
        !tableCreateSqlContains(database, QStringLiteral("work_order_services"), QStringLiteral("REFERENCES work_orders(order_uid)"));
    if (!needsRebuild) return true;
    QString schemaGuardError;
    if (!ensureSchemaDestructiveOperationSnapshot(QStringLiteral("work_order_services"), &schemaGuardError)) return false;

    QSqlQuery q(database);
    if (!execSchemaGuardedSql(q, QStringLiteral("PRAGMA foreign_keys = OFF"), QStringLiteral("disable_foreign_keys"))) return false;

    MutationExecutor::Request request;
    request.sourceModule = QStringLiteral("DbDiagnostics");
    request.entityType = QStringLiteral("schema");
    request.entityId = QStringLiteral("work_order_services");
    request.operation = QStringLiteral("rebuild_active_services_order_uid");
    request.description = QStringLiteral("Rebuild active work_order_services to canonical order_uid schema");
    request.changedTables = {QStringLiteral("work_order_services")};
    request.beginErrorFallback = QStringLiteral("Не вдалося розпочати перебудову work_order_services.");
    request.commitErrorFallback = QStringLiteral("Не вдалося завершити перебудову work_order_services.");

    const auto tx = MutationExecutor::execute(database, request, [&](QSqlDatabase& txDb, QString* txError) {
        QSqlQuery q(txDb);
        auto fail = [&](const QString& text) {
            QSqlQuery qr(txDb);
            qr.exec(QStringLiteral("DROP TABLE IF EXISTS work_order_services_new"));
            if (txError && txError->trimmed().isEmpty()) *txError = text;
            return false;
        };
        if (!q.exec(
                "CREATE TABLE IF NOT EXISTS work_order_services_new ("
                "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
                "  order_uid TEXT NOT NULL,"
                "  order_number INTEGER NOT NULL DEFAULT 0,"
                "  title TEXT NOT NULL,"
                "  qty REAL NOT NULL DEFAULT 1,"
                "  price REAL NOT NULL DEFAULT 0,"
                "  amount REAL NOT NULL DEFAULT 0,"
                "  note TEXT NOT NULL DEFAULT '',"
                "  paid INTEGER NOT NULL DEFAULT 1,"
                "  parent_id INTEGER,"
                "  service_uid TEXT NOT NULL DEFAULT '',"
                "  status TEXT NOT NULL DEFAULT 'open',"
                "  completed_at TEXT NOT NULL DEFAULT '',"
                "  completed_by TEXT NOT NULL DEFAULT '',"
                "  assigned_to TEXT NOT NULL DEFAULT '',"
                "  assigned_to_name TEXT NOT NULL DEFAULT '',"
                "  source TEXT NOT NULL DEFAULT '',"
                "  diagnostic_uid TEXT NOT NULL DEFAULT '',"
                "  source_suggestion_uid TEXT NOT NULL DEFAULT '',"
                "  metadata_json TEXT NOT NULL DEFAULT '{}',"
                "  created_at TEXT NOT NULL DEFAULT '',"
                "  updated_at TEXT NOT NULL DEFAULT '',"
                "  FOREIGN KEY(order_uid) REFERENCES work_orders(order_uid) ON DELETE CASCADE"
                ");")) {
            return fail(q.lastError().text());
        }
    const QString orderExpr = firstAvailableExpr(cols, {"order_number"}, "0");
    const QString uidExpr = cols.contains(QStringLiteral("order_uid"))
        ? QStringLiteral("COALESCE(NULLIF(TRIM(COALESCE(order_uid,'')),''), (SELECT NULLIF(TRIM(COALESCE(wo.order_uid,'')), '') FROM work_orders wo WHERE wo.number = %1), CASE WHEN COALESCE(%1,0)>0 THEN 'rewrite_' || %1 ELSE '' END)").arg(orderExpr)
        : QStringLiteral("COALESCE((SELECT NULLIF(TRIM(COALESCE(wo.order_uid,'')), '') FROM work_orders wo WHERE wo.number = %1), CASE WHEN COALESCE(%1,0)>0 THEN 'rewrite_' || %1 ELSE '' END)").arg(orderExpr);
    const QString titleExpr = nonNullTextSql(firstAvailableExpr(cols, {"title", "name"}, "''"));
    const QString qtyExpr = nonNullRealSql(firstAvailableExpr(cols, {"qty"}, "1"), QStringLiteral("1"));
    const QString priceExpr = nonNullRealSql(firstAvailableExpr(cols, {"price"}, "0"));
    const QString amountExpr = nonNullRealSql(firstAvailableExpr(cols, {"amount"}, QStringLiteral("COALESCE(qty,1)*COALESCE(price,0)")));
    const QString noteExpr = nonNullTextSql(firstAvailableExpr(cols, {"note"}, "''"));
    const QString paidExpr = QStringLiteral("CASE WHEN COALESCE(%1,1)<>0 THEN 1 ELSE 0 END").arg(firstAvailableExpr(cols, {"paid"}, "1"));
    const QString parentExpr = firstAvailableExpr(cols, {"parent_id"}, "NULL");
    const QString serviceUidExpr = nonNullTextSql(firstAvailableExpr(cols, {"service_uid"}, "''"));
    const QString statusExpr = nonNullTextSql(firstAvailableExpr(cols, {"status"}, "'open'"));
    const QString completedAtExpr = nonNullTextSql(firstAvailableExpr(cols, {"completed_at"}, "''"));
    const QString completedByExpr = nonNullTextSql(firstAvailableExpr(cols, {"completed_by"}, "''"));
    const QString assignedToExpr = nonNullTextSql(firstAvailableExpr(cols, {"assigned_to"}, "''"));
    const QString assignedToNameExpr = nonNullTextSql(firstAvailableExpr(cols, {"assigned_to_name"}, "''"));
    const QString sourceExpr = nonNullTextSql(firstAvailableExpr(cols, {"source"}, "''"));
    const QString diagnosticUidExpr = nonNullTextSql(firstAvailableExpr(cols, {"diagnostic_uid"}, "''"));
    const QString sourceSuggestionUidExpr = nonNullTextSql(firstAvailableExpr(cols, {"source_suggestion_uid"}, "''"));
    const QString metadataExpr = nonNullTextSql(firstAvailableExpr(cols, {"metadata_json"}, "'{}'"));
    const QString createdAtExpr = nonNullTextSql(firstAvailableExpr(cols, {"created_at"}, "''"));
    const QString updatedAtExpr = nonNullTextSql(firstAvailableExpr(cols, {"updated_at"}, "''"));
    const QString copySql = QStringLiteral(
        "INSERT INTO work_order_services_new(id, order_uid, order_number, title, qty, price, amount, note, paid, parent_id, service_uid, status, completed_at, completed_by, assigned_to, assigned_to_name, source, diagnostic_uid, source_suggestion_uid, metadata_json, created_at, updated_at) "
        "SELECT id, %1, COALESCE(%2,0), %3, %4, %5, %6, %7, %8, CASE WHEN COALESCE(%9,0)<=0 THEN NULL ELSE %9 END, %10, %11, %12, %13, %14, %15, %16, %17, %18, %19, %20, %21 "
        "FROM work_order_services WHERE COALESCE(%2,0) > 0")
        .arg(uidExpr, orderExpr, titleExpr, qtyExpr, priceExpr, amountExpr, noteExpr, paidExpr, parentExpr,
             serviceUidExpr, statusExpr, completedAtExpr, completedByExpr, assignedToExpr, assignedToNameExpr,
             sourceExpr, diagnosticUidExpr, sourceSuggestionUidExpr, metadataExpr, createdAtExpr, updatedAtExpr, firstAvailableExpr(cols, {"price_input_mode"}, "'unknown'"));
    if (!q.exec(copySql)) {
            return fail(q.lastError().text());
        }
        dropViewsReferencingTable(txDb, QStringLiteral("work_order_services"));
        if (!q.exec("DROP TABLE work_order_services") || !q.exec("ALTER TABLE work_order_services_new RENAME TO work_order_services")) {
            return fail(q.lastError().text());
        }
        return true;
    });
    QSqlQuery qi(database);
    if (!execSchemaGuardedSql(qi, QStringLiteral("PRAGMA foreign_keys = ON"), QStringLiteral("enable_foreign_keys"))) return false;
    if (!tx.ok) return false;
    if (!execSchemaGuardedSql(qi, QStringLiteral("CREATE INDEX IF NOT EXISTS idx_work_order_services_order_uid ON work_order_services(order_uid)"), QStringLiteral("index_work_order_services_order_uid"))) return false;
    if (!execSchemaGuardedSql(qi, QStringLiteral("CREATE INDEX IF NOT EXISTS idx_work_order_services_order_number ON work_order_services(order_number)"), QStringLiteral("index_work_order_services_order_number"))) return false;
    return true;
}

bool rebuildActiveOutsourceRepairsTableToOrderUid(QSqlDatabase database)
{
    if (!database.isValid()) return false;
    if (!database.isOpen() && !database.open()) return false;
    if (!tableExistsInDb(database, QStringLiteral("outsource_repairs"))) return true;
    dropLegacyCompatibilityViews(database);
    dropViewsReferencingTable(database, QStringLiteral("outsource_repairs"));
    const QSet<QString> cols = tableColumnsForDb(database, QStringLiteral("outsource_repairs"));
    const bool needsRebuild =
        !cols.contains(QStringLiteral("order_uid")) ||
        !cols.contains(QStringLiteral("service_id")) ||
        !tableCreateSqlContains(database, QStringLiteral("outsource_repairs"), QStringLiteral("REFERENCES work_orders(order_uid)"));
    if (!needsRebuild) return true;
    QString schemaGuardError;
    if (!ensureSchemaDestructiveOperationSnapshot(QStringLiteral("outsource_repairs"), &schemaGuardError)) return false;
    QSqlQuery q(database);
    if (!execSchemaGuardedSql(q, QStringLiteral("PRAGMA foreign_keys = OFF"), QStringLiteral("disable_foreign_keys"))) return false;

    MutationExecutor::Request request;
    request.sourceModule = QStringLiteral("DbDiagnostics");
    request.entityType = QStringLiteral("schema");
    request.entityId = QStringLiteral("outsource_repairs");
    request.operation = QStringLiteral("rebuild_active_outsource_repairs_order_uid");
    request.description = QStringLiteral("Rebuild active outsource_repairs to canonical order_uid schema");
    request.changedTables = {QStringLiteral("outsource_repairs")};
    request.beginErrorFallback = QStringLiteral("Не вдалося розпочати перебудову outsource_repairs.");
    request.commitErrorFallback = QStringLiteral("Не вдалося завершити перебудову outsource_repairs.");

    const auto tx = MutationExecutor::execute(database, request, [&](QSqlDatabase& txDb, QString* txError) {
        QSqlQuery q(txDb);
        auto fail = [&](const QString& text) {
            QSqlQuery qr(txDb);
            qr.exec(QStringLiteral("DROP TABLE IF EXISTS outsource_repairs_new"));
            if (txError && txError->trimmed().isEmpty()) *txError = text;
            return false;
        };
        if (!q.exec(
                "CREATE TABLE IF NOT EXISTS outsource_repairs_new ("
                "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
                "  order_uid TEXT NOT NULL,"
                "  order_number INTEGER NOT NULL DEFAULT 0,"
                "  sent_at TEXT NOT NULL DEFAULT '',"
                "  company TEXT NOT NULL DEFAULT '',"
                "  part_name TEXT NOT NULL DEFAULT '',"
                "  repair_cost REAL NOT NULL DEFAULT 0,"
                "  client_price REAL NOT NULL DEFAULT 0,"
                "  note TEXT NOT NULL DEFAULT '',"
                "  created_at TEXT NOT NULL DEFAULT (datetime('now')),"
                "  service_id INTEGER,"
                "  FOREIGN KEY(order_uid) REFERENCES work_orders(order_uid) ON DELETE CASCADE"
                ");")) {
            return fail(q.lastError().text());
        }
    const QString orderExpr = firstAvailableExpr(cols, {"order_number"}, "0");
    const QString uidExpr = cols.contains(QStringLiteral("order_uid"))
        ? QStringLiteral("COALESCE(NULLIF(TRIM(COALESCE(order_uid,'')),''), (SELECT NULLIF(TRIM(COALESCE(wo.order_uid,'')), '') FROM work_orders wo WHERE wo.number = %1), CASE WHEN COALESCE(%1,0)>0 THEN 'rewrite_' || %1 ELSE '' END)").arg(orderExpr)
        : QStringLiteral("COALESCE((SELECT NULLIF(TRIM(COALESCE(wo.order_uid,'')), '') FROM work_orders wo WHERE wo.number = %1), CASE WHEN COALESCE(%1,0)>0 THEN 'rewrite_' || %1 ELSE '' END)").arg(orderExpr);
    const QString serviceExpr = firstAvailableExpr(cols, {"service_id"}, "NULL");
    const QString copySql = QStringLiteral(
        "INSERT INTO outsource_repairs_new(id, order_uid, order_number, sent_at, company, part_name, repair_cost, client_price, note, created_at, service_id) "
        "SELECT id, %1, COALESCE(%2,0), %3, %4, %5, %6, %7, %8, %9, CASE WHEN COALESCE(%10,0)<=0 THEN NULL ELSE %10 END "
        "FROM outsource_repairs WHERE COALESCE(%2,0) > 0")
        .arg(uidExpr,
             orderExpr,
             nonNullTextSql(firstAvailableExpr(cols, {"sent_at"}, "''")),
             nonNullTextSql(firstAvailableExpr(cols, {"company"}, "''")),
             nonNullTextSql(firstAvailableExpr(cols, {"part_name"}, "''")),
             nonNullRealSql(firstAvailableExpr(cols, {"repair_cost"}, "0")),
             nonNullRealSql(firstAvailableExpr(cols, {"client_price"}, "0")),
             nonNullTextSql(firstAvailableExpr(cols, {"note"}, "''")),
             nonNullTextSql(firstAvailableExpr(cols, {"created_at"}, QStringLiteral("datetime('now')"))),
             serviceExpr);
    dropViewsReferencingTable(txDb, QStringLiteral("outsource_repairs"));
        if (!q.exec(copySql) || !q.exec("DROP TABLE outsource_repairs") || !q.exec("ALTER TABLE outsource_repairs_new RENAME TO outsource_repairs")) {
            return fail(q.lastError().text());
        }
        return true;
    });
    QSqlQuery qi(database);
    if (!execSchemaGuardedSql(qi, QStringLiteral("PRAGMA foreign_keys = ON"), QStringLiteral("enable_foreign_keys"))) return false;
    if (!tx.ok) return false;
    if (!execSchemaGuardedSql(qi, QStringLiteral("CREATE INDEX IF NOT EXISTS idx_outsource_repairs_order_uid ON outsource_repairs(order_uid)"), QStringLiteral("index_outsource_repairs_order_uid"))) return false;
    if (!execSchemaGuardedSql(qi, QStringLiteral("CREATE INDEX IF NOT EXISTS idx_outsource_repairs_order_number ON outsource_repairs(order_number)"), QStringLiteral("index_outsource_repairs_order_number"))) return false;
    return true;
}

bool rebuildActiveOutsourceJobsTableToOrderUid(QSqlDatabase database)
{
    if (!database.isValid()) return false;
    if (!database.isOpen() && !database.open()) return false;
    if (!tableExistsInDb(database, QStringLiteral("outsource_jobs"))) return true;
    dropLegacyCompatibilityViews(database);
    dropViewsReferencingTable(database, QStringLiteral("outsource_jobs"));
    const QSet<QString> cols = tableColumnsForDb(database, QStringLiteral("outsource_jobs"));
    const bool needsRebuild =
        !cols.contains(QStringLiteral("order_uid")) ||
        !tableCreateSqlContains(database, QStringLiteral("outsource_jobs"), QStringLiteral("REFERENCES work_orders(order_uid)"));
    if (!needsRebuild) return true;
    QString schemaGuardError;
    if (!ensureSchemaDestructiveOperationSnapshot(QStringLiteral("outsource_jobs"), &schemaGuardError)) return false;
    QSqlQuery q(database);
    if (!execSchemaGuardedSql(q, QStringLiteral("PRAGMA foreign_keys = OFF"), QStringLiteral("disable_foreign_keys"))) return false;

    MutationExecutor::Request request;
    request.sourceModule = QStringLiteral("DbDiagnostics");
    request.entityType = QStringLiteral("schema");
    request.entityId = QStringLiteral("outsource_jobs");
    request.operation = QStringLiteral("rebuild_active_outsource_jobs_order_uid");
    request.description = QStringLiteral("Rebuild active outsource_jobs to canonical order_uid schema");
    request.changedTables = {QStringLiteral("outsource_jobs")};
    request.beginErrorFallback = QStringLiteral("Не вдалося розпочати перебудову outsource_jobs.");
    request.commitErrorFallback = QStringLiteral("Не вдалося завершити перебудову outsource_jobs.");

    const auto tx = MutationExecutor::execute(database, request, [&](QSqlDatabase& txDb, QString* txError) {
        QSqlQuery q(txDb);
        auto fail = [&](const QString& text) {
            QSqlQuery qr(txDb);
            qr.exec(QStringLiteral("DROP TABLE IF EXISTS outsource_jobs_new"));
            if (txError && txError->trimmed().isEmpty()) *txError = text;
            return false;
        };
        if (!q.exec(
                "CREATE TABLE IF NOT EXISTS outsource_jobs_new ("
                "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
                "  order_uid TEXT NOT NULL,"
                "  order_number INTEGER NOT NULL DEFAULT 0,"
                "  service_id INTEGER NOT NULL,"
                "  company_id INTEGER NOT NULL DEFAULT 0,"
                "  company_name TEXT NOT NULL DEFAULT '',"
                "  sent_date TEXT NOT NULL DEFAULT '',"
                "  return_date TEXT NOT NULL DEFAULT '',"
                "  part_name TEXT NOT NULL DEFAULT '',"
                "  company_price REAL NOT NULL DEFAULT 0,"
                "  markup_percent REAL NOT NULL DEFAULT 0,"
                "  client_price REAL NOT NULL DEFAULT 0,"
                "  note TEXT NOT NULL DEFAULT '',"
                "  FOREIGN KEY(order_uid) REFERENCES work_orders(order_uid) ON DELETE CASCADE,"
                "  FOREIGN KEY(service_id) REFERENCES work_order_services(id) ON DELETE CASCADE"
                ");")) {
            return fail(q.lastError().text());
        }
    const QString orderExpr = firstAvailableExpr(cols, {"order_number"}, "0");
    const QString uidExpr = cols.contains(QStringLiteral("order_uid"))
        ? QStringLiteral("COALESCE(NULLIF(TRIM(COALESCE(order_uid,'')),''), (SELECT NULLIF(TRIM(COALESCE(wo.order_uid,'')), '') FROM work_orders wo WHERE wo.number = %1), CASE WHEN COALESCE(%1,0)>0 THEN 'rewrite_' || %1 ELSE '' END)").arg(orderExpr)
        : QStringLiteral("COALESCE((SELECT NULLIF(TRIM(COALESCE(wo.order_uid,'')), '') FROM work_orders wo WHERE wo.number = %1), CASE WHEN COALESCE(%1,0)>0 THEN 'rewrite_' || %1 ELSE '' END)").arg(orderExpr);
    const QString copySql = QStringLiteral(
        "INSERT INTO outsource_jobs_new(id, order_uid, order_number, service_id, company_id, company_name, sent_date, return_date, part_name, company_price, markup_percent, client_price, note) "
        "SELECT id, %1, COALESCE(%2,0), COALESCE(service_id,0), COALESCE(company_id,0), COALESCE(company_name,''), COALESCE(sent_date,''), COALESCE(return_date,''), COALESCE(part_name,''), COALESCE(company_price,0), COALESCE(markup_percent,0), COALESCE(client_price,0), COALESCE(note,'') "
        "FROM outsource_jobs WHERE COALESCE(%2,0) > 0")
        .arg(uidExpr, orderExpr);
    dropViewsReferencingTable(txDb, QStringLiteral("outsource_jobs"));
        if (!q.exec(copySql) || !q.exec("DROP TABLE outsource_jobs") || !q.exec("ALTER TABLE outsource_jobs_new RENAME TO outsource_jobs")) {
            return fail(q.lastError().text());
        }
        return true;
    });
    QSqlQuery qi(database);
    if (!execSchemaGuardedSql(qi, QStringLiteral("PRAGMA foreign_keys = ON"), QStringLiteral("enable_foreign_keys"))) return false;
    if (!tx.ok) return false;
    if (!execSchemaGuardedSql(qi, QStringLiteral("CREATE INDEX IF NOT EXISTS idx_outsource_jobs_order_uid ON outsource_jobs(order_uid)"), QStringLiteral("index_outsource_jobs_order_uid"))) return false;
    if (!execSchemaGuardedSql(qi, QStringLiteral("CREATE INDEX IF NOT EXISTS idx_outsource_jobs_order_number ON outsource_jobs(order_number)"), QStringLiteral("index_outsource_jobs_order_number"))) return false;
    if (!execSchemaGuardedSql(qi, QStringLiteral("CREATE INDEX IF NOT EXISTS idx_outsource_jobs_service_id ON outsource_jobs(service_id)"), QStringLiteral("index_outsource_jobs_service_id"))) return false;
    return true;
}

bool rebuildActivePartsTableToCanonical(QSqlDatabase database)
{
    if (!database.isValid()) return false;
    if (!database.isOpen() && !database.open()) return false;
    if (!tableExistsInDb(database, QStringLiteral("work_order_parts"))) return true;

    dropLegacyCompatibilityViews(database);
    dropViewsReferencingTable(database, QStringLiteral("work_order_parts"));
    dropFtsTriggersReferencingTable(database, QStringLiteral("work_order_parts"));
    const QSet<QString> cols = tableColumnsForDb(database, QStringLiteral("work_order_parts"));
    const bool hasLegacyAliases =
        cols.contains(QStringLiteral("work_order")) ||
        cols.contains(QStringLiteral("work_order_id")) ||
        cols.contains(QStringLiteral("work_order_number")) ||
        cols.contains(QStringLiteral("date"));
    const QString partsCreateSql = tableCreateSqlForDb(database, QStringLiteral("work_order_parts"));
    const bool hasBlockingForeignKey = partsCreateSql.contains(QStringLiteral("REFERENCES"), Qt::CaseInsensitive)
        || partsCreateSql.contains(QStringLiteral("FOREIGN KEY"), Qt::CaseInsensitive);
    const bool needsRebuild = hasLegacyAliases || hasBlockingForeignKey
            || !cols.contains(QStringLiteral("order_uid")) || !cols.contains(QStringLiteral("delivery_date")) || !cols.contains(QStringLiteral("installed_at"))
            || !cols.contains(QStringLiteral("purchase_price")) || !cols.contains(QStringLiteral("purchase_amount"))
            || !cols.contains(QStringLiteral("manufacturer"))
            || !cols.contains(QStringLiteral("markup_percent"))
            || !cols.contains(QStringLiteral("manual_markup_override"))
            || !cols.contains(QStringLiteral("markup_source"))
            || !cols.contains(QStringLiteral("price_input_mode"))
            || !cols.contains(QStringLiteral("pricing_updated_at"))
            || !cols.contains(QStringLiteral("pricing_calc_version"))
            || !cols.contains(QStringLiteral("part_uid"))
            || !cols.contains(QStringLiteral("status"))
            || !cols.contains(QStringLiteral("installed"))
            || !cols.contains(QStringLiteral("installed_by"))
            || !cols.contains(QStringLiteral("request_uid"))
            || !cols.contains(QStringLiteral("requested_at"))
            || !cols.contains(QStringLiteral("requested_by"))
            || !cols.contains(QStringLiteral("requested_by_uid"))
            || !cols.contains(QStringLiteral("ordered_by"))
            || !cols.contains(QStringLiteral("ordered_by_uid"))
            || !cols.contains(QStringLiteral("order_comment"))
            || !cols.contains(QStringLiteral("node_group"))
            || !cols.contains(QStringLiteral("component"))
            || !cols.contains(QStringLiteral("metadata_json"))
            || !cols.contains(QStringLiteral("created_at"))
            || !cols.contains(QStringLiteral("updated_at"));
    if (!needsRebuild) return true;
    QString schemaGuardError;
    if (!ensureSchemaDestructiveOperationSnapshot(QStringLiteral("work_order_parts"), &schemaGuardError)) return false;

    QSqlQuery q(database);
    if (!execSchemaGuardedSql(q, QStringLiteral("PRAGMA foreign_keys = OFF"), QStringLiteral("disable_foreign_keys"))) return false;

    MutationExecutor::Request request;
    request.sourceModule = QStringLiteral("DbDiagnostics");
    request.entityType = QStringLiteral("schema");
    request.entityId = QStringLiteral("work_order_parts");
    request.operation = QStringLiteral("rebuild_active_parts_canonical");
    request.description = QStringLiteral("Rebuild active work_order_parts to canonical schema");
    request.changedTables = {QStringLiteral("work_order_parts")};
    request.beginErrorFallback = QStringLiteral("Не вдалося розпочати перебудову work_order_parts.");
    request.commitErrorFallback = QStringLiteral("Не вдалося завершити перебудову work_order_parts.");

    const auto tx = MutationExecutor::execute(database, request, [&](QSqlDatabase& txDb, QString* txError) {
        QSqlQuery q(txDb);
        auto fail = [&](const QString& text) {
            QSqlQuery qr(txDb);
            qr.exec(QStringLiteral("DROP TABLE IF EXISTS work_order_parts_new"));
            if (txError && txError->trimmed().isEmpty()) *txError = text;
            return false;
        };

        if (!q.exec(
            "CREATE TABLE IF NOT EXISTS work_order_parts_new ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  order_uid TEXT NOT NULL,"
            "  order_number INTEGER NOT NULL DEFAULT 0,"
            "  ordered_at TEXT NOT NULL DEFAULT '',"
            "  delivery_date TEXT NOT NULL DEFAULT '',"
            "  installed_at TEXT NOT NULL DEFAULT '',"
            "  store TEXT NOT NULL DEFAULT '',"
            "  part_number TEXT,"
            "  name TEXT NOT NULL,"
            "  manufacturer TEXT NOT NULL DEFAULT '',"
            "  note TEXT NOT NULL DEFAULT '',"
            "  qty REAL NOT NULL DEFAULT 1,"
            "  price REAL NOT NULL DEFAULT 0,"
            "  amount REAL NOT NULL DEFAULT 0,"
            "  service_id INTEGER NOT NULL DEFAULT 0,"
            "  ordered_by_admin INTEGER NOT NULL DEFAULT 1,"
            "  paid INTEGER NOT NULL DEFAULT 0,"
            "  purchase_price REAL NOT NULL DEFAULT 0,"
            "  purchase_amount REAL NOT NULL DEFAULT 0,"
            "  markup_percent REAL NOT NULL DEFAULT 0,"
            "  manual_markup_override INTEGER NOT NULL DEFAULT 0,"
            "  markup_source TEXT NOT NULL DEFAULT 'policy',"
            "  price_input_mode TEXT NOT NULL DEFAULT 'unknown',"
            "  pricing_updated_at TEXT NOT NULL DEFAULT '',"
            "  pricing_calc_version INTEGER NOT NULL DEFAULT 2,"
            "  part_uid TEXT NOT NULL DEFAULT '',"
            "  status TEXT NOT NULL DEFAULT '',"
            "  installed INTEGER NOT NULL DEFAULT 0,"
            "  installed_by TEXT NOT NULL DEFAULT '',"
            "  request_uid TEXT NOT NULL DEFAULT '',"
            "  requested_at TEXT NOT NULL DEFAULT '',"
            "  requested_by TEXT NOT NULL DEFAULT '',"
            "  requested_by_uid TEXT NOT NULL DEFAULT '',"
            "  ordered_by TEXT NOT NULL DEFAULT '',"
            "  ordered_by_uid TEXT NOT NULL DEFAULT '',"
            "  order_comment TEXT NOT NULL DEFAULT '',"
            "  node_group TEXT NOT NULL DEFAULT '',"
            "  component TEXT NOT NULL DEFAULT '',"
            "  metadata_json TEXT NOT NULL DEFAULT '{}',"
            "  created_at TEXT NOT NULL DEFAULT '',"
            "  updated_at TEXT NOT NULL DEFAULT ''"
            ");")) {
            qWarning() << "rebuildActivePartsTableToCanonical: create new table failed" << q.lastError().text();
            return fail(q.lastError().text());
        }

    const QString orderExpr = firstAvailableExpr(cols, {"order_number", "work_order", "work_order_id", "work_order_number"}, "0");
    const QString orderUidExpr = cols.contains(QStringLiteral("order_uid"))
        ? QStringLiteral("COALESCE(NULLIF(TRIM(COALESCE(order_uid,'')),''), (SELECT NULLIF(TRIM(COALESCE(wo.order_uid,'')), '') FROM work_orders wo WHERE wo.number = %1), CASE WHEN COALESCE(%1,0)>0 THEN 'rewrite_' || %1 ELSE '' END)").arg(orderExpr)
        : QStringLiteral("COALESCE((SELECT NULLIF(TRIM(COALESCE(wo.order_uid,'')), '') FROM work_orders wo WHERE wo.number = %1), CASE WHEN COALESCE(%1,0)>0 THEN 'rewrite_' || %1 ELSE '' END)").arg(orderExpr);
    const QString orderedExpr = nonNullTextSql(firstAvailableExpr(cols, {"ordered_at", "date", "created_at"}, "''"));
    const QString deliveryExpr = nonNullTextSql(firstAvailableExpr(cols, {"delivery_date"}, "''"));
    const QString installedExpr = nonNullTextSql(firstAvailableExpr(cols, {"installed_at"}, "''"));
    const QString storeExpr = nonNullTextSql(firstAvailableExpr(cols, {"store", "shop"}, "''"));
    const QString partNumberExpr = QStringLiteral("NULLIF(TRIM(COALESCE(%1,'')),'')").arg(firstAvailableExpr(cols, {"part_number", "oem", "oem_number", "article", "article_number"}, "''"));
    const QString nameExpr = nonNullTextSql(firstAvailableExpr(cols, {"name", "title", "part_name"}, "''"));
    const QString noteExpr = nonNullTextSql(firstAvailableExpr(cols, {"note", "comment", "notes", "customer_note"}, "''"));
    const QString manufacturerExpr = nonNullTextSql(firstAvailableExpr(cols, {"manufacturer", "brand"}, "''"));
    const QString qtyExpr = nonNullRealSql(firstAvailableExpr(cols, {"qty", "quantity"}, "1"), QStringLiteral("1"));
    const QString priceExpr = nonNullRealSql(firstAvailableExpr(cols, {"price", "client_price", "sale_price"}, "0"));
    const QString amountExpr = nonNullRealSql(firstAvailableExpr(cols, {"amount", "sum", "total", "client_amount"}, "0"));
    const QString serviceExpr = nonNullIntSql(firstAvailableExpr(cols, {"service_id"}, "0"));
    const QString orderedAdminExpr = QStringLiteral("CASE WHEN COALESCE(%1,1)<>0 THEN 1 ELSE 0 END").arg(firstAvailableExpr(cols, {"ordered_by_admin", "is_ordered"}, "1"));
    const QString paidExpr = QStringLiteral("CASE WHEN COALESCE(%1,0)<>0 THEN 1 ELSE 0 END").arg(firstAvailableExpr(cols, {"paid", "is_paid"}, "0"));
    const QString purchasePriceExpr = nonNullRealSql(firstAvailableExpr(cols, {"purchase_price", "cost_price"}, "0"));
    const QString purchaseAmountExpr = nonNullRealSql(firstAvailableExpr(cols, {"purchase_amount", "cost_amount"}, "0"));
    const QString markupExpr = nonNullRealSql(firstAvailableExpr(cols, {"markup_percent", "markup", "margin_percent"}, "0"));
    const QString manualMarkupExpr = QStringLiteral("CASE WHEN COALESCE(%1,0)<>0 THEN 1 ELSE 0 END").arg(firstAvailableExpr(cols, {"manual_markup_override"}, "0"));
    const QString markupSourceExpr = nonNullTextSql(firstAvailableExpr(cols, {"markup_source"}, "'policy'"));
    const QString pricingUpdatedAtExpr = nonNullTextSql(firstAvailableExpr(cols, {"pricing_updated_at"}, "''"));
    const QString pricingCalcVersionExpr = nonNullIntSql(firstAvailableExpr(cols, {"pricing_calc_version"}, "2"), QStringLiteral("2"));
    const QString partUidExpr = nonNullTextSql(firstAvailableExpr(cols, {"part_uid"}, "''"));
    const QString statusExpr = nonNullTextSql(firstAvailableExpr(cols, {"status"}, "''"));
    const QString installedFlagExpr = QStringLiteral("CASE WHEN COALESCE(%1,0)<>0 THEN 1 ELSE 0 END").arg(firstAvailableExpr(cols, {"installed"}, "0"));
    const QString installedByExpr = nonNullTextSql(firstAvailableExpr(cols, {"installed_by"}, "''"));
    const QString requestUidExpr = nonNullTextSql(firstAvailableExpr(cols, {"request_uid"}, "''"));
    const QString requestedAtExpr = nonNullTextSql(firstAvailableExpr(cols, {"requested_at"}, "''"));
    const QString requestedByExpr = nonNullTextSql(firstAvailableExpr(cols, {"requested_by"}, "''"));
    const QString requestedByUidExpr = nonNullTextSql(firstAvailableExpr(cols, {"requested_by_uid"}, "''"));
    const QString orderedByExpr = nonNullTextSql(firstAvailableExpr(cols, {"ordered_by"}, "''"));
    const QString orderedByUidExpr = nonNullTextSql(firstAvailableExpr(cols, {"ordered_by_uid"}, "''"));
    const QString orderCommentExpr = nonNullTextSql(firstAvailableExpr(cols, {"order_comment"}, "''"));
    const QString nodeGroupExpr = nonNullTextSql(firstAvailableExpr(cols, {"node_group"}, "''"));
    const QString componentExpr = nonNullTextSql(firstAvailableExpr(cols, {"component"}, "''"));
    const QString metadataExpr = nonNullTextSql(firstAvailableExpr(cols, {"metadata_json"}, "'{}'"));
    const QString createdAtExpr = nonNullTextSql(firstAvailableExpr(cols, {"created_at"}, "''"));
    const QString updatedAtExpr = nonNullTextSql(firstAvailableExpr(cols, {"updated_at"}, "''"));

    const QString copySql =
        QStringLiteral(
            "INSERT INTO work_order_parts_new("
            "id, order_uid, order_number, ordered_at, delivery_date, installed_at, store, part_number, name, manufacturer, note, qty, price, amount, "
            "service_id, ordered_by_admin, paid, purchase_price, purchase_amount, markup_percent, manual_markup_override, markup_source, pricing_updated_at, pricing_calc_version, "
            "part_uid, status, installed, installed_by, request_uid, requested_at, requested_by, requested_by_uid, ordered_by, ordered_by_uid, order_comment, node_group, component, metadata_json, created_at, updated_at, price_input_mode) "
            "SELECT "
            "id, "
            "%1, COALESCE(%2,0), %3, %4, %5, %6, %7, %8, %9, %10, %11, %12, %13, %14, %15, %16, %17, %18, %19, %20, %21, %22, %23, "
            "%24, %25, %26, %27, %28, %29, %30, %31, %32, %33, %34, %35, %36, %37, %38, %39, %40 "
            "FROM work_order_parts WHERE COALESCE(%2,0) > 0")
            .arg(orderUidExpr, orderExpr, orderedExpr, deliveryExpr, installedExpr, storeExpr, partNumberExpr, nameExpr, manufacturerExpr, noteExpr,
                 qtyExpr, priceExpr, amountExpr, serviceExpr, orderedAdminExpr, paidExpr,
                 purchasePriceExpr, purchaseAmountExpr, markupExpr, manualMarkupExpr, markupSourceExpr, pricingUpdatedAtExpr, pricingCalcVersionExpr,
                 partUidExpr, statusExpr, installedFlagExpr, installedByExpr, requestUidExpr, requestedAtExpr, requestedByExpr,
                 requestedByUidExpr, orderedByExpr, orderedByUidExpr, orderCommentExpr, nodeGroupExpr, componentExpr, metadataExpr,
                 createdAtExpr, updatedAtExpr, firstAvailableExpr(cols, {"price_input_mode"}, "'unknown'"));

    if (!q.exec(copySql)) {
            qWarning() << "rebuildActivePartsTableToCanonical: copy failed" << q.lastError().text();
            return fail(q.lastError().text());
        }

        dropViewsReferencingTable(txDb, QStringLiteral("work_order_parts"));
        if (!dropFtsTriggersReferencingTable(txDb, QStringLiteral("work_order_parts"))) {
            qWarning() << "rebuildActivePartsTableToCanonical: drop stale FTS triggers failed";
            return fail(QStringLiteral("Failed to drop stale work_order_parts FTS triggers"));
        }
        if (!q.exec(QStringLiteral("DROP TABLE work_order_parts"))) {
            qWarning() << "rebuildActivePartsTableToCanonical: drop old table failed" << q.lastError().text();
            return fail(q.lastError().text());
        }
        if (!q.exec(QStringLiteral("ALTER TABLE work_order_parts_new RENAME TO work_order_parts"))) {
            qWarning() << "rebuildActivePartsTableToCanonical: rename failed" << q.lastError().text();
            return fail(q.lastError().text());
        }

        return true;
    });

    if (!execSchemaGuardedSql(q, QStringLiteral("PRAGMA foreign_keys = ON"), QStringLiteral("enable_foreign_keys"))) return false;
    if (!tx.ok) {
        qWarning() << "rebuildActivePartsTableToCanonical: commit failed" << tx.errorText;
        return false;
    }
    return true;
}
