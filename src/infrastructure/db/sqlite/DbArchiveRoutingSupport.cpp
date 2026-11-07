#include "infrastructure/db/sqlite/Db.h"
#include <QScopeGuard>
#include "core/services/OrderSearchIndex.h"
#include "core/io/SafeFileOps.h"
#include "infrastructure/db/sqlite/ScopedConnectionManager.h"
#include "infrastructure/db/sqlite/DbFacadeSupport.h"
#include "infrastructure/db/sqlite/DbArchiveInternals.h"
#include "infrastructure/db/sqlite/DbArchiveHydrationSupport.h"
#include "infrastructure/db/sqlite/DbArchiveSourceSchemaSupport.h"
#include "infrastructure/db/sqlite/DbArchiveRouterCacheImportSupport.h"
#include "infrastructure/db/sqlite/LegacyCanonicalImportNormalization.h"
#include "infrastructure/db/sqlite/LegacyCanonicalAncillaryImportSupport.h"
#include "infrastructure/db/sqlite/DbSchemaAuditSupport.h"
#include "infrastructure/db/sqlite/DbSettingsCacheSupport.h"
#include "infrastructure/db/sqlite/DbSourceFingerprintSupport.h"
#include "infrastructure/db/sqlite/DbWorkspaceOrderStateSupport.h"
#include "infrastructure/db/sqlite/DbThreadConnectionSupport.h"
#include "core/services/OrderRoutingLookupService.h"
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
#include "runtime/AppRuntime.h"

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

using namespace OrderRoutingLookupService;

bool ensureLegacyArchiveSnapshot()
{
    const QString legacyPath = AppRuntime::legacyDbFilePath();
    const QString archivePath = AppRuntime::archiveDbFilePath();
    const QString activePath = AppRuntime::activeDbFilePath();
    if (QFileInfo(legacyPath).absoluteFilePath() == QFileInfo(archivePath).absoluteFilePath()) return true;
    if (QFileInfo::exists(archivePath)) return true;

    QString sourcePath;
    if (QFileInfo::exists(legacyPath)) {
        sourcePath = legacyPath;
    } else if (QFileInfo::exists(activePath)) {
        // During post-import reopen the imported legacy source may be restored into
        // the active runtime slot before canonical projections are rebuilt. In that
        // case create a side snapshot from the active file and use it as the
        // attached rewrite source.
        sourcePath = activePath;
    } else {
        return true;
    }

    if (QFileInfo(sourcePath).absoluteFilePath() == QFileInfo(archivePath).absoluteFilePath()) return true;
    QDir().mkpath(QFileInfo(archivePath).absolutePath());
    SafeFileOps::safeRemoveFile(archivePath, {QFileInfo(archivePath).absolutePath()}, {}, {QStringLiteral(".db")});
    if (!QFile::copy(sourcePath, archivePath)) {
        qWarning() << "Legacy archive copy failed:" << sourcePath << "->" << archivePath;
        return false;
    }
    AppRuntime::protectPathAtRest(archivePath);
    return true;
}

bool ensureRegistrySchemaFile()
{
    const QString expectedPath = QFileInfo(AppRuntime::registryDbFilePath()).absoluteFilePath();
    QSqlDatabase d;
    if (QSqlDatabase::contains(dbRegistryConnectionName())) {
        d = QSqlDatabase::database(dbRegistryConnectionName(), false);
#ifdef Q_OS_WIN
        const auto pathCase = Qt::CaseInsensitive;
#else
        const auto pathCase = Qt::CaseSensitive;
#endif
        if (d.isOpen() && QFileInfo(d.databaseName()).absoluteFilePath().compare(expectedPath, pathCase) != 0) {
            qWarning() << "Close the previous profile before opening another registry path";
            return false;
        }
        if (!d.isOpen()) d.setDatabaseName(expectedPath);
        DbThreadConnectionSupport::registerRuntimeConnection(dbRegistryConnectionName(), d.databaseName());
    } else {
        d = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), dbRegistryConnectionName());
        d.setDatabaseName(AppRuntime::registryDbFilePath());
        DbThreadConnectionSupport::registerRuntimeConnection(dbRegistryConnectionName(), AppRuntime::registryDbFilePath());
    }
    if (!d.isOpen() && !d.open()) {
        qWarning() << "Registry DB open failed:" << d.lastError().text();
        return false;
    }
    AppRuntime::protectPathAtRest(AppRuntime::registryDbFilePath());
    QSqlQuery schemaTransaction(d);
    if (!schemaTransaction.exec(QStringLiteral("SAVEPOINT registry_schema_prepare"))) return false;
    auto rollback=qScopeGuard([&] {
        if (!schemaTransaction.exec(QStringLiteral("ROLLBACK TO registry_schema_prepare"))) qWarning()<<"Registry schema rollback failed"<<schemaTransaction.lastError().text();
        if (!schemaTransaction.exec(QStringLiteral("RELEASE registry_schema_prepare"))) qWarning()<<"Registry schema savepoint release failed"<<schemaTransaction.lastError().text();
    });
    QSqlQuery q(d);
    auto schemaSql=[&](const QString& sql) {
        if (q.exec(sql)) return true;
        qWarning()<<"Registry schema SQL failed"<<q.lastError().text(); return false;
    };
    if (!q.exec(
            "CREATE TABLE IF NOT EXISTS order_registry ("
            "  order_number INTEGER PRIMARY KEY,"
            "  storage_kind TEXT NOT NULL DEFAULT 'active',"
            "  storage_path TEXT NOT NULL DEFAULT '',"
            "  source_db TEXT NOT NULL DEFAULT '',"
            "  imported_at TEXT NOT NULL DEFAULT '',"
            "  last_seen_at TEXT NOT NULL DEFAULT '',"
            "  status TEXT NOT NULL DEFAULT '',"
            "  opened_at TEXT NOT NULL DEFAULT '',"
            "  closed_at TEXT NOT NULL DEFAULT '',"
            "  plate TEXT NOT NULL DEFAULT '',"
            "  plate_norm TEXT NOT NULL DEFAULT '',"
            "  vin TEXT NOT NULL DEFAULT '',"
            "  car TEXT NOT NULL DEFAULT '',"
            "  client TEXT NOT NULL DEFAULT '',"
            "  master_id TEXT NOT NULL DEFAULT '',"
            "  master_name_closed TEXT NOT NULL DEFAULT '',"
            "  works_sum REAL NOT NULL DEFAULT 0,"
            "  parts_sum REAL NOT NULL DEFAULT 0"
            ");")) {
        qWarning() << "Registry schema failed:" << q.lastError().text();
        return false;
    }
    if (!q.exec(
            "CREATE TABLE IF NOT EXISTS registry_meta ("
            "  key TEXT PRIMARY KEY,"
            "  value TEXT NOT NULL DEFAULT ''"
            ");")) {
        qWarning() << "Registry meta schema failed:" << q.lastError().text();
        return false;
    }
    const QSet<QString> registryCols = tableColumnsForDb(d, QStringLiteral("order_registry"));
    auto ensureRegistryColumn = [&](const QString& colName, const QString& ddl) {
        if (registryCols.contains(colName)) return true;
        QSqlQuery alter(d);
        if (alter.exec(QStringLiteral("ALTER TABLE order_registry ADD COLUMN %1;").arg(ddl))) return true;
        qWarning()<<"Registry column preparation failed"<<colName<<alter.lastError().text(); return false;
    };
    if (!ensureRegistryColumn(QStringLiteral("order_uid"), QStringLiteral("order_uid TEXT NOT NULL DEFAULT ''"))) return false;
    if (!ensureRegistryColumn(QStringLiteral("visible_order_number"), QStringLiteral("visible_order_number INTEGER NOT NULL DEFAULT 0"))) return false;
    if (!ensureRegistryColumn(QStringLiteral("order_year"), QStringLiteral("order_year INTEGER NOT NULL DEFAULT 0"))) return false;
    if (!ensureRegistryColumn(QStringLiteral("master_id"), QStringLiteral("master_id TEXT NOT NULL DEFAULT ''"))) return false;
    if (!ensureRegistryColumn(QStringLiteral("master_name_closed"), QStringLiteral("master_name_closed TEXT NOT NULL DEFAULT ''"))) return false;
    if (!ensureRegistryColumn(QStringLiteral("works_sum"), QStringLiteral("works_sum REAL NOT NULL DEFAULT 0"))) return false;
    if (!ensureRegistryColumn(QStringLiteral("parts_sum"), QStringLiteral("parts_sum REAL NOT NULL DEFAULT 0"))) return false;
    if (!schemaSql("DROP INDEX IF EXISTS idx_registry_order_uid_unique_nonempty")) return false;
    if (!schemaSql("CREATE UNIQUE INDEX IF NOT EXISTS idx_registry_order_uid_unique ON order_registry(order_uid) WHERE TRIM(COALESCE(order_uid,''))<>''")) return false;
    if (!schemaSql("CREATE INDEX IF NOT EXISTS idx_registry_legacy_internal_order ON order_registry(order_number)")) return false;
    if (!schemaSql("CREATE INDEX IF NOT EXISTS idx_registry_visible_year ON order_registry(order_year, visible_order_number)")) return false;
    if (!schemaSql("CREATE INDEX IF NOT EXISTS idx_registry_visible_lookup ON order_registry(visible_order_number, order_year DESC, order_number DESC)")) return false;
    if (!schemaSql("CREATE INDEX IF NOT EXISTS idx_registry_plate_norm ON order_registry(plate_norm)")) return false;
    if (!schemaSql("CREATE INDEX IF NOT EXISTS idx_registry_status_opened ON order_registry(status, opened_at)")) return false;
    if (!schemaSql("CREATE INDEX IF NOT EXISTS idx_registry_closed_at ON order_registry(closed_at)")) return false;
    if (!schemaSql("CREATE INDEX IF NOT EXISTS idx_registry_storage_number ON order_registry(storage_kind, order_number)")) return false;
    if (!schemaTransaction.exec(QStringLiteral("RELEASE registry_schema_prepare"))) return false;
    rollback.dismiss();
    return true;
}


