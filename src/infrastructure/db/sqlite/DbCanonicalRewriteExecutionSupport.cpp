#include "infrastructure/db/sqlite/DbCanonicalRewriteExecutionSupport.h"

#include "infrastructure/db/sqlite/DbArchiveInternals.h"
#include "infrastructure/db/sqlite/DbFacadeSupport.h"
#include "infrastructure/db/sqlite/DbWorkspaceOrderStateSupport.h"
#include "runtime/AppRuntime.h"
#include "core/services/OrderRoutingLookupService.h"

#include <QFileInfo>
#include <QScopeGuard>
#include <QSqlError>
#include <QSqlQuery>

bool ensureCanonicalRewriteSupportSchema(QSqlDatabase database, QString* errorText)
{
    if (!database.isValid()) {
        if (errorText) *errorText = QStringLiteral("Main DB connection is invalid");
        return false;
    }
    if (!database.isOpen() && !database.open()) {
        if (errorText) *errorText = database.lastError().text();
        return false;
    }

    auto fail = [&](const QString& msg) {
        if (errorText) *errorText = msg;
        return false;
    };
    auto ensureSupportColumn = [&](const QString& tableName, const QString& columnName, const QString& ddl) {
        return ensureColumnForDb(database, tableName, columnName, ddl, errorText);
    };

    QSqlQuery transaction(database);
    if (!transaction.exec(QStringLiteral("SAVEPOINT canonical_rewrite_support"))) {
        return fail(QStringLiteral("Begin canonical support preparation failed: %1").arg(transaction.lastError().text()));
    }
    auto rollback = qScopeGuard([&] {
        QSqlQuery cleanup(database);
        for (const QString& sql : {QStringLiteral("ROLLBACK TO canonical_rewrite_support"),
                                   QStringLiteral("RELEASE canonical_rewrite_support")}) {
            if (!cleanup.exec(sql)) {
                const QString detail = QStringLiteral("Canonical support rollback failed: %1").arg(cleanup.lastError().text());
                if (errorText) *errorText += QStringLiteral("; ") + detail;
                qWarning() << detail;
            }
        }
    });
    QSqlQuery q(database);
    auto normalize = [&](const QString& sql) {
        if (q.exec(sql)) return true;
        return fail(QStringLiteral("Canonical support normalization failed: %1").arg(q.lastError().text()));
    };
    if (!q.exec(
            "CREATE TABLE IF NOT EXISTS settings ("
            "  key TEXT PRIMARY KEY,"
            "  value TEXT NOT NULL DEFAULT ''"
            ");")) {
        return fail(QStringLiteral("Create settings failed: %1").arg(q.lastError().text()));
    }

    if (!q.exec(
            "CREATE TABLE IF NOT EXISTS deleted_imports ("
            "  source TEXT NOT NULL,"
            "  external_id TEXT NOT NULL,"
            "  deleted_at TEXT NOT NULL DEFAULT (datetime('now')),"
            "  PRIMARY KEY(source, external_id)"
            ");")) {
        return fail(QStringLiteral("Create deleted_imports failed: %1").arg(q.lastError().text()));
    }

    if (!ensureSupportColumn(QStringLiteral("work_orders"), QStringLiteral("order_uid"), QStringLiteral("order_uid TEXT NOT NULL DEFAULT ''"))) return false;
    if (!ensureSupportColumn(QStringLiteral("work_orders"), QStringLiteral("order_number"), QStringLiteral("order_number INTEGER NOT NULL DEFAULT 0"))) return false;
    if (!ensureSupportColumn(QStringLiteral("work_orders"), QStringLiteral("order_year"), QStringLiteral("order_year INTEGER NOT NULL DEFAULT 0"))) return false;
    if (!ensureSupportColumn(QStringLiteral("work_orders"), QStringLiteral("master_name_closed"), QStringLiteral("master_name_closed TEXT NOT NULL DEFAULT ''"))) return false;
    if (!ensureSupportColumn(QStringLiteral("work_orders"), QStringLiteral("car_model"), QStringLiteral("car_model TEXT NOT NULL DEFAULT ''"))) return false;
    if (!ensureSupportColumn(QStringLiteral("work_orders"), QStringLiteral("car_brand"), QStringLiteral("car_brand TEXT NOT NULL DEFAULT ''"))) return false;
    if (!ensureSupportColumn(QStringLiteral("work_orders"), QStringLiteral("car_kind"), QStringLiteral("car_kind TEXT NOT NULL DEFAULT ''"))) return false;
    if (!ensureSupportColumn(QStringLiteral("work_orders"), QStringLiteral("owner_name"), QStringLiteral("owner_name TEXT NOT NULL DEFAULT ''"))) return false;
    if (!ensureSupportColumn(QStringLiteral("work_orders"), QStringLiteral("color"), QStringLiteral("color TEXT NOT NULL DEFAULT ''"))) return false;
    if (!ensureSupportColumn(QStringLiteral("work_orders"), QStringLiteral("car_color"), QStringLiteral("car_color TEXT NOT NULL DEFAULT ''"))) return false;
    if (!ensureSupportColumn(QStringLiteral("work_orders"), QStringLiteral("vin"), QStringLiteral("vin TEXT NOT NULL DEFAULT ''"))) return false;
    if (!ensureSupportColumn(QStringLiteral("work_orders"), QStringLiteral("engine_type"), QStringLiteral("engine_type TEXT NOT NULL DEFAULT ''"))) return false;
    if (!ensureSupportColumn(QStringLiteral("work_orders"), QStringLiteral("engine_volume"), QStringLiteral("engine_volume TEXT NOT NULL DEFAULT ''"))) return false;
    if (!ensureSupportColumn(QStringLiteral("work_order_services"), QStringLiteral("order_uid"), QStringLiteral("order_uid TEXT NOT NULL DEFAULT ''"))) return false;
    if (!ensureSupportColumn(QStringLiteral("work_order_services"), QStringLiteral("note"), QStringLiteral("note TEXT NOT NULL DEFAULT ''"))) return false;
    if (!ensureSupportColumn(QStringLiteral("work_order_services"), QStringLiteral("paid"), QStringLiteral("paid INTEGER NOT NULL DEFAULT 1"))) return false;
    if (!ensureSupportColumn(QStringLiteral("work_order_services"), QStringLiteral("parent_id"), QStringLiteral("parent_id INTEGER"))) return false;
    if (!ensureSupportColumn(QStringLiteral("work_order_parts"), QStringLiteral("order_uid"), QStringLiteral("order_uid TEXT NOT NULL DEFAULT ''"))) return false;
    if (!ensureSupportColumn(QStringLiteral("work_order_parts"), QStringLiteral("delivery_date"), QStringLiteral("delivery_date TEXT NOT NULL DEFAULT ''"))) return false;
    if (!ensureSupportColumn(QStringLiteral("work_order_parts"), QStringLiteral("installed_at"), QStringLiteral("installed_at TEXT NOT NULL DEFAULT ''"))) return false;
    if (!ensureSupportColumn(QStringLiteral("work_order_parts"), QStringLiteral("store"), QStringLiteral("store TEXT NOT NULL DEFAULT ''"))) return false;
    if (!ensureSupportColumn(QStringLiteral("work_order_parts"), QStringLiteral("part_number"), QStringLiteral("part_number TEXT"))) return false;
    if (!ensureSupportColumn(QStringLiteral("work_order_parts"), QStringLiteral("note"), QStringLiteral("note TEXT NOT NULL DEFAULT ''"))) return false;
    if (!ensureSupportColumn(QStringLiteral("work_order_parts"), QStringLiteral("service_id"), QStringLiteral("service_id INTEGER NOT NULL DEFAULT 0"))) return false;
    if (!ensureSupportColumn(QStringLiteral("work_order_parts"), QStringLiteral("ordered_by_admin"), QStringLiteral("ordered_by_admin INTEGER NOT NULL DEFAULT 1"))) return false;
    if (!ensureSupportColumn(QStringLiteral("work_order_parts"), QStringLiteral("paid"), QStringLiteral("paid INTEGER NOT NULL DEFAULT 0"))) return false;
    if (!ensureSupportColumn(QStringLiteral("work_order_parts"), QStringLiteral("purchase_price"), QStringLiteral("purchase_price REAL NOT NULL DEFAULT 0"))) return false;
    if (!ensureSupportColumn(QStringLiteral("work_order_parts"), QStringLiteral("purchase_amount"), QStringLiteral("purchase_amount REAL NOT NULL DEFAULT 0"))) return false;
    if (!ensureSupportColumn(QStringLiteral("work_order_parts"), QStringLiteral("markup_percent"), QStringLiteral("markup_percent REAL NOT NULL DEFAULT 0"))) return false;
    if (!ensureSupportColumn(QStringLiteral("work_order_parts"), QStringLiteral("manual_markup_override"), QStringLiteral("manual_markup_override INTEGER NOT NULL DEFAULT 0"))) return false;
    if (!ensureSupportColumn(QStringLiteral("work_order_parts"), QStringLiteral("markup_source"), QStringLiteral("markup_source TEXT NOT NULL DEFAULT 'policy'"))) return false;
    if (!ensureSupportColumn(QStringLiteral("work_order_parts"), QStringLiteral("pricing_updated_at"), QStringLiteral("pricing_updated_at TEXT NOT NULL DEFAULT ''"))) return false;
    if (!ensureSupportColumn(QStringLiteral("work_order_parts"), QStringLiteral("pricing_calc_version"), QStringLiteral("pricing_calc_version INTEGER NOT NULL DEFAULT 2"))) return false;
    if (!ensureSupportColumn(QStringLiteral("outsource_repairs"), QStringLiteral("order_uid"), QStringLiteral("order_uid TEXT NOT NULL DEFAULT ''"))) return false;
    if (!ensureSupportColumn(QStringLiteral("outsource_repairs"), QStringLiteral("note"), QStringLiteral("note TEXT NOT NULL DEFAULT ''"))) return false;
    if (!ensureSupportColumn(QStringLiteral("outsource_repairs"), QStringLiteral("created_at"), QStringLiteral("created_at TEXT NOT NULL DEFAULT ''"))) return false;
    if (!ensureSupportColumn(QStringLiteral("outsource_repairs"), QStringLiteral("service_id"), QStringLiteral("service_id INTEGER"))) return false;
    if (!ensureSupportColumn(QStringLiteral("outsource_jobs"), QStringLiteral("order_uid"), QStringLiteral("order_uid TEXT NOT NULL DEFAULT ''"))) return false;
    if (!ensureSupportColumn(QStringLiteral("outsource_jobs"), QStringLiteral("order_number"), QStringLiteral("order_number INTEGER NOT NULL DEFAULT 0"))) return false;
    if (!ensureSupportColumn(QStringLiteral("outsource_jobs"), QStringLiteral("service_id"), QStringLiteral("service_id INTEGER NOT NULL DEFAULT 0"))) return false;
    if (!ensureSupportColumn(QStringLiteral("outsource_jobs"), QStringLiteral("company_id"), QStringLiteral("company_id INTEGER NOT NULL DEFAULT 0"))) return false;
    if (!ensureSupportColumn(QStringLiteral("outsource_jobs"), QStringLiteral("company_name"), QStringLiteral("company_name TEXT NOT NULL DEFAULT ''"))) return false;
    if (!ensureSupportColumn(QStringLiteral("outsource_jobs"), QStringLiteral("sent_date"), QStringLiteral("sent_date TEXT NOT NULL DEFAULT ''"))) return false;
    if (!ensureSupportColumn(QStringLiteral("outsource_jobs"), QStringLiteral("return_date"), QStringLiteral("return_date TEXT NOT NULL DEFAULT ''"))) return false;
    if (!ensureSupportColumn(QStringLiteral("outsource_jobs"), QStringLiteral("part_name"), QStringLiteral("part_name TEXT NOT NULL DEFAULT ''"))) return false;
    if (!ensureSupportColumn(QStringLiteral("outsource_jobs"), QStringLiteral("company_price"), QStringLiteral("company_price REAL NOT NULL DEFAULT 0"))) return false;
    if (!ensureSupportColumn(QStringLiteral("outsource_jobs"), QStringLiteral("markup_percent"), QStringLiteral("markup_percent REAL NOT NULL DEFAULT 0"))) return false;
    if (!ensureSupportColumn(QStringLiteral("outsource_jobs"), QStringLiteral("client_price"), QStringLiteral("client_price REAL NOT NULL DEFAULT 0"))) return false;
    if (!ensureSupportColumn(QStringLiteral("outsource_jobs"), QStringLiteral("note"), QStringLiteral("note TEXT NOT NULL DEFAULT ''"))) return false;

    const QSet<QString> workOrderCols = tableColumnsForDb(database, QStringLiteral("work_orders"));
    if (workOrderCols.contains(QStringLiteral("status"))) {
        if (!normalize(QStringLiteral("UPDATE work_orders SET status='В роботі' WHERE TRIM(COALESCE(status,''))=''"))) return false;
    }
    if (workOrderCols.contains(QStringLiteral("order_uid"))) {
        if (!normalize(QStringLiteral("UPDATE work_orders SET order_uid = lower(hex(randomblob(16))) WHERE TRIM(COALESCE(order_uid,''))=''"))) return false;
    }
    if (workOrderCols.contains(QStringLiteral("order_number"))) {
        if (!normalize(QStringLiteral("UPDATE work_orders SET order_number = COALESCE(NULLIF(order_number,0), number)"))) return false;
    }
    if (workOrderCols.contains(QStringLiteral("order_year"))) {
        if (!normalize(QStringLiteral("UPDATE work_orders SET order_year = COALESCE(NULLIF(order_year,0), CAST(strftime('%Y', (CASE WHEN substr(trim(COALESCE(opened_at,'')),3,1)='.' AND substr(trim(COALESCE(opened_at,'')),6,1)='.' THEN CASE WHEN substr(trim(COALESCE(opened_at,'')),7,4) GLOB '[0-9][0-9][0-9][0-9]' THEN substr(trim(COALESCE(opened_at,'')),7,4) ELSE '20'||substr(trim(COALESCE(opened_at,'')),7,2) END||'-'||substr(trim(COALESCE(opened_at,'')),4,2)||'-'||substr(trim(COALESCE(opened_at,'')),1,2) ELSE COALESCE(opened_at,'') END)) AS INTEGER), CAST(strftime('%Y','now') AS INTEGER))"))) return false;
    }

    // Cache and its children must receive the final parent UID on the first preparation.
    if (!ensureRouterCacheSchema(database)) {
        return fail(QStringLiteral("Router cache schema bootstrap failed"));
    }

    // Resolve parent identity before filling missing registry and workspace identities.
    if (!ensureLocalRegistryMirrorSchema(database)) {
        return fail(QStringLiteral("Local registry mirror schema bootstrap failed"));
    }
    if (!ensureWorkspaceOrderStateSchema(database)) {
        return fail(QStringLiteral("Workspace order state schema bootstrap failed"));
    }

    const QSet<QString> serviceCols = tableColumnsForDb(database, QStringLiteral("work_order_services"));
    if (serviceCols.contains(QStringLiteral("order_uid"))) {
        if (!normalize(QStringLiteral(
            "UPDATE work_order_services "
            "SET order_uid=(SELECT COALESCE(wo.order_uid,'') FROM work_orders wo WHERE wo.number=work_order_services.order_number LIMIT 1) "
            "WHERE TRIM(COALESCE(order_uid,''))=''"))) return false;
    }
    if (serviceCols.contains(QStringLiteral("paid"))) {
        if (!normalize(QStringLiteral("UPDATE work_order_services SET paid=1 WHERE paid IS NULL"))) return false;
    }

    const QSet<QString> partCols = tableColumnsForDb(database, QStringLiteral("work_order_parts"));
    if (partCols.contains(QStringLiteral("order_uid"))) {
        if (!normalize(QStringLiteral(
            "UPDATE work_order_parts "
            "SET order_uid=(SELECT COALESCE(wo.order_uid,'') FROM work_orders wo WHERE wo.number=work_order_parts.order_number LIMIT 1) "
            "WHERE TRIM(COALESCE(order_uid,''))=''"))) return false;
    }
    if (partCols.contains(QStringLiteral("paid"))) {
        if (!normalize(QStringLiteral("UPDATE work_order_parts SET paid=COALESCE(paid,0)"))) return false;
    }
    if (partCols.contains(QStringLiteral("ordered_by_admin"))) {
        if (!normalize(QStringLiteral("UPDATE work_order_parts SET ordered_by_admin=COALESCE(ordered_by_admin,1)"))) return false;
    }

    const QSet<QString> outsourceRepairCols = tableColumnsForDb(database, QStringLiteral("outsource_repairs"));
    if (outsourceRepairCols.contains(QStringLiteral("order_uid"))) {
        if (!normalize(QStringLiteral(
            "UPDATE outsource_repairs "
            "SET order_uid=(SELECT COALESCE(wo.order_uid,'') FROM work_orders wo WHERE wo.number=outsource_repairs.order_number LIMIT 1) "
            "WHERE TRIM(COALESCE(order_uid,''))=''"))) return false;
    }

    const QSet<QString> outsourceJobCols = tableColumnsForDb(database, QStringLiteral("outsource_jobs"));
    if (outsourceJobCols.contains(QStringLiteral("order_uid"))) {
        if (!normalize(QStringLiteral(
            "UPDATE outsource_jobs "
            "SET order_uid=(SELECT COALESCE(wo.order_uid,'') FROM work_orders wo WHERE wo.number=outsource_jobs.order_number LIMIT 1) "
            "WHERE TRIM(COALESCE(order_uid,''))=''"))) return false;
    }

    if (!transaction.exec(QStringLiteral("RELEASE canonical_rewrite_support"))) {
        return fail(QStringLiteral("Commit canonical support preparation failed: %1").arg(transaction.lastError().text()));
    }
    rollback.dismiss();
    return true;
}

