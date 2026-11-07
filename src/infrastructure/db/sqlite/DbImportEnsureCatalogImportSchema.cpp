// P2 architecture migration: implementation module converted from legacy .ipp implementation-fragment decomposition.
// Compiled as a separate P2 implementation module; include-order dependencies are isolated in the module prelude.

#include "infrastructure/db/sqlite/DbImportSupportModulePrelude.h"

bool Database::ensureCatalogImportSchema(QString* errorText)
{
    if (!ensureConnectionAlive(errorText)) return false;
    if (!ensureSchema()) {
        if (errorText && errorText->trimmed().isEmpty()) *errorText = QString::fromUtf8("Не вдалося підготувати базову схему БД.");
        return false;
    }

    QSqlDatabase d = db();
    if (!d.isValid() || (!d.isOpen() && !d.open())) {
        if (errorText) *errorText = d.lastError().text();
        return false;
    }

    // ensureSchema() may normalize canonical child tables and, during that process,
    // temporarily drop router views that depend on work_order_services / work_order_parts.
    // Rebuild them here before any startup schema audit or runtime preflight reads the
    // active workspace, otherwise the app can exit immediately after a successful build
    // with:
    //   missing view router_work_order_services
    //   missing view router_work_order_parts
    if (!ensureRouterCacheSchema(d) || !rebuildRouterViews(d)) {
        if (errorText && errorText->trimmed().isEmpty()) {
            *errorText = QString::fromUtf8("Не вдалося відновити router views після підготовки catalog schema.");
        }
        return false;
    }

    const QSet<QString> currentCols = tableColumnsForDb(d, QStringLiteral("parts_catalog"));
    if (currentCols.isEmpty()) {
        if (errorText) *errorText = QString::fromUtf8("Не вдалося прочитати структуру таблиці parts_catalog.");
        return false;
    }

    const QStringList requiredCols = {
        QStringLiteral("part_number"), QStringLiteral("oem_number"), QStringLiteral("name"), QStringLiteral("brand"),
        QStringLiteral("price"), QStringLiteral("supplier"), QStringLiteral("currency"), QStringLiteral("unit"),
        QStringLiteral("note"), QStringLiteral("kind"), QStringLiteral("group_key"), QStringLiteral("group_name"),
        QStringLiteral("node_tags"), QStringLiteral("vehicle_brand"), QStringLiteral("vehicle_model"),
        QStringLiteral("year_from"), QStringLiteral("year_to"), QStringLiteral("fuel_type"), QStringLiteral("engine_type"),
        QStringLiteral("body_type"), QStringLiteral("source_type"), QStringLiteral("source_order_uid_first"),
        QStringLiteral("source_order_uid_last"), QStringLiteral("first_imported_at"), QStringLiteral("last_imported_at"),
        QStringLiteral("manually_edited"), QStringLiteral("is_active")
    };

    bool needsMigration = false;
    for (const QString& col : requiredCols) {
        if (!currentCols.contains(col)) { needsMigration = true; break; }
    }
    if (!tableExists(QStringLiteral("catalog_import_runs"))
        || !tableExists(QStringLiteral("catalog_import_items"))
        || !tableExists(QStringLiteral("parts_catalog_vehicle_map"))
        || !tableExists(QStringLiteral("parts_catalog_sources"))) {
        needsMigration = true;
    }

    if (needsMigration && !createCatalogMigrationBackup(errorText)) return false;

    MutationExecutor::Request request;
    request.sourceModule = QStringLiteral("DbImportSupport::ensureCatalogImportSchema");
    request.entityType = QStringLiteral("schema");
    request.entityId = QStringLiteral("parts_catalog");
    request.operation = QStringLiteral("ensure_catalog_import_schema");
    request.changedTables = {QStringLiteral("parts_catalog"), QStringLiteral("catalog_import_runs"), QStringLiteral("catalog_import_items"), QStringLiteral("parts_catalog_vehicle_map"), QStringLiteral("parts_catalog_sources")};
    request.beginErrorFallback = QString::fromUtf8("Не вдалося почати транзакцію підготовки catalog schema.");
    request.commitErrorFallback = QString::fromUtf8("Не вдалося завершити транзакцію підготовки catalog schema.");

    const auto tx = MutationExecutor::execute(d, request, [&](QSqlDatabase& txDb, QString* txError) {
        QSqlDatabase d = txDb;
        auto fail = [&](const QString& text) {
            if (txError) *txError = text;
            qWarning() << "Catalog schema ensure failed:" << text;
            return false;
        };

    if (!ensureColumn(QStringLiteral("parts_catalog"), QStringLiteral("oem_number"), QStringLiteral("oem_number TEXT NOT NULL DEFAULT ''"))) return fail(QString::fromUtf8("Не вдалося додати колонку oem_number."));
    if (!ensureColumn(QStringLiteral("parts_catalog"), QStringLiteral("kind"), QStringLiteral("kind TEXT NOT NULL DEFAULT ''"))) return fail(QString::fromUtf8("Не вдалося додати колонку kind."));
    if (!ensureColumn(QStringLiteral("parts_catalog"), QStringLiteral("group_key"), QStringLiteral("group_key TEXT NOT NULL DEFAULT ''"))) return fail(QString::fromUtf8("Не вдалося додати колонку group_key."));
    if (!ensureColumn(QStringLiteral("parts_catalog"), QStringLiteral("node_tags"), QStringLiteral("node_tags TEXT NOT NULL DEFAULT ''"))) return fail(QString::fromUtf8("Не вдалося додати колонку node_tags."));
    if (!ensureColumn(QStringLiteral("parts_catalog"), QStringLiteral("vehicle_brand"), QStringLiteral("vehicle_brand TEXT NOT NULL DEFAULT ''"))) return fail(QString::fromUtf8("Не вдалося додати колонку vehicle_brand."));
    if (!ensureColumn(QStringLiteral("parts_catalog"), QStringLiteral("vehicle_model"), QStringLiteral("vehicle_model TEXT NOT NULL DEFAULT ''"))) return fail(QString::fromUtf8("Не вдалося додати колонку vehicle_model."));
    if (!ensureColumn(QStringLiteral("parts_catalog"), QStringLiteral("year_from"), QStringLiteral("year_from INTEGER NOT NULL DEFAULT 0"))) return fail(QString::fromUtf8("Не вдалося додати колонку year_from."));
    if (!ensureColumn(QStringLiteral("parts_catalog"), QStringLiteral("year_to"), QStringLiteral("year_to INTEGER NOT NULL DEFAULT 0"))) return fail(QString::fromUtf8("Не вдалося додати колонку year_to."));
    if (!ensureColumn(QStringLiteral("parts_catalog"), QStringLiteral("fuel_type"), QStringLiteral("fuel_type TEXT NOT NULL DEFAULT ''"))) return fail(QString::fromUtf8("Не вдалося додати колонку fuel_type."));
    if (!ensureColumn(QStringLiteral("parts_catalog"), QStringLiteral("engine_type"), QStringLiteral("engine_type TEXT NOT NULL DEFAULT ''"))) return fail(QString::fromUtf8("Не вдалося додати колонку engine_type."));
    if (!ensureColumn(QStringLiteral("parts_catalog"), QStringLiteral("body_type"), QStringLiteral("body_type TEXT NOT NULL DEFAULT ''"))) return fail(QString::fromUtf8("Не вдалося додати колонку body_type."));
    if (!ensureColumn(QStringLiteral("parts_catalog"), QStringLiteral("group_name"), QStringLiteral("group_name TEXT NOT NULL DEFAULT ''"))) return fail(QString::fromUtf8("Не вдалося додати колонку group_name."));
    if (!ensureColumn(QStringLiteral("parts_catalog"), QStringLiteral("supplier"), QStringLiteral("supplier TEXT NOT NULL DEFAULT ''"))) return fail(QString::fromUtf8("Не вдалося додати колонку supplier."));
    if (!ensureColumn(QStringLiteral("parts_catalog"), QStringLiteral("currency"), QStringLiteral("currency TEXT NOT NULL DEFAULT 'UAH'"))) return fail(QString::fromUtf8("Не вдалося додати колонку currency."));
    if (!ensureColumn(QStringLiteral("parts_catalog"), QStringLiteral("unit"), QStringLiteral("unit TEXT NOT NULL DEFAULT 'шт'"))) return fail(QString::fromUtf8("Не вдалося додати колонку unit."));
    if (!ensureColumn(QStringLiteral("parts_catalog"), QStringLiteral("note"), QStringLiteral("note TEXT NOT NULL DEFAULT ''"))) return fail(QString::fromUtf8("Не вдалося додати колонку note."));
    if (!ensureColumn(QStringLiteral("parts_catalog"), QStringLiteral("source_type"), QStringLiteral("source_type TEXT NOT NULL DEFAULT ''"))) return fail(QString::fromUtf8("Не вдалося додати колонку source_type."));
    if (!ensureColumn(QStringLiteral("parts_catalog"), QStringLiteral("source_order_uid_first"), QStringLiteral("source_order_uid_first TEXT NOT NULL DEFAULT ''"))) return fail(QString::fromUtf8("Не вдалося додати колонку source_order_uid_first."));
    if (!ensureColumn(QStringLiteral("parts_catalog"), QStringLiteral("source_order_uid_last"), QStringLiteral("source_order_uid_last TEXT NOT NULL DEFAULT ''"))) return fail(QString::fromUtf8("Не вдалося додати колонку source_order_uid_last."));
    if (!ensureColumn(QStringLiteral("parts_catalog"), QStringLiteral("first_imported_at"), QStringLiteral("first_imported_at TEXT NOT NULL DEFAULT ''"))) return fail(QString::fromUtf8("Не вдалося додати колонку first_imported_at."));
    if (!ensureColumn(QStringLiteral("parts_catalog"), QStringLiteral("last_imported_at"), QStringLiteral("last_imported_at TEXT NOT NULL DEFAULT ''"))) return fail(QString::fromUtf8("Не вдалося додати колонку last_imported_at."));
    if (!ensureColumn(QStringLiteral("parts_catalog"), QStringLiteral("normalized_name"), QStringLiteral("normalized_name TEXT NOT NULL DEFAULT ''"))) return fail(QString::fromUtf8("Не вдалося додати колонку normalized_name."));
    if (!ensureColumn(QStringLiteral("parts_catalog"), QStringLiteral("normalized_part_number"), QStringLiteral("normalized_part_number TEXT NOT NULL DEFAULT ''"))) return fail(QString::fromUtf8("Не вдалося додати колонку normalized_part_number."));
    if (!ensureColumn(QStringLiteral("parts_catalog"), QStringLiteral("normalized_oem_number"), QStringLiteral("normalized_oem_number TEXT NOT NULL DEFAULT ''"))) return fail(QString::fromUtf8("Не вдалося додати колонку normalized_oem_number."));
    if (!ensureColumn(QStringLiteral("parts_catalog"), QStringLiteral("catalog_match_key"), QStringLiteral("catalog_match_key TEXT NOT NULL DEFAULT ''"))) return fail(QString::fromUtf8("Не вдалося додати колонку catalog_match_key."));
    if (!ensureColumn(QStringLiteral("parts_catalog"), QStringLiteral("dedupe_signature"), QStringLiteral("dedupe_signature TEXT NOT NULL DEFAULT ''"))) return fail(QString::fromUtf8("Не вдалося додати колонку dedupe_signature."));
    if (!ensureColumn(QStringLiteral("parts_catalog"), QStringLiteral("usage_count"), QStringLiteral("usage_count INTEGER NOT NULL DEFAULT 0"))) return fail(QString::fromUtf8("Не вдалося додати колонку usage_count."));
    if (!ensureColumn(QStringLiteral("parts_catalog"), QStringLiteral("orders_count"), QStringLiteral("orders_count INTEGER NOT NULL DEFAULT 0"))) return fail(QString::fromUtf8("Не вдалося додати колонку orders_count."));
    if (!ensureColumn(QStringLiteral("parts_catalog"), QStringLiteral("first_seen_at"), QStringLiteral("first_seen_at TEXT NOT NULL DEFAULT ''"))) return fail(QString::fromUtf8("Не вдалося додати колонку first_seen_at."));
    if (!ensureColumn(QStringLiteral("parts_catalog"), QStringLiteral("last_seen_at"), QStringLiteral("last_seen_at TEXT NOT NULL DEFAULT ''"))) return fail(QString::fromUtf8("Не вдалося додати колонку last_seen_at."));
    if (!ensureColumn(QStringLiteral("parts_catalog"), QStringLiteral("min_price"), QStringLiteral("min_price REAL NOT NULL DEFAULT 0"))) return fail(QString::fromUtf8("Не вдалося додати колонку min_price."));
    if (!ensureColumn(QStringLiteral("parts_catalog"), QStringLiteral("max_price"), QStringLiteral("max_price REAL NOT NULL DEFAULT 0"))) return fail(QString::fromUtf8("Не вдалося додати колонку max_price."));
    if (!ensureColumn(QStringLiteral("parts_catalog"), QStringLiteral("avg_price"), QStringLiteral("avg_price REAL NOT NULL DEFAULT 0"))) return fail(QString::fromUtf8("Не вдалося додати колонку avg_price."));
    if (!ensureColumn(QStringLiteral("parts_catalog"), QStringLiteral("last_price"), QStringLiteral("last_price REAL NOT NULL DEFAULT 0"))) return fail(QString::fromUtf8("Не вдалося додати колонку last_price."));
    if (!ensureColumn(QStringLiteral("parts_catalog"), QStringLiteral("last_store"), QStringLiteral("last_store TEXT NOT NULL DEFAULT ''"))) return fail(QString::fromUtf8("Не вдалося додати колонку last_store."));
    if (!ensureColumn(QStringLiteral("parts_catalog"), QStringLiteral("stores_seen"), QStringLiteral("stores_seen TEXT NOT NULL DEFAULT ''"))) return fail(QString::fromUtf8("Не вдалося додати колонку stores_seen."));
    if (!ensureColumn(QStringLiteral("parts_catalog"), QStringLiteral("last_order_number"), QStringLiteral("last_order_number INTEGER NOT NULL DEFAULT 0"))) return fail(QString::fromUtf8("Не вдалося додати колонку last_order_number."));
    if (!ensureColumn(QStringLiteral("parts_catalog"), QStringLiteral("last_order_uid"), QStringLiteral("last_order_uid TEXT NOT NULL DEFAULT ''"))) return fail(QString::fromUtf8("Не вдалося додати колонку last_order_uid."));
    if (!ensureColumn(QStringLiteral("parts_catalog"), QStringLiteral("qty_last"), QStringLiteral("qty_last REAL NOT NULL DEFAULT 0"))) return fail(QString::fromUtf8("Не вдалося додати колонку qty_last."));
    if (!ensureColumn(QStringLiteral("parts_catalog"), QStringLiteral("amount_last"), QStringLiteral("amount_last REAL NOT NULL DEFAULT 0"))) return fail(QString::fromUtf8("Не вдалося додати колонку amount_last."));
    if (!ensureColumn(QStringLiteral("parts_catalog"), QStringLiteral("ordered_at"), QStringLiteral("ordered_at TEXT NOT NULL DEFAULT ''"))) return fail(QString::fromUtf8("Не вдалося додати колонку ordered_at."));
    if (!ensureColumn(QStringLiteral("parts_catalog"), QStringLiteral("ordered_by_admin"), QStringLiteral("ordered_by_admin INTEGER NOT NULL DEFAULT 0"))) return fail(QString::fromUtf8("Не вдалося додати колонку ordered_by_admin."));
    if (!ensureColumn(QStringLiteral("parts_catalog"), QStringLiteral("paid"), QStringLiteral("paid INTEGER NOT NULL DEFAULT 0"))) return fail(QString::fromUtf8("Не вдалося додати колонку paid."));
    if (!ensureColumn(QStringLiteral("parts_catalog"), QStringLiteral("service_id"), QStringLiteral("service_id INTEGER NOT NULL DEFAULT 0"))) return fail(QString::fromUtf8("Не вдалося додати колонку service_id."));
    if (!ensureColumn(QStringLiteral("parts_catalog"), QStringLiteral("source_part_id"), QStringLiteral("source_part_id INTEGER NOT NULL DEFAULT 0"))) return fail(QString::fromUtf8("Не вдалося додати колонку source_part_id."));
    if (!ensureColumn(QStringLiteral("parts_catalog"), QStringLiteral("source_order_number"), QStringLiteral("source_order_number INTEGER NOT NULL DEFAULT 0"))) return fail(QString::fromUtf8("Не вдалося додати колонку source_order_number."));
    if (!ensureColumn(QStringLiteral("parts_catalog"), QStringLiteral("source_db_layer"), QStringLiteral("source_db_layer TEXT NOT NULL DEFAULT ''"))) return fail(QString::fromUtf8("Не вдалося додати колонку source_db_layer."));
    if (!ensureColumn(QStringLiteral("parts_catalog"), QStringLiteral("is_from_legacy_import"), QStringLiteral("is_from_legacy_import INTEGER NOT NULL DEFAULT 0"))) return fail(QString::fromUtf8("Не вдалося додати колонку is_from_legacy_import."));
    if (!ensureColumn(QStringLiteral("parts_catalog"), QStringLiteral("import_batch_id"), QStringLiteral("import_batch_id TEXT NOT NULL DEFAULT ''"))) return fail(QString::fromUtf8("Не вдалося додати колонку import_batch_id."));

    QSqlQuery q(d);
    if (!q.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS catalog_import_runs (id INTEGER PRIMARY KEY AUTOINCREMENT, started_at TEXT NOT NULL DEFAULT (datetime('now')), finished_at TEXT NOT NULL DEFAULT '', processed_count INTEGER NOT NULL DEFAULT 0, eligible_count INTEGER NOT NULL DEFAULT 0, added_count INTEGER NOT NULL DEFAULT 0, updated_count INTEGER NOT NULL DEFAULT 0, skipped_count INTEGER NOT NULL DEFAULT 0, skipped_oil_count INTEGER NOT NULL DEFAULT 0, error_count INTEGER NOT NULL DEFAULT 0, summary_text TEXT NOT NULL DEFAULT '')"))) return fail(q.lastError().text());
    if (!q.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS catalog_import_items (id INTEGER PRIMARY KEY AUTOINCREMENT, run_id INTEGER NOT NULL DEFAULT 0, source_part_id INTEGER NOT NULL DEFAULT 0, order_uid TEXT NOT NULL DEFAULT '', order_number INTEGER NOT NULL DEFAULT 0, part_number TEXT NOT NULL DEFAULT '', name TEXT NOT NULL DEFAULT '', action TEXT NOT NULL DEFAULT '', catalog_id INTEGER NOT NULL DEFAULT 0, message TEXT NOT NULL DEFAULT '', is_oil INTEGER NOT NULL DEFAULT 0)"))) return fail(q.lastError().text());
    if (!q.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS parts_catalog_vehicle_map (id INTEGER PRIMARY KEY AUTOINCREMENT, catalog_id INTEGER NOT NULL, vehicle_brand TEXT NOT NULL DEFAULT '', vehicle_model TEXT NOT NULL DEFAULT '', year_from INTEGER NOT NULL DEFAULT 0, year_to INTEGER NOT NULL DEFAULT 0, fuel_type TEXT NOT NULL DEFAULT '', engine_type TEXT NOT NULL DEFAULT '', body_type TEXT NOT NULL DEFAULT '', source_order_uid TEXT NOT NULL DEFAULT '', last_seen_at TEXT NOT NULL DEFAULT (datetime('now')))"))) return fail(q.lastError().text());
    if (!q.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS parts_catalog_sources (id INTEGER PRIMARY KEY AUTOINCREMENT, catalog_id INTEGER NOT NULL, source_part_id INTEGER NOT NULL DEFAULT 0, order_uid TEXT NOT NULL DEFAULT '', order_number INTEGER NOT NULL DEFAULT 0, supplier TEXT NOT NULL DEFAULT '', note TEXT NOT NULL DEFAULT '', imported_at TEXT NOT NULL DEFAULT (datetime('now')))"))) return fail(q.lastError().text());
    if (!q.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS catalog_source_map (id INTEGER PRIMARY KEY AUTOINCREMENT, source_part_id INTEGER NOT NULL DEFAULT 0, source_order_uid TEXT NOT NULL DEFAULT '', source_order_number INTEGER NOT NULL DEFAULT 0, source_db_layer TEXT NOT NULL DEFAULT '', catalog_item_id INTEGER NOT NULL DEFAULT 0, imported_at TEXT NOT NULL DEFAULT (datetime('now')), import_batch_id TEXT NOT NULL DEFAULT '', dedupe_mode TEXT NOT NULL DEFAULT '', was_created_new INTEGER NOT NULL DEFAULT 0, was_merged_into_existing INTEGER NOT NULL DEFAULT 0, source_signature TEXT NOT NULL DEFAULT '', source_part_number TEXT NOT NULL DEFAULT '', source_oem_number TEXT NOT NULL DEFAULT '', source_name TEXT NOT NULL DEFAULT '', source_store TEXT NOT NULL DEFAULT '', source_price REAL NOT NULL DEFAULT 0, source_qty REAL NOT NULL DEFAULT 0, source_amount REAL NOT NULL DEFAULT 0)"))) return fail(q.lastError().text());
    if (!q.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS catalog_import_conflicts (id INTEGER PRIMARY KEY AUTOINCREMENT, run_id INTEGER NOT NULL DEFAULT 0, import_batch_id TEXT NOT NULL DEFAULT '', source_part_id INTEGER NOT NULL DEFAULT 0, order_uid TEXT NOT NULL DEFAULT '', order_number INTEGER NOT NULL DEFAULT 0, source_db_layer TEXT NOT NULL DEFAULT '', part_number TEXT NOT NULL DEFAULT '', oem_number TEXT NOT NULL DEFAULT '', name TEXT NOT NULL DEFAULT '', conflict_reason TEXT NOT NULL DEFAULT '', candidate_catalog_ids TEXT NOT NULL DEFAULT '', recommended_action TEXT NOT NULL DEFAULT '', created_at TEXT NOT NULL DEFAULT (datetime('now')))"))) return fail(q.lastError().text());

    ensureColumn(QStringLiteral("catalog_import_runs"), QStringLiteral("unique_source_count"), QStringLiteral("unique_source_count INTEGER NOT NULL DEFAULT 0"));
    ensureColumn(QStringLiteral("catalog_import_runs"), QStringLiteral("merged_count"), QStringLiteral("merged_count INTEGER NOT NULL DEFAULT 0"));
    ensureColumn(QStringLiteral("catalog_import_runs"), QStringLiteral("conflict_count"), QStringLiteral("conflict_count INTEGER NOT NULL DEFAULT 0"));
    ensureColumn(QStringLiteral("catalog_import_runs"), QStringLiteral("legacy_imported_count"), QStringLiteral("legacy_imported_count INTEGER NOT NULL DEFAULT 0"));
    ensureColumn(QStringLiteral("catalog_import_runs"), QStringLiteral("archive_imported_count"), QStringLiteral("archive_imported_count INTEGER NOT NULL DEFAULT 0"));
    ensureColumn(QStringLiteral("catalog_import_runs"), QStringLiteral("active_imported_count"), QStringLiteral("active_imported_count INTEGER NOT NULL DEFAULT 0"));
    ensureColumn(QStringLiteral("catalog_import_runs"), QStringLiteral("import_batch_id"), QStringLiteral("import_batch_id TEXT NOT NULL DEFAULT ''"));
    ensureColumn(QStringLiteral("catalog_import_runs"), QStringLiteral("reports_dir"), QStringLiteral("reports_dir TEXT NOT NULL DEFAULT ''"));
    ensureColumn(QStringLiteral("catalog_import_items"), QStringLiteral("source_db_layer"), QStringLiteral("source_db_layer TEXT NOT NULL DEFAULT ''"));
    ensureColumn(QStringLiteral("catalog_import_items"), QStringLiteral("import_batch_id"), QStringLiteral("import_batch_id TEXT NOT NULL DEFAULT ''"));
    ensureColumn(QStringLiteral("catalog_import_items"), QStringLiteral("dedupe_mode"), QStringLiteral("dedupe_mode TEXT NOT NULL DEFAULT ''"));
    ensureColumn(QStringLiteral("catalog_import_items"), QStringLiteral("source_signature"), QStringLiteral("source_signature TEXT NOT NULL DEFAULT ''"));
    ensureColumn(QStringLiteral("parts_catalog_sources"), QStringLiteral("source_db_layer"), QStringLiteral("source_db_layer TEXT NOT NULL DEFAULT ''"));
    ensureColumn(QStringLiteral("parts_catalog_sources"), QStringLiteral("source_signature"), QStringLiteral("source_signature TEXT NOT NULL DEFAULT ''"));
    ensureColumn(QStringLiteral("parts_catalog_sources"), QStringLiteral("import_batch_id"), QStringLiteral("import_batch_id TEXT NOT NULL DEFAULT ''"));
    ensureColumn(QStringLiteral("parts_catalog_sources"), QStringLiteral("part_number"), QStringLiteral("part_number TEXT NOT NULL DEFAULT ''"));
    ensureColumn(QStringLiteral("parts_catalog_sources"), QStringLiteral("oem_number"), QStringLiteral("oem_number TEXT NOT NULL DEFAULT ''"));
    ensureColumn(QStringLiteral("parts_catalog_sources"), QStringLiteral("name"), QStringLiteral("name TEXT NOT NULL DEFAULT ''"));
    ensureColumn(QStringLiteral("parts_catalog_sources"), QStringLiteral("manufacturer"), QStringLiteral("manufacturer TEXT NOT NULL DEFAULT ''"));
    ensureColumn(QStringLiteral("parts_catalog_sources"), QStringLiteral("vehicle_brand"), QStringLiteral("vehicle_brand TEXT NOT NULL DEFAULT ''"));
    ensureColumn(QStringLiteral("parts_catalog_sources"), QStringLiteral("vehicle_model"), QStringLiteral("vehicle_model TEXT NOT NULL DEFAULT ''"));
    ensureColumn(QStringLiteral("parts_catalog_sources"), QStringLiteral("year_from"), QStringLiteral("year_from INTEGER NOT NULL DEFAULT 0"));
    ensureColumn(QStringLiteral("parts_catalog_sources"), QStringLiteral("year_to"), QStringLiteral("year_to INTEGER NOT NULL DEFAULT 0"));
    ensureColumn(QStringLiteral("parts_catalog_sources"), QStringLiteral("fuel_type"), QStringLiteral("fuel_type TEXT NOT NULL DEFAULT ''"));
    ensureColumn(QStringLiteral("parts_catalog_sources"), QStringLiteral("engine_type"), QStringLiteral("engine_type TEXT NOT NULL DEFAULT ''"));
    ensureColumn(QStringLiteral("parts_catalog_sources"), QStringLiteral("body_type"), QStringLiteral("body_type TEXT NOT NULL DEFAULT ''"));
    ensureColumn(QStringLiteral("parts_catalog_sources"), QStringLiteral("group_name"), QStringLiteral("group_name TEXT NOT NULL DEFAULT ''"));
    ensureColumn(QStringLiteral("parts_catalog_sources"), QStringLiteral("qty"), QStringLiteral("qty REAL NOT NULL DEFAULT 0"));
    ensureColumn(QStringLiteral("parts_catalog_sources"), QStringLiteral("price"), QStringLiteral("price REAL NOT NULL DEFAULT 0"));
    ensureColumn(QStringLiteral("parts_catalog_sources"), QStringLiteral("amount"), QStringLiteral("amount REAL NOT NULL DEFAULT 0"));
    ensureColumn(QStringLiteral("parts_catalog_sources"), QStringLiteral("purchase_price"), QStringLiteral("purchase_price REAL NOT NULL DEFAULT 0"));
    ensureColumn(QStringLiteral("parts_catalog_sources"), QStringLiteral("purchase_amount"), QStringLiteral("purchase_amount REAL NOT NULL DEFAULT 0"));
    ensureColumn(QStringLiteral("parts_catalog_sources"), QStringLiteral("markup_percent"), QStringLiteral("markup_percent REAL NOT NULL DEFAULT 0"));
    ensureColumn(QStringLiteral("parts_catalog_sources"), QStringLiteral("paid"), QStringLiteral("paid INTEGER NOT NULL DEFAULT 0"));
    ensureColumn(QStringLiteral("parts_catalog_sources"), QStringLiteral("ordered_by_admin"), QStringLiteral("ordered_by_admin INTEGER NOT NULL DEFAULT 0"));
    ensureColumn(QStringLiteral("parts_catalog_sources"), QStringLiteral("service_id"), QStringLiteral("service_id INTEGER NOT NULL DEFAULT 0"));
    ensureColumn(QStringLiteral("parts_catalog_sources"), QStringLiteral("ordered_at"), QStringLiteral("ordered_at TEXT NOT NULL DEFAULT ''"));
    ensureColumn(QStringLiteral("parts_catalog_sources"), QStringLiteral("delivery_date"), QStringLiteral("delivery_date TEXT NOT NULL DEFAULT ''"));
    ensureColumn(QStringLiteral("parts_catalog_sources"), QStringLiteral("seen_at"), QStringLiteral("seen_at TEXT NOT NULL DEFAULT ''"));

    q.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_parts_catalog_oem_number ON parts_catalog(oem_number)"));
    q.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_parts_catalog_part_number ON parts_catalog(part_number)"));
    q.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_parts_catalog_name ON parts_catalog(name)"));
    q.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_parts_catalog_brand ON parts_catalog(brand)"));
    q.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_parts_catalog_source_last ON parts_catalog(source_order_uid_last)"));
    q.exec(QStringLiteral("CREATE UNIQUE INDEX IF NOT EXISTS idx_parts_catalog_vehicle_map_unique ON parts_catalog_vehicle_map(catalog_id, vehicle_brand, vehicle_model, year_from, year_to, fuel_type, engine_type, body_type)"));
    q.exec(QStringLiteral("CREATE UNIQUE INDEX IF NOT EXISTS idx_parts_catalog_sources_unique ON parts_catalog_sources(catalog_id, source_part_id)"));
    q.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_parts_catalog_norm_name ON parts_catalog(normalized_name)"));
    q.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_parts_catalog_norm_part ON parts_catalog(normalized_part_number)"));
    q.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_parts_catalog_norm_oem ON parts_catalog(normalized_oem_number)"));
    q.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_parts_catalog_match_key ON parts_catalog(catalog_match_key)"));
    q.exec(QStringLiteral("CREATE UNIQUE INDEX IF NOT EXISTS idx_catalog_source_map_signature ON catalog_source_map(source_signature)"));
    q.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_catalog_source_map_catalog ON catalog_source_map(catalog_item_id)"));
    q.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_catalog_import_conflicts_run ON catalog_import_conflicts(run_id)"));

    const auto execCatalogUpdate = [&](const QString& sql, const QString& label) {
        if (!q.exec(sql)) return fail(QStringLiteral("%1: %2").arg(label, q.lastError().text()));
        return true;
    };

    if (!execCatalogUpdate(QStringLiteral("UPDATE parts_catalog SET part_number=TRIM(COALESCE(NULLIF(part_number,''), NULLIF(oem_number,''), ''))"), QStringLiteral("normalize parts_catalog.part_number"))) return false;
    if (!execCatalogUpdate(QStringLiteral("UPDATE parts_catalog SET oem_number=TRIM(COALESCE(NULLIF(oem_number,''), NULLIF(part_number,''), ''))"), QStringLiteral("normalize parts_catalog.oem_number"))) return false;
    if (!execCatalogUpdate(QStringLiteral("UPDATE parts_catalog SET group_name=TRIM(COALESCE(group_name,'')) WHERE group_name IS NULL OR TRIM(COALESCE(group_name,''))<>COALESCE(group_name,'')"), QStringLiteral("normalize parts_catalog.group_name"))) return false;
    if (!execCatalogUpdate(QStringLiteral("UPDATE parts_catalog SET supplier=TRIM(COALESCE(supplier,'')) WHERE supplier IS NULL OR TRIM(COALESCE(supplier,''))<>COALESCE(supplier,'')"), QStringLiteral("normalize parts_catalog.supplier"))) return false;
    if (!execCatalogUpdate(QStringLiteral("UPDATE parts_catalog SET brand='' WHERE COALESCE(manually_edited,0)=0 AND TRIM(COALESCE(brand,''))<>'' AND TRIM(COALESCE(supplier,''))<>'' AND LOWER(TRIM(COALESCE(brand,'')))=LOWER(TRIM(COALESCE(supplier,'')))"), QStringLiteral("normalize parts_catalog.brand_vs_supplier"))) return false;
    if (!execCatalogUpdate(QStringLiteral("UPDATE parts_catalog SET note=CASE WHEN TRIM(COALESCE(note,''))='' AND TRIM(COALESCE(node_tags,''))<>'' THEN TRIM(node_tags) ELSE TRIM(COALESCE(note,'')) END"), QStringLiteral("normalize parts_catalog.note"))) return false;
    if (!execCatalogUpdate(QStringLiteral("UPDATE parts_catalog SET currency='UAH' WHERE TRIM(COALESCE(currency,''))=''"), QStringLiteral("normalize parts_catalog.currency"))) return false;
    if (!execCatalogUpdate(QStringLiteral("UPDATE parts_catalog SET unit='шт' WHERE TRIM(COALESCE(unit,''))=''"), QStringLiteral("normalize parts_catalog.unit"))) return false;
    if (!execCatalogUpdate(QStringLiteral("UPDATE parts_catalog SET is_active=1 WHERE COALESCE(is_active,0)=0"), QStringLiteral("normalize parts_catalog.is_active"))) return false;

    if (!execCatalogUpdate(QStringLiteral("UPDATE parts_catalog SET normalized_name=LOWER(TRIM(COALESCE(name,''))) WHERE TRIM(COALESCE(normalized_name,''))=''"), QStringLiteral("normalize parts_catalog.normalized_name"))) return false;
    if (!execCatalogUpdate(QStringLiteral("UPDATE parts_catalog SET normalized_part_number=UPPER(REPLACE(REPLACE(REPLACE(REPLACE(TRIM(COALESCE(part_number,'')),' ',''),'-',''),'/',''),'.','')) WHERE TRIM(COALESCE(normalized_part_number,''))=''"), QStringLiteral("normalize parts_catalog.normalized_part_number"))) return false;
    if (!execCatalogUpdate(QStringLiteral("UPDATE parts_catalog SET normalized_oem_number=UPPER(REPLACE(REPLACE(REPLACE(REPLACE(TRIM(COALESCE(oem_number,'')),' ',''),'-',''),'/',''),'.','')) WHERE TRIM(COALESCE(normalized_oem_number,''))=''"), QStringLiteral("normalize parts_catalog.normalized_oem_number"))) return false;
    if (!execCatalogUpdate(QStringLiteral("UPDATE parts_catalog SET source_order_number=COALESCE(source_order_number,0) WHERE source_order_number IS NULL"), QStringLiteral("normalize parts_catalog.source_order_number"))) return false;
    if (!execCatalogUpdate(QStringLiteral("UPDATE parts_catalog SET usage_count=CASE WHEN COALESCE(usage_count,0)<0 THEN 0 ELSE COALESCE(usage_count,0) END"), QStringLiteral("normalize parts_catalog.usage_count"))) return false;
    if (!execCatalogUpdate(QStringLiteral("UPDATE parts_catalog SET orders_count=CASE WHEN COALESCE(orders_count,0)<0 THEN 0 ELSE COALESCE(orders_count,0) END"), QStringLiteral("normalize parts_catalog.orders_count"))) return false;

    // Keep the group normalization fully SQL-based. Updating the same table via
    // a second query while iterating a live SELECT cursor is fragile on SQLite/
    // Qt drivers and was one of the reproducible sources of:
    //   NOT NULL constraint failed: parts_catalog.group_name
    //   Unable to fetch row
    // during post-import startup.
    if (!execCatalogUpdate(
            QStringLiteral(
                "UPDATE parts_catalog "
                "SET note=CASE "
                "  WHEN TRIM(COALESCE(note,''))='' AND TRIM(COALESCE(node_tags,''))<>'' THEN TRIM(COALESCE(node_tags,'')) "
                "  WHEN TRIM(COALESCE(node_tags,''))<>'' AND INSTR(LOWER(COALESCE(note,'')), LOWER(TRIM(COALESCE(node_tags,''))))=0 THEN TRIM(COALESCE(note,'')) || char(10) || TRIM(COALESCE(node_tags,'')) "
                "  ELSE TRIM(COALESCE(note,'')) END"),
            QStringLiteral("normalize parts_catalog.note_merge"))) return false;

    const QString partsGroupSql = QStringLiteral(
        "UPDATE parts_catalog SET group_name=CASE "
        "WHEN TRIM(COALESCE(group_name,''))<>'' THEN TRIM(COALESCE(group_name,'')) "
        "WHEN TRIM(COALESCE(kind,''))<>'' AND LOWER(TRIM(COALESCE(kind,''))) NOT IN ('oem','oe','aftermarket','analog','analogue','replacement') THEN TRIM(COALESCE(kind,'')) "
        "WHEN LOWER(TRIM(COALESCE(name,'')) || ' ' || TRIM(COALESCE(note,'')) || ' ' || TRIM(COALESCE(node_tags,''))) LIKE '%фільтр%' OR LOWER(TRIM(COALESCE(name,'')) || ' ' || TRIM(COALESCE(note,'')) || ' ' || TRIM(COALESCE(node_tags,''))) LIKE '%фильтр%' OR LOWER(TRIM(COALESCE(name,'')) || ' ' || TRIM(COALESCE(note,'')) || ' ' || TRIM(COALESCE(node_tags,''))) LIKE '%filter%' THEN 'Фільтри' "
        "WHEN LOWER(TRIM(COALESCE(name,'')) || ' ' || TRIM(COALESCE(note,'')) || ' ' || TRIM(COALESCE(node_tags,''))) LIKE '%гальм%' OR LOWER(TRIM(COALESCE(name,'')) || ' ' || TRIM(COALESCE(note,'')) || ' ' || TRIM(COALESCE(node_tags,''))) LIKE '%brake%' OR LOWER(TRIM(COALESCE(name,'')) || ' ' || TRIM(COALESCE(note,'')) || ' ' || TRIM(COALESCE(node_tags,''))) LIKE '%колод%' THEN 'Гальмівна система' "
        "WHEN LOWER(TRIM(COALESCE(name,'')) || ' ' || TRIM(COALESCE(note,'')) || ' ' || TRIM(COALESCE(node_tags,''))) LIKE '%аморт%' OR LOWER(TRIM(COALESCE(name,'')) || ' ' || TRIM(COALESCE(note,'')) || ' ' || TRIM(COALESCE(node_tags,''))) LIKE '%сайлент%' OR LOWER(TRIM(COALESCE(name,'')) || ' ' || TRIM(COALESCE(note,'')) || ' ' || TRIM(COALESCE(node_tags,''))) LIKE '%suspension%' THEN 'Підвіска' "
        "WHEN LOWER(TRIM(COALESCE(name,'')) || ' ' || TRIM(COALESCE(note,'')) || ' ' || TRIM(COALESCE(node_tags,''))) LIKE '%грм%' OR LOWER(TRIM(COALESCE(name,'')) || ' ' || TRIM(COALESCE(note,'')) || ' ' || TRIM(COALESCE(node_tags,''))) LIKE '%timing%' THEN 'ГРМ' "
        "WHEN LOWER(TRIM(COALESCE(name,'')) || ' ' || TRIM(COALESCE(note,'')) || ' ' || TRIM(COALESCE(node_tags,''))) LIKE '%радіатор%' OR LOWER(TRIM(COALESCE(name,'')) || ' ' || TRIM(COALESCE(note,'')) || ' ' || TRIM(COALESCE(node_tags,''))) LIKE '%термостат%' OR LOWER(TRIM(COALESCE(name,'')) || ' ' || TRIM(COALESCE(note,'')) || ' ' || TRIM(COALESCE(node_tags,''))) LIKE '%coolant%' THEN 'Система охолодження' "
        "WHEN LOWER(TRIM(COALESCE(name,'')) || ' ' || TRIM(COALESCE(note,'')) || ' ' || TRIM(COALESCE(node_tags,''))) LIKE '%свіч%' OR LOWER(TRIM(COALESCE(name,'')) || ' ' || TRIM(COALESCE(note,'')) || ' ' || TRIM(COALESCE(node_tags,''))) LIKE '%coil%' OR LOWER(TRIM(COALESCE(name,'')) || ' ' || TRIM(COALESCE(note,'')) || ' ' || TRIM(COALESCE(node_tags,''))) LIKE '%запал%' THEN 'Запалювання' "
        "WHEN LOWER(TRIM(COALESCE(name,'')) || ' ' || TRIM(COALESCE(note,'')) || ' ' || TRIM(COALESCE(node_tags,''))) LIKE '%руль%' OR LOWER(TRIM(COALESCE(name,'')) || ' ' || TRIM(COALESCE(note,'')) || ' ' || TRIM(COALESCE(node_tags,''))) LIKE '%рейк%' THEN 'Рульове керування' "
        "WHEN LOWER(TRIM(COALESCE(name,'')) || ' ' || TRIM(COALESCE(note,'')) || ' ' || TRIM(COALESCE(node_tags,''))) LIKE '%зчеп%' OR LOWER(TRIM(COALESCE(name,'')) || ' ' || TRIM(COALESCE(note,'')) || ' ' || TRIM(COALESCE(node_tags,''))) LIKE '%clutch%' THEN 'Зчеплення' "
        "WHEN LOWER(TRIM(COALESCE(name,'')) || ' ' || TRIM(COALESCE(note,'')) || ' ' || TRIM(COALESCE(node_tags,''))) LIKE '%акпп%' OR LOWER(TRIM(COALESCE(name,'')) || ' ' || TRIM(COALESCE(note,'')) || ' ' || TRIM(COALESCE(node_tags,''))) LIKE '%коробк%' OR LOWER(TRIM(COALESCE(name,'')) || ' ' || TRIM(COALESCE(note,'')) || ' ' || TRIM(COALESCE(node_tags,''))) LIKE '%transmission%' THEN 'Трансмісія' "
        "WHEN LOWER(TRIM(COALESCE(name,'')) || ' ' || TRIM(COALESCE(note,'')) || ' ' || TRIM(COALESCE(node_tags,''))) LIKE '%генератор%' OR LOWER(TRIM(COALESCE(name,'')) || ' ' || TRIM(COALESCE(note,'')) || ' ' || TRIM(COALESCE(node_tags,''))) LIKE '%стартер%' OR LOWER(TRIM(COALESCE(name,'')) || ' ' || TRIM(COALESCE(note,'')) || ' ' || TRIM(COALESCE(node_tags,''))) LIKE '%електр%' THEN 'Електрика' "
        "WHEN LOWER(TRIM(COALESCE(name,'')) || ' ' || TRIM(COALESCE(note,'')) || ' ' || TRIM(COALESCE(node_tags,''))) LIKE '%кондиціон%' OR LOWER(TRIM(COALESCE(name,'')) || ' ' || TRIM(COALESCE(note,'')) || ' ' || TRIM(COALESCE(node_tags,''))) LIKE '%compressor%' THEN 'Кондиціонер / клімат' "
        "WHEN LOWER(TRIM(COALESCE(name,'')) || ' ' || TRIM(COALESCE(note,'')) || ' ' || TRIM(COALESCE(node_tags,''))) LIKE '%вихлоп%' OR LOWER(TRIM(COALESCE(name,'')) || ' ' || TRIM(COALESCE(note,'')) || ' ' || TRIM(COALESCE(node_tags,''))) LIKE '%exhaust%' THEN 'Вихлопна система' "
        "WHEN LOWER(TRIM(COALESCE(name,'')) || ' ' || TRIM(COALESCE(note,'')) || ' ' || TRIM(COALESCE(node_tags,''))) LIKE '%двиг%' OR LOWER(TRIM(COALESCE(name,'')) || ' ' || TRIM(COALESCE(note,'')) || ' ' || TRIM(COALESCE(node_tags,''))) LIKE '%engine%' THEN 'Двигун' "
        "ELSE '' END");
    if (!execCatalogUpdate(partsGroupSql, QStringLiteral("normalize parts_catalog.business_group"))) return false;

        return true;
    });

    if (!tx.ok) {
        if (errorText) *errorText = tx.errorText;
        return false;
    }
    return true;
}