bool ensurePartRegistrySchemaFile()
{
    if (!ensureRegistrySchemaFile()) return false;
    QSqlDatabase d = QSqlDatabase::database(dbRegistryConnectionName());
    if (!d.isOpen() && !d.open()) {
        qWarning() << "Registry DB open failed for part registry:" << d.lastError().text();
        return false;
    }
    QSqlQuery transaction(d);
    if(!transaction.exec(QStringLiteral("SAVEPOINT part_registry_schema_prepare")))return false;
    auto rollback=qScopeGuard([&]{
        if(!transaction.exec(QStringLiteral("ROLLBACK TO part_registry_schema_prepare")))qWarning()<<"Part registry schema rollback failed"<<transaction.lastError().text();
        if(!transaction.exec(QStringLiteral("RELEASE part_registry_schema_prepare")))qWarning()<<"Part registry schema release failed"<<transaction.lastError().text();
    });
    QSqlQuery q(d);
    auto schemaSql=[&](const QString& sql) {
        if (q.exec(sql)) return true;
        qWarning()<<"Parts registry schema SQL failed"<<q.lastError().text(); return false;
    };
    if (!q.exec(
            "CREATE TABLE IF NOT EXISTS part_registry ("
            "  entry_key TEXT PRIMARY KEY,"
            "  order_number INTEGER NOT NULL,"
            "  part_ref INTEGER NOT NULL DEFAULT 0,"
            "  storage_kind TEXT NOT NULL DEFAULT 'active',"
            "  storage_path TEXT NOT NULL DEFAULT '',"
            "  source_db TEXT NOT NULL DEFAULT '',"
            "  ordered_at TEXT NOT NULL DEFAULT '',"
            "  delivery_date TEXT NOT NULL DEFAULT '',"
            "  part_number TEXT NOT NULL DEFAULT '',"
            "  name TEXT NOT NULL DEFAULT '',"
            "  note TEXT NOT NULL DEFAULT '',"
            "  plate TEXT NOT NULL DEFAULT '',"
            "  plate_norm TEXT NOT NULL DEFAULT '',"
            "  vin TEXT NOT NULL DEFAULT '',"
            "  car TEXT NOT NULL DEFAULT '',"
            "  client TEXT NOT NULL DEFAULT '',"
            "  master_id TEXT NOT NULL DEFAULT '',"
            "  master_name_closed TEXT NOT NULL DEFAULT '',"
            "  works_sum REAL NOT NULL DEFAULT 0,"
            "  parts_sum REAL NOT NULL DEFAULT 0"
            ");")) {
        qWarning() << "Registry part_registry schema failed:" << q.lastError().text();
        return false;
    }
    // Existing numeric locators remain explicitly unverified; never infer UIDs.
    const auto columns=tableColumnsForDb(d,QStringLiteral("part_registry"));
    for(const auto& column:QStringList{QStringLiteral("order_uid"),QStringLiteral("part_uid")}) {
        if(!columns.contains(column) && !schemaSql(QStringLiteral("ALTER TABLE part_registry ADD COLUMN %1 TEXT NOT NULL DEFAULT ''").arg(column)))return false;
    }
    if(!schemaSql("CREATE INDEX IF NOT EXISTS idx_part_registry_order_uid ON part_registry(order_uid)"))return false;
    if(!schemaSql("CREATE UNIQUE INDEX IF NOT EXISTS idx_part_registry_storage_part_uid ON part_registry(storage_kind,storage_path,part_uid) WHERE TRIM(part_uid)<>''"))return false;
    if (!schemaSql("CREATE INDEX IF NOT EXISTS idx_part_registry_order ON part_registry(order_number)")) return false;
    if (!schemaSql("CREATE INDEX IF NOT EXISTS idx_part_registry_partno ON part_registry(part_number)")) return false;
    if (!schemaSql("CREATE INDEX IF NOT EXISTS idx_part_registry_plate_norm ON part_registry(plate_norm)")) return false;
    if (!schemaSql("CREATE INDEX IF NOT EXISTS idx_part_registry_dates ON part_registry(ordered_at, delivery_date)")) return false;
    if (!schemaSql("CREATE INDEX IF NOT EXISTS idx_part_registry_storage_order ON part_registry(storage_kind, order_number)")) return false;
    if(!transaction.exec(QStringLiteral("RELEASE part_registry_schema_prepare")))return false;
    rollback.dismiss();
    return true;
}

QSqlDatabase Database::db(QString* errorText)
{
    QString localError;
    const QString connName = currentConnectionName();
    if (!Database::ensureConnectionAlive(&localError)) {
        const QString detail = localError.trimmed().isEmpty()
            ? QStringLiteral("Database connection failed without detailed error")
            : localError.trimmed();
        if (errorText) *errorText = detail;
        qCritical().noquote() << QStringLiteral("DB_CONNECTION phase=ensure_failed operation=Database::db connection=") << connName
                              << QStringLiteral(" error=") << detail;
        return QSqlDatabase();
    }

    QSqlDatabase database = QSqlDatabase::database(connName, false);
    if (!database.isValid()) {
        const QString detail = QStringLiteral("Database connection handle is invalid: %1").arg(connName);
        if (errorText) *errorText = detail;
        qCritical().noquote() << QStringLiteral("DB_CONNECTION phase=invalid_handle operation=Database::db connection=") << connName
                              << QStringLiteral(" error=") << detail;
        return QSqlDatabase();
    }

    if (!database.isOpen()) {
        const QString detail = database.lastError().text().trimmed().isEmpty()
            ? QStringLiteral("Database connection is not open: %1").arg(connName)
            : database.lastError().text().trimmed();
        if (errorText) *errorText = detail;
        qCritical().noquote() << QStringLiteral("DB_CONNECTION phase=not_open operation=Database::db connection=") << connName
                              << QStringLiteral(" error=") << detail;
        return QSqlDatabase();
    }

    if (errorText) errorText->clear();
    return database;
}