bool upsertActiveOrdersIntoRegistryForWorkspace(QSqlDatabase workspaceDb, QString* errorText)
{
    if (!workspaceDb.isValid() || (!workspaceDb.isOpen() && !workspaceDb.open())) {
        if (errorText) *errorText = QStringLiteral("Registry workspace unavailable: %1").arg(workspaceDb.lastError().text());
        return false;
    }
    if (!ensureRegistrySchemaFile()) {
        if (errorText) *errorText = QStringLiteral("Registry schema preparation failed");
        return false;
    }
    return upsertActiveOrdersIntoRegistry(workspaceDb, QSqlDatabase::database(dbRegistryConnectionName()), errorText);
}

bool upsertActiveOrdersIntoRegistry(QSqlDatabase workspaceDb, QSqlDatabase registryDb, QString* errorText)
{
    auto fail = [&](const QString& message) {
        if (errorText) *errorText = message;
        return false;
    };
    if (!workspaceDb.isValid() || !workspaceDb.isOpen())
        return fail(QStringLiteral("Registry workspace is not open"));
    if (!registryDb.isValid() || !registryDb.isOpen())
        return fail(QStringLiteral("Registry database is not open"));
    QSqlQuery transaction(registryDb);
    if (!transaction.exec(QStringLiteral("SAVEPOINT canonical_registry_sync")))
        return fail(QStringLiteral("Registry savepoint failed: %1").arg(transaction.lastError().text()));
    auto rollback = qScopeGuard([&] {
        transaction.exec(QStringLiteral("ROLLBACK TO canonical_registry_sync"));
        transaction.exec(QStringLiteral("RELEASE canonical_registry_sync"));
    });

    QSqlQuery read(workspaceDb);
    if (!read.exec(QStringLiteral("SELECT number, COALESCE(order_uid,''), COALESCE(order_number,number), COALESCE(NULLIF(order_year,0), CAST(strftime('%Y', (CASE WHEN substr(trim(COALESCE(opened_at,'')),3,1)='.' AND substr(trim(COALESCE(opened_at,'')),6,1)='.' THEN CASE WHEN substr(trim(COALESCE(opened_at,'')),7,4) GLOB '[0-9][0-9][0-9][0-9]' THEN substr(trim(COALESCE(opened_at,'')),7,4) ELSE '20'||substr(trim(COALESCE(opened_at,'')),7,2) END||'-'||substr(trim(COALESCE(opened_at,'')),4,2)||'-'||substr(trim(COALESCE(opened_at,'')),1,2) ELSE COALESCE(opened_at,'') END)) AS INTEGER), CAST(strftime('%Y','now') AS INTEGER)), COALESCE(status,''), COALESCE(opened_at,''), COALESCE(closed_at,''), COALESCE(plate,''), COALESCE(plate_norm,''), COALESCE(vin,''), COALESCE(car,''), COALESCE(client,''), COALESCE(master_id,''), COALESCE(master_name_closed,''), COALESCE(works_sum,0), COALESCE(parts_sum,0) FROM work_orders"))) {
        return fail(QStringLiteral("Registry scan failed: %1").arg(read.lastError().text()));
    }

    QSqlQuery up(registryDb);
    if (!up.prepare(QStringLiteral(
        "INSERT INTO order_registry(order_number, order_uid, visible_order_number, order_year, storage_kind, storage_path, source_db, imported_at, last_seen_at, status, opened_at, closed_at, plate, plate_norm, vin, car, client, master_id, master_name_closed, works_sum, parts_sum) "
        "VALUES(?, ?, ?, ?, ?, ?, ?, COALESCE((SELECT imported_at FROM order_registry WHERE order_number=?), ''), datetime('now'), ?,?,?,?,?,?,?,?,?,?,?,?) "
        "ON CONFLICT(order_number) DO UPDATE SET "
        "order_uid=CASE WHEN TRIM(COALESCE(excluded.order_uid,''))<>'' THEN excluded.order_uid ELSE order_registry.order_uid END, "
        "visible_order_number=CASE WHEN COALESCE(excluded.visible_order_number,0)>0 THEN excluded.visible_order_number ELSE order_registry.visible_order_number END, "
        "order_year=CASE WHEN COALESCE(excluded.order_year,0)>0 THEN excluded.order_year ELSE order_registry.order_year END, "
        "storage_kind=excluded.storage_kind, storage_path=excluded.storage_path, source_db=excluded.source_db, "
        "last_seen_at=excluded.last_seen_at, status=excluded.status, opened_at=excluded.opened_at, closed_at=excluded.closed_at, "
        "plate=excluded.plate, plate_norm=excluded.plate_norm, vin=excluded.vin, car=excluded.car, client=excluded.client, "
        "master_id=excluded.master_id, master_name_closed=excluded.master_name_closed, works_sum=excluded.works_sum, parts_sum=excluded.parts_sum")))
        return fail(QStringLiteral("Registry prepare failed: %1").arg(up.lastError().text()));

    const QString activePath = AppRuntime::activeDbFilePath();
    const QString legacyArchivePath = QFileInfo(AppRuntime::archiveDbFilePath()).exists() ? AppRuntime::archiveDbFilePath() : QStringLiteral("");
    while (read.next()) {
        const int orderNumber = read.value(0).toInt();
        QString origin;
        if (!routerCacheHasOrderEntry(workspaceDb, orderNumber) && !workspaceOrderStateLookup(workspaceDb, orderNumber, &origin)) {
            if (!upsertWorkspaceOrderStateEntry(
                workspaceDb,
                orderNumber,
                QStringLiteral("native"),
                QStringLiteral(""),
                QString(),
                [&](int fallbackOrderNumber) {
                    const QString activeUid = OrderRoutingLookupService::activeOrderUidForInternalNumber(workspaceDb, fallbackOrderNumber).trimmed();
                    return !activeUid.isEmpty() ? activeUid : OrderRoutingLookupService::routerCacheOrderUidForInternalNumber(workspaceDb, fallbackOrderNumber).trimmed();
                }))
                return fail(QStringLiteral("Registry workspace state write failed for order %1").arg(orderNumber));
            origin = QStringLiteral("native");
        }
        if (origin.trimmed().isEmpty()) {
            if (!workspaceOrderStateLookup(workspaceDb, orderNumber, &origin)) origin = QStringLiteral("native");
        }
        const bool hydratedArchive = (origin == QStringLiteral("legacy_copy"));
        const QString sourceDb = hydratedArchive ? nonNullText(workspaceOrderSourceDbLookup(workspaceDb, orderNumber)) : QStringLiteral("");
        const QString storageKind = hydratedArchive ? QStringLiteral("archive") : QStringLiteral("active");
        const QString storagePath = hydratedArchive ? (sourceDb.trimmed().isEmpty() ? legacyArchivePath : sourceDb) : activePath;

        up.addBindValue(orderNumber);
        up.addBindValue(nonNullVariantText(read.value(1)));
        up.addBindValue(read.value(2).toInt());
        up.addBindValue(read.value(3).toInt());
        up.addBindValue(storageKind);
        up.addBindValue(nonNullText(storagePath));
        up.addBindValue(nonNullText(sourceDb));
        up.addBindValue(orderNumber);
        up.addBindValue(nonNullVariantText(read.value(4)));
        up.addBindValue(nonNullVariantText(read.value(5)));
        up.addBindValue(nonNullVariantText(read.value(6)));
        up.addBindValue(nonNullVariantText(read.value(7)));
        up.addBindValue(nonNullVariantText(read.value(8)));
        up.addBindValue(nonNullVariantText(read.value(9)));
        up.addBindValue(nonNullVariantText(read.value(10)));
        up.addBindValue(nonNullVariantText(read.value(11)));
        up.addBindValue(nonNullVariantText(read.value(12)));
        up.addBindValue(nonNullVariantText(read.value(13)));
        up.addBindValue(read.value(14).toDouble());
        up.addBindValue(read.value(15).toDouble());
        if (!up.exec()) {
            return fail(QStringLiteral("Registry upsert failed for order %1: %2").arg(orderNumber).arg(up.lastError().text()));
        }
        up.finish();
    }
    if (read.lastError().isValid())
        return fail(QStringLiteral("Registry scan iteration failed: %1").arg(read.lastError().text()));
    if (!transaction.exec(QStringLiteral("RELEASE canonical_registry_sync")))
        return fail(QStringLiteral("Registry release failed: %1").arg(transaction.lastError().text()));
    rollback.dismiss();
    return true;
}