QSqlDatabase Database::db()
{
    QString errorText;
    QSqlDatabase database = Database::db(&errorText);
    if ((!database.isValid() || !database.isOpen()) && !errorText.trimmed().isEmpty()) {
        qCritical().noquote() << QStringLiteral("DB_CONNECTION phase=caller_legacy_no_error_channel operation=Database::db connection=") << currentConnectionName()
                              << QStringLiteral(" error=") << errorText;
    }
    return database;
}


bool ensureRouterCacheSchema(QSqlDatabase database)
{
    if (!database.isValid()) return false;
    if (!database.isOpen() && !database.open()) return false;
    QSqlQuery q(database);
    if (!q.exec(
            "CREATE TABLE IF NOT EXISTS router_cache_work_orders ("
            "  number INTEGER PRIMARY KEY,"
            "  order_uid TEXT NOT NULL DEFAULT '',"
            "  order_number INTEGER NOT NULL DEFAULT 0,"
            "  order_year INTEGER NOT NULL DEFAULT 0,"
            "  opened_at TEXT NOT NULL,"
            "  closed_at TEXT,"
            "  status TEXT NOT NULL,"
            "  plate TEXT NOT NULL,"
            "  plate_norm TEXT NOT NULL,"
            "  vin TEXT NOT NULL DEFAULT '',"
            "  car_year INTEGER NOT NULL DEFAULT 0,"
            "  odometer INTEGER NOT NULL DEFAULT 0,"
            "  engine_type TEXT NOT NULL DEFAULT '',"
            "  engine_volume TEXT NOT NULL DEFAULT '',"
            "  fuel_type TEXT NOT NULL DEFAULT '',"
            "  car TEXT NOT NULL,"
            "  client TEXT NOT NULL,"
            "  client_phone TEXT NOT NULL DEFAULT '',"
            "  master_id TEXT NOT NULL DEFAULT '',"
            "  works_sum REAL NOT NULL DEFAULT 0,"
            "  parts_sum REAL NOT NULL DEFAULT 0,"
            "  discount REAL NOT NULL DEFAULT 0,"
            "  master_name_closed TEXT NOT NULL DEFAULT '',"
            "  car_model TEXT NOT NULL DEFAULT '',"
            "  car_brand TEXT NOT NULL DEFAULT '',"
            "  car_kind TEXT NOT NULL DEFAULT '',"
            "  color TEXT NOT NULL DEFAULT '',"
            "  car_color TEXT NOT NULL DEFAULT '',"
            "  owner_name TEXT NOT NULL DEFAULT ''"
            ");")) return false;
    if (!q.exec(
            "CREATE TABLE IF NOT EXISTS router_cache_work_order_services ("
            "  id INTEGER PRIMARY KEY,"
            "  order_uid TEXT NOT NULL DEFAULT '',"
            "  order_number INTEGER NOT NULL DEFAULT 0,"
            "  title TEXT NOT NULL,"
            "  qty REAL NOT NULL DEFAULT 1,"
            "  price REAL NOT NULL DEFAULT 0,"
            "  amount REAL NOT NULL DEFAULT 0,"
            "  note TEXT NOT NULL DEFAULT '',"
            "  paid INTEGER NOT NULL DEFAULT 1,"
            "  parent_id INTEGER NOT NULL DEFAULT 0"
            ");")) return false;
    if (!q.exec(
            "CREATE TABLE IF NOT EXISTS router_cache_service_masters ("
            "  service_id INTEGER NOT NULL,"
            "  master_id TEXT NOT NULL,"
            "  share_percent REAL NOT NULL DEFAULT 100,"
            "  role TEXT NOT NULL DEFAULT 'master',"
            "  master_name_closed TEXT NOT NULL DEFAULT '',"
            "  PRIMARY KEY(service_id, master_id)"
            ");")) return false;
    if (!q.exec(
            "CREATE TABLE IF NOT EXISTS router_cache_work_order_parts ("
            "  id INTEGER PRIMARY KEY,"
            "  part_uid TEXT NOT NULL DEFAULT '',"
            "  order_uid TEXT NOT NULL DEFAULT '',"
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
            "  markup_source TEXT NOT NULL DEFAULT 'legacy',"
            "  price_input_mode TEXT NOT NULL DEFAULT 'unknown',"
            "  pricing_updated_at TEXT NOT NULL DEFAULT '',"
            "  pricing_calc_version INTEGER NOT NULL DEFAULT 1,"
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
            ");")) return false;
    if (!q.exec(
            "CREATE TABLE IF NOT EXISTS router_cache_outsource_repairs ("
            "  id INTEGER PRIMARY KEY,"
            "  order_uid TEXT NOT NULL DEFAULT '',"
            "  order_number INTEGER NOT NULL DEFAULT 0,"
            "  sent_at TEXT NOT NULL DEFAULT '',"
            "  company TEXT NOT NULL DEFAULT '',"
            "  part_name TEXT NOT NULL DEFAULT '',"
            "  repair_cost REAL NOT NULL DEFAULT 0,"
            "  client_price REAL NOT NULL DEFAULT 0,"
            "  note TEXT NOT NULL DEFAULT '',"
            "  created_at TEXT NOT NULL DEFAULT '',"
            "  service_id INTEGER NOT NULL DEFAULT 0"
            ");")) return false;
    // DDL defaults do not certify existing cached content. Persist invalidation before ALTER.
    if (!q.exec("CREATE TABLE IF NOT EXISTS router_cache_content_state(singleton INTEGER PRIMARY KEY CHECK(singleton=1), ready INTEGER NOT NULL CHECK(ready IN (0,1)))")) return false;
    if (!q.exec("INSERT OR IGNORE INTO router_cache_content_state(singleton,ready) SELECT 1,CASE WHEN EXISTS(SELECT 1 FROM router_cache_work_orders) OR EXISTS(SELECT 1 FROM router_cache_work_order_parts) OR EXISTS(SELECT 1 FROM router_cache_work_order_services) OR EXISTS(SELECT 1 FROM router_cache_outsource_repairs) OR EXISTS(SELECT 1 FROM router_cache_service_masters) THEN 0 ELSE 1 END")) return false;
    {
        const QSet<QString> orderCols = tableColumnsForDb(database, QStringLiteral("router_cache_work_orders"));
        auto ensureRouterCacheColumn = [&](const QString& colName, const QString& ddl) {
            if (orderCols.contains(colName)) return true;
            if (!q.exec("UPDATE router_cache_content_state SET ready=0 WHERE singleton=1 AND (EXISTS(SELECT 1 FROM router_cache_work_orders) OR EXISTS(SELECT 1 FROM router_cache_work_order_parts) OR EXISTS(SELECT 1 FROM router_cache_work_order_services) OR EXISTS(SELECT 1 FROM router_cache_outsource_repairs) OR EXISTS(SELECT 1 FROM router_cache_service_masters))")) return false;
            QSqlQuery alter(database);
            return alter.exec(QStringLiteral("ALTER TABLE router_cache_work_orders ADD COLUMN %1;").arg(ddl));
        };
        if (!ensureRouterCacheColumn(QStringLiteral("discount_mode"), QStringLiteral("discount_mode TEXT NOT NULL DEFAULT 'legacy'"))) return false;
        if (!ensureRouterCacheColumn(QStringLiteral("order_uid"), QStringLiteral("order_uid TEXT NOT NULL DEFAULT ''"))) return false;
        if (!ensureRouterCacheColumn(QStringLiteral("order_number"), QStringLiteral("order_number INTEGER NOT NULL DEFAULT 0"))) return false;
        if (!ensureRouterCacheColumn(QStringLiteral("order_year"), QStringLiteral("order_year INTEGER NOT NULL DEFAULT 0"))) return false;
        if (!ensureRouterCacheColumn(QStringLiteral("client_uid"), QStringLiteral("client_uid TEXT"))) return false;
        if (!ensureRouterCacheColumn(QStringLiteral("vehicle_uid"), QStringLiteral("vehicle_uid TEXT"))) return false;
        if (!ensureRouterCacheColumn(QStringLiteral("crm_backfilled_at"), QStringLiteral("crm_backfilled_at TEXT"))) return false;
        if (tableExistsInDb(database, QStringLiteral("work_orders"))) {
            const QSet<QString> activeOrderCols = tableColumnsForDb(database, QStringLiteral("work_orders"));
            auto ensureActiveOrderColumn = [&](const QString& colName, const QString& ddl) {
                if (activeOrderCols.contains(colName)) return true;
                QSqlQuery alter(database);
                return alter.exec(QStringLiteral("ALTER TABLE work_orders ADD COLUMN %1;").arg(ddl));
            };
            if (!ensureActiveOrderColumn(QStringLiteral("discount_mode"), QStringLiteral("discount_mode TEXT NOT NULL DEFAULT 'legacy'"))) return false;
            if (!ensureActiveOrderColumn(QStringLiteral("client_uid"), QStringLiteral("client_uid TEXT"))) return false;
            if (!ensureActiveOrderColumn(QStringLiteral("vehicle_uid"), QStringLiteral("vehicle_uid TEXT"))) return false;
            if (!ensureActiveOrderColumn(QStringLiteral("crm_backfilled_at"), QStringLiteral("crm_backfilled_at TEXT"))) return false;
        }

        auto ensureChildColumn = [&](const QString& tableName, const QString& colName, const QString& ddl) {
            const QSet<QString> cols = tableColumnsForDb(database, tableName);
            if (cols.contains(colName)) return true;
            if (!q.exec("UPDATE router_cache_content_state SET ready=0 WHERE singleton=1 AND (EXISTS(SELECT 1 FROM router_cache_work_orders) OR EXISTS(SELECT 1 FROM router_cache_work_order_parts) OR EXISTS(SELECT 1 FROM router_cache_work_order_services) OR EXISTS(SELECT 1 FROM router_cache_outsource_repairs) OR EXISTS(SELECT 1 FROM router_cache_service_masters))")) return false;
            QSqlQuery alter(database);
            return alter.exec(QStringLiteral("ALTER TABLE %1 ADD COLUMN %2;").arg(tableName, ddl));
        };
        if (!ensureChildColumn(QStringLiteral("router_cache_work_order_services"), QStringLiteral("order_uid"), QStringLiteral("order_uid TEXT NOT NULL DEFAULT ''"))) return false;
        if (!ensureChildColumn(QStringLiteral("router_cache_work_order_services"), QStringLiteral("note"), QStringLiteral("note TEXT NOT NULL DEFAULT ''"))) return false;
        if (!ensureChildColumn(QStringLiteral("router_cache_work_order_services"), QStringLiteral("paid"), QStringLiteral("paid INTEGER NOT NULL DEFAULT 1"))) return false;
        if (!ensureChildColumn(QStringLiteral("router_cache_work_order_services"), QStringLiteral("parent_id"), QStringLiteral("parent_id INTEGER NOT NULL DEFAULT 0"))) return false;
        if (!ensureChildColumn(QStringLiteral("router_cache_work_order_parts"), QStringLiteral("order_uid"), QStringLiteral("order_uid TEXT NOT NULL DEFAULT ''"))) return false;
        if (!ensureChildColumn(QStringLiteral("router_cache_work_order_parts"), QStringLiteral("part_uid"), QStringLiteral("part_uid TEXT NOT NULL DEFAULT ''"))) return false;
        if (!ensureChildColumn(QStringLiteral("router_cache_work_order_parts"), QStringLiteral("status"), QStringLiteral("status TEXT NOT NULL DEFAULT ''"))) return false;
        if (!ensureChildColumn(QStringLiteral("router_cache_work_order_parts"), QStringLiteral("installed"), QStringLiteral("installed INTEGER NOT NULL DEFAULT 0"))) return false;
        if (!ensureChildColumn(QStringLiteral("router_cache_work_order_parts"), QStringLiteral("installed_by"), QStringLiteral("installed_by TEXT NOT NULL DEFAULT ''"))) return false;
        if (!ensureChildColumn(QStringLiteral("router_cache_work_order_parts"), QStringLiteral("request_uid"), QStringLiteral("request_uid TEXT NOT NULL DEFAULT ''"))) return false;
        if (!ensureChildColumn(QStringLiteral("router_cache_work_order_parts"), QStringLiteral("requested_at"), QStringLiteral("requested_at TEXT NOT NULL DEFAULT ''"))) return false;
        if (!ensureChildColumn(QStringLiteral("router_cache_work_order_parts"), QStringLiteral("requested_by"), QStringLiteral("requested_by TEXT NOT NULL DEFAULT ''"))) return false;
        if (!ensureChildColumn(QStringLiteral("router_cache_work_order_parts"), QStringLiteral("requested_by_uid"), QStringLiteral("requested_by_uid TEXT NOT NULL DEFAULT ''"))) return false;
        if (!ensureChildColumn(QStringLiteral("router_cache_work_order_parts"), QStringLiteral("ordered_by"), QStringLiteral("ordered_by TEXT NOT NULL DEFAULT ''"))) return false;
        if (!ensureChildColumn(QStringLiteral("router_cache_work_order_parts"), QStringLiteral("ordered_by_uid"), QStringLiteral("ordered_by_uid TEXT NOT NULL DEFAULT ''"))) return false;
        if (!ensureChildColumn(QStringLiteral("router_cache_work_order_parts"), QStringLiteral("order_comment"), QStringLiteral("order_comment TEXT NOT NULL DEFAULT ''"))) return false;
        if (!ensureChildColumn(QStringLiteral("router_cache_work_order_parts"), QStringLiteral("node_group"), QStringLiteral("node_group TEXT NOT NULL DEFAULT ''"))) return false;
        if (!ensureChildColumn(QStringLiteral("router_cache_work_order_parts"), QStringLiteral("component"), QStringLiteral("component TEXT NOT NULL DEFAULT ''"))) return false;
        if (!ensureChildColumn(QStringLiteral("router_cache_work_order_parts"), QStringLiteral("metadata_json"), QStringLiteral("metadata_json TEXT NOT NULL DEFAULT '{}'"))) return false;
        if (!ensureChildColumn(QStringLiteral("router_cache_work_order_parts"), QStringLiteral("created_at"), QStringLiteral("created_at TEXT NOT NULL DEFAULT ''"))) return false;
        if (!ensureChildColumn(QStringLiteral("router_cache_work_order_parts"), QStringLiteral("updated_at"), QStringLiteral("updated_at TEXT NOT NULL DEFAULT ''"))) return false;
        if (!ensureChildColumn(QStringLiteral("router_cache_work_order_parts"), QStringLiteral("installed_at"), QStringLiteral("installed_at TEXT NOT NULL DEFAULT ''"))) return false;
        if (!ensureChildColumn(QStringLiteral("router_cache_work_order_parts"), QStringLiteral("manufacturer"), QStringLiteral("manufacturer TEXT NOT NULL DEFAULT ''"))) return false;
        if (!ensureChildColumn(QStringLiteral("router_cache_work_order_parts"), QStringLiteral("manual_markup_override"), QStringLiteral("manual_markup_override INTEGER NOT NULL DEFAULT 0"))) return false;
        if (!ensureChildColumn(QStringLiteral("router_cache_work_order_parts"), QStringLiteral("markup_source"), QStringLiteral("markup_source TEXT NOT NULL DEFAULT 'legacy'"))) return false;
        if (!ensureChildColumn(QStringLiteral("router_cache_work_order_parts"), QStringLiteral("price_input_mode"), QStringLiteral("price_input_mode TEXT NOT NULL DEFAULT 'unknown'"))) return false;
        if (!ensureChildColumn(QStringLiteral("router_cache_work_order_parts"), QStringLiteral("pricing_updated_at"), QStringLiteral("pricing_updated_at TEXT NOT NULL DEFAULT ''"))) return false;
        if (!ensureChildColumn(QStringLiteral("router_cache_work_order_parts"), QStringLiteral("pricing_calc_version"), QStringLiteral("pricing_calc_version INTEGER NOT NULL DEFAULT 1"))) return false;
        if (!ensureChildColumn(QStringLiteral("router_cache_outsource_repairs"), QStringLiteral("order_uid"), QStringLiteral("order_uid TEXT NOT NULL DEFAULT ''"))) return false;
        if (!ensureChildColumn(QStringLiteral("router_cache_outsource_repairs"), QStringLiteral("service_id"), QStringLiteral("service_id INTEGER NOT NULL DEFAULT 0"))) return false;

        if (!q.exec("UPDATE router_cache_work_orders SET order_number=number WHERE COALESCE(order_number,0)=0")) return false;
        if (!q.exec("UPDATE router_cache_work_orders SET order_year=CASE WHEN substr(TRIM(COALESCE(opened_at,'')),5,1)='-' AND substr(TRIM(COALESCE(opened_at,'')),8,1)='-' THEN CAST(substr(TRIM(COALESCE(opened_at,'')),1,4) AS INTEGER) WHEN substr(TRIM(COALESCE(opened_at,'')),3,1)='.' AND substr(TRIM(COALESCE(opened_at,'')),6,1)='.' AND length(TRIM(COALESCE(opened_at,'')))>=8 THEN CASE WHEN substr(TRIM(COALESCE(opened_at,'')),7,4) GLOB '[0-9][0-9][0-9][0-9]' THEN CAST(substr(TRIM(COALESCE(opened_at,'')),7,4) AS INTEGER) ELSE 2000 + CAST(substr(TRIM(COALESCE(opened_at,'')),7,2) AS INTEGER) END ELSE CAST(strftime('%Y','now') AS INTEGER) END WHERE COALESCE(order_year,0)<=0")) return false;
        if (q.exec("SELECT number FROM router_cache_work_orders WHERE TRIM(COALESCE(order_uid,''))='' ORDER BY number ASC")) {
            while (q.next()) {
                QSqlQuery up(database);
                if (!up.prepare("UPDATE router_cache_work_orders SET order_uid=? WHERE number=?")) return false;
                up.addBindValue(OrderIdentityService::createOrderUid());
                up.addBindValue(q.value(0));
                if (!up.exec()) return false;
            }
            if (q.lastError().isValid()) return false;
        } else {
            return false;
        }
    }
    if (!q.exec("CREATE INDEX IF NOT EXISTS idx_router_cache_orders_status ON router_cache_work_orders(status, opened_at)")) return false;
    if (!q.exec("CREATE INDEX IF NOT EXISTS idx_router_cache_orders_order_number ON router_cache_work_orders(order_number)")) return false;
    if (!q.exec("CREATE INDEX IF NOT EXISTS idx_router_cache_orders_order_year ON router_cache_work_orders(order_year)")) return false;
    if (!q.exec("CREATE INDEX IF NOT EXISTS idx_router_cache_orders_vehicle_year ON router_cache_work_orders(car_year, order_uid)")) return false;
    if (!q.exec("CREATE UNIQUE INDEX IF NOT EXISTS idx_router_cache_orders_uid_unique ON router_cache_work_orders(order_uid)")) return false;
    if (!q.exec("CREATE UNIQUE INDEX IF NOT EXISTS idx_router_cache_orders_year_number_unique ON router_cache_work_orders(order_year, order_number)")) return false;
    if (!q.exec("CREATE INDEX IF NOT EXISTS idx_router_cache_services_order_number ON router_cache_work_order_services(order_number)")) return false;
    if (!q.exec("CREATE INDEX IF NOT EXISTS idx_router_cache_services_order_uid_id ON router_cache_work_order_services(order_uid, id)")) return false;
    if (!q.exec("CREATE INDEX IF NOT EXISTS idx_router_cache_parts_order_number ON router_cache_work_order_parts(order_number)")) return false;
    if (!q.exec("CREATE INDEX IF NOT EXISTS idx_router_cache_parts_order_uid_id ON router_cache_work_order_parts(order_uid, id)")) return false;
    if (!q.exec("CREATE INDEX IF NOT EXISTS idx_router_cache_parts_service_id ON router_cache_work_order_parts(service_id)")) return false;
    if (!q.exec("CREATE INDEX IF NOT EXISTS idx_router_cache_outsource_order_number ON router_cache_outsource_repairs(order_number)")) return false;
    if (!q.exec("CREATE INDEX IF NOT EXISTS idx_router_cache_outsource_service_id ON router_cache_outsource_repairs(service_id)")) return false;
    if (!q.exec("CREATE INDEX IF NOT EXISTS idx_router_cache_sm_service ON router_cache_service_masters(service_id)")) return false;
    // Standalone cache creation may precede the active parent identity schema.
    // Once that schema exists, a failed synchronization must reach the caller.
    QSqlQuery parentSchema(database);
    if (!parentSchema.exec(QStringLiteral("PRAGMA table_info(work_orders)"))) return false;
    bool hasParentNumber = false;
    bool hasParentUid = false;
    while (parentSchema.next()) {
        const QString column = parentSchema.value(1).toString();
        hasParentNumber |= column == QStringLiteral("number");
        hasParentUid |= column == QStringLiteral("order_uid");
    }
    if (parentSchema.lastError().isValid()) return false;
    parentSchema.finish();
    if (hasParentNumber && hasParentUid) {
        if (!q.exec("UPDATE router_cache_work_orders SET order_uid=COALESCE((SELECT NULLIF(TRIM(COALESCE(w.order_uid,'')), '') FROM work_orders w WHERE w.number=router_cache_work_orders.number), CASE WHEN COALESCE(number,0)>0 THEN 'rewrite_' || number ELSE '' END) WHERE EXISTS (SELECT 1 FROM work_orders w WHERE w.number=router_cache_work_orders.number) AND COALESCE(order_uid,'')<>COALESCE((SELECT COALESCE(NULLIF(TRIM(COALESCE(w.order_uid,'')), ''), CASE WHEN COALESCE(router_cache_work_orders.number,0)>0 THEN 'rewrite_' || router_cache_work_orders.number ELSE '' END) FROM work_orders w WHERE w.number=router_cache_work_orders.number),'')")) return false;
    }
    if (!q.exec("UPDATE router_cache_work_order_services SET order_uid=COALESCE((SELECT NULLIF(TRIM(COALESCE(wo.order_uid,'')), '') FROM router_cache_work_orders wo WHERE wo.number=router_cache_work_order_services.order_number), CASE WHEN COALESCE(order_number,0)>0 THEN 'rewrite_' || order_number ELSE '' END) WHERE EXISTS (SELECT 1 FROM router_cache_work_orders wo WHERE wo.number=router_cache_work_order_services.order_number) AND COALESCE(order_uid,'')<>COALESCE((SELECT COALESCE(NULLIF(TRIM(COALESCE(wo.order_uid,'')), ''), CASE WHEN COALESCE(router_cache_work_order_services.order_number,0)>0 THEN 'rewrite_' || router_cache_work_order_services.order_number ELSE '' END) FROM router_cache_work_orders wo WHERE wo.number=router_cache_work_order_services.order_number),'')")) return false;
    if (!q.exec("UPDATE router_cache_work_order_parts SET order_uid=COALESCE((SELECT NULLIF(TRIM(COALESCE(wo.order_uid,'')), '') FROM router_cache_work_orders wo WHERE wo.number=router_cache_work_order_parts.order_number), CASE WHEN COALESCE(order_number,0)>0 THEN 'rewrite_' || order_number ELSE '' END) WHERE EXISTS (SELECT 1 FROM router_cache_work_orders wo WHERE wo.number=router_cache_work_order_parts.order_number) AND COALESCE(order_uid,'')<>COALESCE((SELECT COALESCE(NULLIF(TRIM(COALESCE(wo.order_uid,'')), ''), CASE WHEN COALESCE(router_cache_work_order_parts.order_number,0)>0 THEN 'rewrite_' || router_cache_work_order_parts.order_number ELSE '' END) FROM router_cache_work_orders wo WHERE wo.number=router_cache_work_order_parts.order_number),'')")) return false;
    if (!q.exec("UPDATE router_cache_outsource_repairs SET order_uid=COALESCE((SELECT NULLIF(TRIM(COALESCE(wo.order_uid,'')), '') FROM router_cache_work_orders wo WHERE wo.number=router_cache_outsource_repairs.order_number), CASE WHEN COALESCE(order_number,0)>0 THEN 'rewrite_' || order_number ELSE '' END) WHERE EXISTS (SELECT 1 FROM router_cache_work_orders wo WHERE wo.number=router_cache_outsource_repairs.order_number) AND COALESCE(order_uid,'')<>COALESCE((SELECT COALESCE(NULLIF(TRIM(COALESCE(wo.order_uid,'')), ''), CASE WHEN COALESCE(router_cache_outsource_repairs.order_number,0)>0 THEN 'rewrite_' || router_cache_outsource_repairs.order_number ELSE '' END) FROM router_cache_work_orders wo WHERE wo.number=router_cache_outsource_repairs.order_number),'')")) return false;
    return true;
}

bool refreshArchiveIntoRouterCache(QSqlDatabase database)
{
    if (!ensureRouterCacheSchema(database)) return false;
    const QString legacyPath = AppRuntime::legacyDbFilePath();
    const QString archivePath = AppRuntime::archiveDbFilePath();
    if (QFileInfo::exists(legacyPath)
        && QFileInfo(archivePath).absoluteFilePath() != QFileInfo(legacyPath).absoluteFilePath()
        && !QFileInfo::exists(archivePath)) {
        QDir().mkpath(QFileInfo(archivePath).absolutePath());
        QFile::copy(legacyPath, archivePath);
    }

    ArchiveRouterSourceSchema sourceSchema;
    QString sourceSchemaError;
    if (!prepareArchiveRouterSourceSchema(database, archivePath, legacyPath, &sourceSchema, &sourceSchemaError)) {
        qWarning() << "Prepare archive router source schema failed:" << sourceSchemaError;
        return false;
    }

    MutationExecutor::Request request;
    request.sourceModule = QStringLiteral("DbArchiveRoutingSupport::forceRebuildRouterCacheFromActiveWorkspace");
    request.entityType = QStringLiteral("router_cache");
    request.entityId = QStringLiteral("active_workspace");
    request.operation = QStringLiteral("force_rebuild_router_cache_from_active_workspace");
    request.changedTables = {QStringLiteral("router_cache_content_state"), QStringLiteral("router_cache_work_orders"), QStringLiteral("router_cache_work_order_services"), QStringLiteral("router_cache_work_order_parts"), QStringLiteral("router_cache_service_masters"), QStringLiteral("router_cache_outsource_repairs"), QStringLiteral("masters"), QStringLiteral("work_orders"), QStringLiteral("work_order_services"), QStringLiteral("work_order_parts"), QStringLiteral("outsource_repairs")};
    request.beginErrorFallback = QStringLiteral("Router cache rebuild transaction begin failed");
    request.commitErrorFallback = QStringLiteral("Router cache rebuild transaction commit failed");

    const auto tx = MutationExecutor::execute(database, request, [&](QSqlDatabase& txDb, QString* txError) {
        QSqlDatabase database = txDb;
        auto fail = [&](const QString& msg) {
            qWarning() << msg;
            if (txError) *txError = msg;
            return false;
        };

        if (!routerCacheContentReady(database)) {
            QString coverageError;
            if (!validateRouterCacheReconstructionSources(database, sourceSchema.legacyAttached, &coverageError))
                return fail(coverageError);
        }
        QSqlQuery clear(txDb);
        for (const auto& table : {"router_cache_service_masters", "router_cache_outsource_repairs",
                                  "router_cache_work_order_parts", "router_cache_work_order_services",
                                  "router_cache_work_orders"}) {
            if (!clear.exec(QStringLiteral("DELETE FROM %1").arg(QString::fromLatin1(table))))
                return fail(clear.lastError().text());
        }

        QString importError;
        if (!sourceSchema.useActiveFallbackOnly
                && !importArchiveSourceIntoRouterCache(database, sourceSchema, &importError)) {
            return fail(importError);
        }

        QString backfillError;
        if (!mergeActiveWorkspaceIntoRouterCache(database, &backfillError)) {
            return fail(backfillError);
        }

        return true;
    });

    if (sourceSchema.legacyAttached) detachArchiveRouterSource(database);

    if (!tx.ok) {
        qWarning() << "Router cache commit failed:" << tx.errorText;
        return false;
    }
    return true;
}

bool forceRebuildRouterCacheFromActiveWorkspace(QSqlDatabase database, QString* errorText)
{
    if (!ensureRouterCacheSchema(database)) {
        return false;
    }
    if (!tableExistsInDb(database, QStringLiteral("work_orders"))
            || !tableExistsInDb(database, QStringLiteral("work_order_services"))
            || !tableExistsInDb(database, QStringLiteral("work_order_parts"))) {
        return true;
    }

    MutationExecutor::Request request;
    request.sourceModule = QStringLiteral("DbArchiveRoutingSupport");
    request.entityType = QStringLiteral("router_cache");
    request.entityId = QStringLiteral("active_workspace");
    request.operation = QStringLiteral("force_rebuild_router_cache_from_active_workspace");
    request.description = QStringLiteral("Rebuild router cache tables from active workspace");
    request.changedTables = {QStringLiteral("router_cache_content_state"), QStringLiteral("router_cache_service_masters"), QStringLiteral("router_cache_outsource_repairs"), QStringLiteral("router_cache_work_order_parts"), QStringLiteral("router_cache_work_order_services"), QStringLiteral("router_cache_work_orders")};
    request.beginErrorFallback = QStringLiteral("Не вдалося розпочати перебудову router cache.");
    request.commitErrorFallback = QStringLiteral("Не вдалося завершити перебудову router cache.");

    const auto tx = MutationExecutor::execute(database, request, [&](QSqlDatabase& txDb, QString* txError) {
        QString rebuildError;
        if (!rebuildRouterCacheFromActiveWorkspaceData(txDb, &rebuildError)) {
            if (txError) *txError = rebuildError;
            qWarning() << "forceRebuildRouterCacheFromActiveWorkspace failed:" << rebuildError;
            return false;
        }
        return true;
    });
    if (!tx.ok) {
        if (errorText) *errorText = tx.errorText;
        return false;
    }
    return true;
}


bool rebuildRouterViews(QSqlDatabase database)
{
    if (!database.isValid()) return false;
    if (!database.isOpen() && !database.open()) return false;
    if (!ensureLocalRegistryMirrorSchema(database) || !ensureWorkspaceOrderStateSchema(database) || !ensureRouterCacheSchema(database)) return false;

    QSqlQuery q(database);
    const QString closedPredicate = OrderStoragePolicyService::closedOrderPredicate(QStringLiteral("o"));
    const QString activePathSql = sqlQuoteText(AppRuntime::activeDbFilePath());

    q.exec("DROP VIEW IF EXISTS router_work_orders");
    if (!q.exec(
            "CREATE VIEW router_work_orders AS "
            "SELECT o.number, o.order_uid, o.order_number, o.order_year, o.opened_at, o.closed_at, o.status, o.plate, o.plate_norm, o.vin, o.car_year, o.odometer, o.engine_type, o.engine_volume, o.fuel_type, o.car, o.client, o.client_phone, o.master_id, o.works_sum, o.parts_sum, o.discount, o.master_name_closed, o.car_model, o.car_brand, o.car_kind, o.color, o.car_color, o.owner_name, o.client_uid, o.vehicle_uid, o.crm_backfilled_at, o.discount_mode FROM work_orders o "
            "UNION ALL "
            "SELECT c.number, c.order_uid, c.order_number, c.order_year, c.opened_at, c.closed_at, c.status, c.plate, c.plate_norm, c.vin, c.car_year, c.odometer, c.engine_type, c.engine_volume, c.fuel_type, c.car, c.client, c.client_phone, c.master_id, c.works_sum, c.parts_sum, c.discount, c.master_name_closed, c.car_model, c.car_brand, c.car_kind, c.color, c.car_color, c.owner_name, c.client_uid, c.vehicle_uid, c.crm_backfilled_at, c.discount_mode "
            "FROM router_cache_work_orders c "
            "WHERE NOT EXISTS (SELECT 1 FROM work_orders o WHERE o.number=c.number)")) {
        qWarning() << "router_work_orders create failed:" << q.lastError().text();
        return false;
    }
    q.exec("DROP VIEW IF EXISTS router_work_order_services");
    if (!q.exec(
            "CREATE VIEW router_work_order_services AS "
            "SELECT s.id, s.order_uid, s.order_number, s.title, s.qty, s.price, s.amount, COALESCE(s.note,''), COALESCE(s.paid,1), COALESCE(s.parent_id,0) FROM work_order_services s "
            "UNION ALL "
            "SELECT c.id, c.order_uid, c.order_number, c.title, c.qty, c.price, c.amount, c.note, c.paid, c.parent_id "
            "FROM router_cache_work_order_services c "
            "WHERE NOT EXISTS (SELECT 1 FROM work_orders o WHERE o.number=c.order_number)")) {
        qWarning() << "router_work_order_services create failed:" << q.lastError().text();
        return false;
    }
    q.exec("DROP VIEW IF EXISTS router_service_masters");
    if (!q.exec(
            "CREATE VIEW router_service_masters AS "
            "SELECT sm.service_id, sm.master_id, sm.share_percent, sm.role, sm.master_name_closed FROM service_masters sm "
            "UNION ALL "
            "SELECT c.service_id, c.master_id, c.share_percent, c.role, c.master_name_closed "
            "FROM router_cache_service_masters c "
            "JOIN router_cache_work_order_services rs ON rs.id=c.service_id "
            "WHERE NOT EXISTS (SELECT 1 FROM work_orders o WHERE o.number=rs.order_number) "
            "  AND NOT EXISTS (SELECT 1 FROM service_masters sm WHERE sm.service_id=c.service_id AND sm.master_id=c.master_id)")) {
        qWarning() << "router_service_masters create failed:" << q.lastError().text();
        return false;
    }
    q.exec("DROP VIEW IF EXISTS router_work_order_parts");
    if (!q.exec(
            "CREATE VIEW router_work_order_parts AS "
            "SELECT p.id, p.order_uid, p.order_number, p.ordered_at, p.delivery_date, p.installed_at, p.store, p.part_number, p.name, p.manufacturer, p.note, p.qty, p.price, p.amount, p.service_id, p.ordered_by_admin, p.paid, p.purchase_price, p.purchase_amount, p.markup_percent, p.manual_markup_override, p.markup_source, p.pricing_updated_at, p.pricing_calc_version "
            "FROM work_order_parts p "
            "UNION ALL "
            "SELECT c.id, c.order_uid, c.order_number, c.ordered_at, c.delivery_date, c.installed_at, c.store, c.part_number, c.name, c.manufacturer, c.note, c.qty, c.price, c.amount, c.service_id, c.ordered_by_admin, c.paid, c.purchase_price, c.purchase_amount, c.markup_percent, c.manual_markup_override, c.markup_source, c.pricing_updated_at, c.pricing_calc_version "
            "FROM router_cache_work_order_parts c "
            "WHERE NOT EXISTS (SELECT 1 FROM work_orders o WHERE o.number=c.order_number)")) {
        qWarning() << "router_work_order_parts create failed:" << q.lastError().text();
        return false;
    }
    q.exec("DROP VIEW IF EXISTS router_outsource_repairs");
    if (!q.exec(
            "CREATE VIEW router_outsource_repairs AS "
            "SELECT r.id, r.order_uid, r.order_number, r.sent_at, r.company, r.part_name, r.repair_cost, r.client_price, r.note, r.created_at, r.service_id FROM outsource_repairs r "
            "UNION ALL "
            "SELECT c.id, c.order_uid, c.order_number, c.sent_at, c.company, c.part_name, c.repair_cost, c.client_price, c.note, c.created_at, c.service_id "
            "FROM router_cache_outsource_repairs c "
            "WHERE NOT EXISTS (SELECT 1 FROM work_orders o WHERE o.number=c.order_number)")) {
        qWarning() << "router_outsource_repairs create failed:" << q.lastError().text();
        return false;
    }

    q.exec("DROP VIEW IF EXISTS router_order_locator");
    if (!q.exec(QStringLiteral(
        "CREATE VIEW router_order_locator AS "
        "SELECT src.number AS number, src.order_uid AS order_uid, src.order_number AS order_number, src.order_year AS order_year, "
        "       src.opened_at AS opened_at, src.closed_at AS closed_at, src.status AS status, src.plate AS plate, src.plate_norm AS plate_norm, "
        "       src.vin AS vin, src.car AS car, src.client AS client, src.master_id AS master_id, src.master_name_closed AS master_name_closed, "
        "       src.works_sum AS works_sum, src.parts_sum AS parts_sum, src.storage_kind AS storage_kind, src.storage_path AS storage_path, "
        "       src.source_db AS source_db, src.workspace_origin AS workspace_origin, src.is_in_active AS is_in_active, src.is_editable_copy AS is_editable_copy "
        "FROM ("
        "  SELECT r.order_number AS number, "
        "         COALESCE(NULLIF(w.order_uid,''), NULLIF(r.order_uid,''), '') AS order_uid, "
        "         COALESCE(NULLIF(w.order_number,0), NULLIF(r.visible_order_number,0), COALESCE(w.number, r.order_number), 0) AS order_number, "
        "         COALESCE(NULLIF(w.order_year,0), NULLIF(r.order_year,0), CAST(strftime('%%Y', substr(COALESCE(NULLIF(w.opened_at,''), r.opened_at, ''),1,10)) AS INTEGER), CAST(strftime('%%Y','now') AS INTEGER)) AS order_year, "
        "         COALESCE(NULLIF(w.opened_at,''), r.opened_at, '') AS opened_at, "
        "         COALESCE(NULLIF(w.closed_at,''), r.closed_at, '') AS closed_at, "
        "         COALESCE(NULLIF(w.status,''), r.status, '') AS status, "
        "         COALESCE(NULLIF(w.plate,''), r.plate, '') AS plate, "
        "         COALESCE(NULLIF(w.plate_norm,''), r.plate_norm, '') AS plate_norm, "
        "         COALESCE(NULLIF(w.vin,''), r.vin, '') AS vin, "
        "         COALESCE(NULLIF(w.car,''), r.car, '') AS car, "
        "         COALESCE(NULLIF(w.client,''), r.client, '') AS client, "
        "         COALESCE(NULLIF(w.master_id,''), r.master_id, '') AS master_id, "
        "         COALESCE(NULLIF(w.master_name_closed,''), r.master_name_closed, '') AS master_name_closed, "
        "         COALESCE(w.works_sum, r.works_sum, 0) AS works_sum, "
        "         COALESCE(w.parts_sum, r.parts_sum, 0) AS parts_sum, "
        "         CASE "
        "             WHEN COALESCE(ws.origin,'')='legacy_copy' THEN COALESCE(NULLIF(r.storage_kind,''), 'archive') "
        "             WHEN w.number IS NOT NULL THEN 'active' "
        "             ELSE COALESCE(NULLIF(r.storage_kind,''), 'archive') "
        "         END AS storage_kind, "
        "         CASE "
        "             WHEN COALESCE(ws.origin,'')='legacy_copy' THEN COALESCE(NULLIF(ws.source_db,''), NULLIF(r.storage_path,''), '') "
        "             WHEN w.number IS NOT NULL THEN %1 "
        "             ELSE COALESCE(r.storage_path,'') "
        "         END AS storage_path, "
        "         CASE "
        "             WHEN COALESCE(ws.origin,'')='legacy_copy' THEN COALESCE(NULLIF(ws.source_db,''), NULLIF(r.source_db,''), NULLIF(r.storage_path,''), '') "
        "             WHEN w.number IS NOT NULL THEN '' "
        "             ELSE COALESCE(r.source_db,'') "
        "         END AS source_db, "
        "         COALESCE(NULLIF(ws.origin,''), CASE WHEN w.number IS NOT NULL THEN 'native' ELSE '' END) AS workspace_origin, "
        "         CASE WHEN w.number IS NOT NULL THEN 1 ELSE 0 END AS is_in_active, "
        "         CASE WHEN w.number IS NOT NULL AND COALESCE(ws.origin,'')='legacy_copy' THEN 1 ELSE 0 END AS is_editable_copy "
        "  FROM order_registry r "
        "  LEFT JOIN work_orders w ON w.number=r.order_number "
        "  LEFT JOIN workspace_order_state ws ON ws.order_number=r.order_number "
        "  UNION ALL "
        "  SELECT w.number AS number, COALESCE(w.order_uid,'') AS order_uid, COALESCE(w.order_number,w.number) AS order_number, "
        "         COALESCE(NULLIF(w.order_year,0), CAST(strftime('%%Y', substr(COALESCE(w.opened_at,''),1,10)) AS INTEGER), CAST(strftime('%%Y','now') AS INTEGER)) AS order_year, "
        "         COALESCE(w.opened_at,'') AS opened_at, COALESCE(w.closed_at,'') AS closed_at, COALESCE(w.status,'') AS status, "
        "         COALESCE(w.plate,'') AS plate, COALESCE(w.plate_norm,'') AS plate_norm, COALESCE(w.vin,'') AS vin, COALESCE(w.car,'') AS car, COALESCE(w.client,'') AS client, "
        "         COALESCE(w.master_id,'') AS master_id, COALESCE(w.master_name_closed,'') AS master_name_closed, COALESCE(w.works_sum,0) AS works_sum, COALESCE(w.parts_sum,0) AS parts_sum, "
        "         CASE WHEN COALESCE(ws.origin,'')='legacy_copy' THEN 'archive' ELSE 'active' END AS storage_kind, "
        "         CASE WHEN COALESCE(ws.origin,'')='legacy_copy' THEN COALESCE(NULLIF(ws.source_db,''), '') ELSE %1 END AS storage_path, "
        "         CASE WHEN COALESCE(ws.origin,'')='legacy_copy' THEN COALESCE(NULLIF(ws.source_db,''), '') ELSE '' END AS source_db, "
        "         COALESCE(NULLIF(ws.origin,''), 'native') AS workspace_origin, 1 AS is_in_active, "
        "         CASE WHEN COALESCE(ws.origin,'')='legacy_copy' THEN 1 ELSE 0 END AS is_editable_copy "
        "  FROM work_orders w "
        "  LEFT JOIN order_registry r ON r.order_number=w.number "
        "  LEFT JOIN workspace_order_state ws ON ws.order_number=w.number "
        "  WHERE r.order_number IS NULL"
        " ) src").arg(activePathSql))) {
        qWarning() << "router_order_locator create failed:" << q.lastError().text();
        return false;
    }

    q.exec("DROP VIEW IF EXISTS router_closed_order_locator");
    if (!q.exec(QStringLiteral("CREATE VIEW router_closed_order_locator AS SELECT o.* FROM router_order_locator o WHERE %1").arg(closedPredicate))) {
        qWarning() << "router_closed_order_locator create failed:" << q.lastError().text();
        return false;
    }
    q.exec("DROP VIEW IF EXISTS router_open_order_locator");
    if (!q.exec(QStringLiteral("CREATE VIEW router_open_order_locator AS SELECT o.* FROM router_order_locator o WHERE o.is_in_active=1 AND NOT %1").arg(closedPredicate))) {
        qWarning() << "router_open_order_locator create failed:" << q.lastError().text();
        return false;
    }

    q.exec("DROP VIEW IF EXISTS router_closed_orders");
    q.exec(QStringLiteral("CREATE VIEW router_closed_orders AS SELECT o.* FROM router_work_orders o WHERE %1").arg(closedPredicate));
    q.exec("DROP VIEW IF EXISTS router_open_orders");
    q.exec(QStringLiteral("CREATE VIEW router_open_orders AS SELECT o.* FROM router_work_orders o WHERE NOT %1").arg(closedPredicate));
    QString searchError;
    if (!OrderSearchIndex::ensure(database, &searchError)) {
        qWarning() << "Order search index failed:" << searchError;
        return false;
    }
    return true;
}

static bool copyCachedOrderToActive(QSqlDatabase database, const QString& requestedCanonicalUid, int orderNumber, QString* errorText)
{
    const QString requestedUid = requestedCanonicalUid.trimmed();
    if (!ensureRouterCacheSchema(database) || !ensureWorkspaceOrderStateSchema(database)) {
        if (errorText) *errorText = QStringLiteral("Router cache or workspace state schema is unavailable");
        return false;
    }

    QSqlQuery exists(database);
    exists.prepare("SELECT 1 FROM router_cache_work_orders WHERE number=? LIMIT 1");
    exists.addBindValue(orderNumber);
    if (!exists.exec() || !exists.next()) {
        if (errorText) *errorText = QStringLiteral("Order %1 not found in router cache").arg(orderNumber);
        return false;
    }

    QString existingOrigin;
    const bool hasState = workspaceOrderStateLookup(database, orderNumber, &existingOrigin);
    QSqlQuery activeExists(database);
    activeExists.prepare("SELECT 1 FROM work_orders WHERE number=? LIMIT 1");
    activeExists.addBindValue(orderNumber);
    const bool hasActiveOrder = activeExists.exec() && activeExists.next();
    if (hasActiveOrder) {
        if (hasState && existingOrigin != QStringLiteral("native")) {
            if (errorText) *errorText = QStringLiteral("Order %1 is already editable in workspace").arg(orderNumber);
            return true;
        }
        QString purgeError;
        if (!purgeActiveOrderGraph(database, orderNumber, &purgeError)) {
            if (errorText) *errorText = purgeError;
            return false;
        }
    }

    MutationExecutor::Request request;
    request.sourceModule = QStringLiteral("DbArchiveRoutingSupport::copyCachedOrderToActive");
    request.entityType = QStringLiteral("order");
    request.entityId = QString::number(orderNumber);
    request.operation = QStringLiteral("copy_cached_order_to_active");
    request.changedTables = {QStringLiteral("work_orders"), QStringLiteral("work_order_services"), QStringLiteral("service_masters"), QStringLiteral("work_order_parts"), QStringLiteral("outsource_repairs"), QStringLiteral("outsource_jobs"), QStringLiteral("workspace_order_state")};
    request.beginErrorFallback = QStringLiteral("Cached order copy transaction begin failed");
    request.commitErrorFallback = QStringLiteral("Cached order copy transaction commit failed");

    const auto tx = MutationExecutor::execute(database, request, [&](QSqlDatabase& txDb, QString* txError) {
        QSqlDatabase database = txDb;
        if (!hydrateCachedOrderToActive(database, requestedUid, orderNumber, txError)) {
            return false;
        }
        if (!upsertWorkspaceOrderState(database, orderNumber, QStringLiteral("legacy_copy"), AppRuntime::archiveDbFilePath())) {
            if (txError) *txError = QStringLiteral("Failed to mark workspace copy state for order %1").arg(orderNumber);
            return false;
        }
        return true;
    });

    if (!tx.ok) {
        if (errorText) *errorText = tx.errorText;
        return false;
    }
    return true;
}
