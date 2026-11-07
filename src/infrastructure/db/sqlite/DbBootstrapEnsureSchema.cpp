// P2 architecture migration: implementation module converted from previous .ipp implementation-fragment decomposition.
// Compiled as a separate P2 implementation module; include-order dependencies are isolated in the module prelude.

#include "infrastructure/db/sqlite/DbBootstrapModulePrelude.h"
#include "core/diagnostics/RuntimeLoggingControl.h"
#include "core/repository/RepositoryWriteBoundary.h"
#include "core/services/AdditionalExpenseService.h"
#include "infrastructure/db/sqlite/DbBootstrapEnsureSchema.h"

bool Database::ensureSchema()
{
    return ensureWorkspaceSchema(db());
}

bool ensureWorkspaceSchema(QSqlDatabase d)
{
    if (!d.isValid() || !d.isOpen()) return false;
    QString preparationError;
    RepositoryWriteBoundary preparation(d, &preparationError);
    if (!preparation.started()) {
        qWarning() << "Workspace schema preparation failed:" << preparationError;
        return false;
    }
    bool columnsReady = true;
    const auto ensureColumn = [&](const QString& table, const QString& column, const QString& ddl) {
        QString error;
        if (ensureColumnForDb(d, table, column, ddl, &error)) return true;
        columnsReady = false;
        qWarning() << error;
        return false;
    };
    QSqlQuery q(d);

    // masters
    if (!q.exec(
            "CREATE TABLE IF NOT EXISTS masters ("
            "  id TEXT PRIMARY KEY,"
            "  name TEXT NOT NULL,"
            "  role TEXT NOT NULL DEFAULT 'master',"
            "  admin_percent REAL NOT NULL DEFAULT 0,"
            "  active INTEGER NOT NULL DEFAULT 1,"
            "  active_from TEXT NOT NULL DEFAULT '',"
            "  active_to TEXT NOT NULL DEFAULT ''"
            ");")) {
        qCritical() << "Create masters failed" << q.lastError().text();
        return false;
    }

    // work_orders
    if (!q.exec(
            "CREATE TABLE IF NOT EXISTS work_orders ("
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
            "  master_id TEXT NOT NULL,"
            "  works_sum REAL NOT NULL DEFAULT 0,"
            "  parts_sum REAL NOT NULL DEFAULT 0,"
            "  discount REAL NOT NULL DEFAULT 0,"
            "  master_name_closed TEXT NOT NULL DEFAULT '',"
            "  mobile_state_json TEXT NOT NULL DEFAULT '{}',"
            "  updated_at TEXT NOT NULL DEFAULT '',"
            "  FOREIGN KEY(master_id) REFERENCES masters(id)"
            ");")) {
        qCritical() << "Create work_orders failed" << q.lastError().text();
        return false;
    }

    // Existing DBs may already have work_orders without canonical identity columns.
    // Ensure these columns before any backfill/rebuild path references wo.order_uid/order_number/order_year.
    ensureColumn("work_orders", "order_uid", "order_uid TEXT NOT NULL DEFAULT ''");
    ensureColumn("work_orders", "order_number", "order_number INTEGER NOT NULL DEFAULT 0");
    ensureColumn("work_orders", "order_year", "order_year INTEGER NOT NULL DEFAULT 0");
    ensureColumn("work_orders", "mobile_state_json", "mobile_state_json TEXT NOT NULL DEFAULT '{}'");
    ensureColumn("work_orders", "updated_at", "updated_at TEXT NOT NULL DEFAULT ''");

    if (!ensureColumn("work_orders", "discount_mode", "discount_mode TEXT NOT NULL DEFAULT 'legacy'")) return false;

    // services
    if (!q.exec(
            "CREATE TABLE IF NOT EXISTS work_order_services ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  order_uid TEXT NOT NULL DEFAULT '',"
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
        qCritical() << "Create work_order_services failed" << q.lastError().text();
        return false;
    }

    // Existing service rows must have routing columns before service_masters backfill uses them.
    ensureColumn("work_order_services", "order_uid", "order_uid TEXT NOT NULL DEFAULT ''");
    ensureColumn("work_order_services", "order_number", "order_number INTEGER NOT NULL DEFAULT 0");
    ensureColumn("work_order_services", "service_uid", "service_uid TEXT NOT NULL DEFAULT ''");
    ensureColumn("work_order_services", "status", "status TEXT NOT NULL DEFAULT 'open'");
    ensureColumn("work_order_services", "completed_at", "completed_at TEXT NOT NULL DEFAULT ''");
    ensureColumn("work_order_services", "completed_by", "completed_by TEXT NOT NULL DEFAULT ''");
    ensureColumn("work_order_services", "assigned_to", "assigned_to TEXT NOT NULL DEFAULT ''");
    ensureColumn("work_order_services", "assigned_to_name", "assigned_to_name TEXT NOT NULL DEFAULT ''");
    ensureColumn("work_order_services", "source", "source TEXT NOT NULL DEFAULT ''");
    ensureColumn("work_order_services", "diagnostic_uid", "diagnostic_uid TEXT NOT NULL DEFAULT ''");
    ensureColumn("work_order_services", "source_suggestion_uid", "source_suggestion_uid TEXT NOT NULL DEFAULT ''");
    ensureColumn("work_order_services", "metadata_json", "metadata_json TEXT NOT NULL DEFAULT '{}'");
    ensureColumn("work_order_services", "created_at", "created_at TEXT NOT NULL DEFAULT ''");
    ensureColumn("work_order_services", "updated_at", "updated_at TEXT NOT NULL DEFAULT ''");

    // service_masters
    if (!q.exec(
            "CREATE TABLE IF NOT EXISTS service_masters ("
            "  service_id INTEGER NOT NULL,"
            "  master_id TEXT NOT NULL,"
            "  share_percent REAL NOT NULL DEFAULT 100,"
            "  role TEXT NOT NULL DEFAULT 'master',"
            "  master_name_closed TEXT NOT NULL DEFAULT '',"
            "  PRIMARY KEY(service_id, master_id),"
            "  FOREIGN KEY(service_id) REFERENCES work_order_services(id) ON DELETE CASCADE,"
            "  FOREIGN KEY(master_id) REFERENCES masters(id)"
            ");")) {
        qCritical() << "Create service_masters failed" << q.lastError().text();
        return false;
    }


// Backfill service_masters for existing rows (uses work_orders.master_id when needed)
// This enables multi-master payroll/reporting even for older orders that were saved before service-level assignment existed.
if (!q.exec(
        "INSERT OR IGNORE INTO service_masters(service_id, master_id, share_percent, role, master_name_closed) "
        "SELECT s.id, wo.master_id, 100.0, 'master', COALESCE(wo.master_name_closed, m.name, '') "
        "FROM work_order_services s "
        "JOIN work_orders wo ON wo.number = s.order_number "
        "LEFT JOIN masters m ON m.id = wo.master_id "
        "WHERE wo.master_id IS NOT NULL AND wo.master_id <> '' "
        "AND NOT EXISTS (SELECT 1 FROM service_masters sm WHERE sm.service_id = s.id)"
        ";")) {
    qWarning() << "Backfill service_masters failed" << q.lastError().text();
    // non-fatal
}

    // parts
    if (!q.exec(
            "CREATE TABLE IF NOT EXISTS work_order_parts ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
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
        qCritical() << "Create work_order_parts failed" << q.lastError().text();
        return false;
    }

    // Existing part rows must have routing columns before canonical rebuild uses parent UID lookups.
    ensureColumn("work_order_parts", "order_uid", "order_uid TEXT NOT NULL DEFAULT ''");
    ensureColumn("work_order_parts", "order_number", "order_number INTEGER NOT NULL DEFAULT 0");
    ensureColumn("work_order_parts", "part_uid", "part_uid TEXT NOT NULL DEFAULT ''");
    ensureColumn("work_order_parts", "status", "status TEXT NOT NULL DEFAULT ''");
    ensureColumn("work_order_parts", "installed", "installed INTEGER NOT NULL DEFAULT 0");
    ensureColumn("work_order_parts", "installed_by", "installed_by TEXT NOT NULL DEFAULT ''");
    ensureColumn("work_order_parts", "request_uid", "request_uid TEXT NOT NULL DEFAULT ''");
    ensureColumn("work_order_parts", "requested_at", "requested_at TEXT NOT NULL DEFAULT ''");
    ensureColumn("work_order_parts", "requested_by", "requested_by TEXT NOT NULL DEFAULT ''");
    ensureColumn("work_order_parts", "requested_by_uid", "requested_by_uid TEXT NOT NULL DEFAULT ''");
    ensureColumn("work_order_parts", "ordered_by", "ordered_by TEXT NOT NULL DEFAULT ''");
    ensureColumn("work_order_parts", "ordered_by_uid", "ordered_by_uid TEXT NOT NULL DEFAULT ''");
    ensureColumn("work_order_parts", "order_comment", "order_comment TEXT NOT NULL DEFAULT ''");
    ensureColumn("work_order_parts", "node_group", "node_group TEXT NOT NULL DEFAULT ''");
    ensureColumn("work_order_parts", "component", "component TEXT NOT NULL DEFAULT ''");
    ensureColumn("work_order_parts", "metadata_json", "metadata_json TEXT NOT NULL DEFAULT '{}'");
    ensureColumn("work_order_parts", "created_at", "created_at TEXT NOT NULL DEFAULT ''");
    ensureColumn("work_order_parts", "updated_at", "updated_at TEXT NOT NULL DEFAULT ''");

    // Drop stale FTS maintenance triggers from older builds before any canonical rebuild.
    // They may reference the dropped/renamed work_order_parts table and break both
    // invoice import INSERTs and ALTER TABLE RENAME with "Unable to fetch row".
    if (!dropFtsTriggersReferencingTable(d, QStringLiteral("work_order_parts"))) {
        qWarning() << "Failed to drop stale work_order_parts FTS triggers before schema normalization";
    }

    // Normalize active parts table to the canonical new-schema layout.
    // Previous aliases must stay only in archive sources; keeping them in active caused
    // mixed-schema inserts to fail on some migrated user databases.
    if (!rebuildActivePartsTableToCanonical(d)) {
        qWarning() << "Failed to normalize active work_order_parts table to canonical schema";
    }


    // outsource
    if (!q.exec(
            "CREATE TABLE IF NOT EXISTS outsource_repairs ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  order_uid TEXT NOT NULL DEFAULT '',"
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
        qCritical() << "Create outsource_repairs failed" << q.lastError().text();
        return false;
    }

    // catalog
    if (!q.exec(
            "CREATE TABLE IF NOT EXISTS parts_catalog ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  part_number TEXT NOT NULL,"
            "  name TEXT NOT NULL,"
            "  brand TEXT NOT NULL DEFAULT '',"
            "  price REAL NOT NULL DEFAULT 0"
            ");")) {
        qCritical() << "Create parts_catalog failed" << q.lastError().text();
        return false;
    }

    // outsource jobs for per-service outsourcing state used by dialogs and reports
    if (!q.exec(
            "CREATE TABLE IF NOT EXISTS outsource_jobs ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  order_uid TEXT NOT NULL DEFAULT '',"
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
        qCritical() << "Create outsource_jobs failed" << q.lastError().text();
        return false;
    }


    // backfill/migrate: canonical columns for older active DBs
    ensureColumn("work_order_services", "order_uid", "order_uid TEXT NOT NULL DEFAULT ''");
    ensureColumn("work_order_parts", "order_uid", "order_uid TEXT NOT NULL DEFAULT ''");
    ensureColumn("outsource_repairs", "order_uid", "order_uid TEXT NOT NULL DEFAULT ''");
    ensureColumn("outsource_jobs", "order_uid", "order_uid TEXT NOT NULL DEFAULT ''");
    ensureColumn("work_order_parts", "delivery_date", "delivery_date TEXT NOT NULL DEFAULT ''");
    ensureColumn("work_order_parts", "installed_at", "installed_at TEXT NOT NULL DEFAULT ''");
    ensureColumn("work_order_parts", "paid", "paid INTEGER NOT NULL DEFAULT 0");
    ensureColumn("work_order_parts", "part_number", "part_number TEXT");
    ensureColumn("work_order_parts", "manufacturer", "manufacturer TEXT NOT NULL DEFAULT ''");
    ensureColumn("work_order_parts", "purchase_price", "purchase_price REAL NOT NULL DEFAULT 0");
    ensureColumn("work_order_parts", "purchase_amount", "purchase_amount REAL NOT NULL DEFAULT 0");
    ensureColumn("work_order_parts", "markup_percent", "markup_percent REAL NOT NULL DEFAULT 0");
    ensureColumn("work_order_parts", "manual_markup_override", "manual_markup_override INTEGER NOT NULL DEFAULT 0");
    ensureColumn("work_order_parts", "markup_source", "markup_source TEXT NOT NULL DEFAULT 'policy'");
    ensureColumn("work_order_parts", "price_input_mode", "price_input_mode TEXT NOT NULL DEFAULT 'unknown'");
    ensureColumn("work_order_parts", "pricing_updated_at", "pricing_updated_at TEXT NOT NULL DEFAULT ''");
    ensureColumn("work_order_parts", "pricing_calc_version", "pricing_calc_version INTEGER NOT NULL DEFAULT 2");

    // Identity repair configures foreign keys outside the preparation savepoint.
    if (!columnsReady || !preparation.finish()) {
        qWarning() << "Workspace schema preparation failed:" << preparationError;
        return false;
    }
    q.exec("UPDATE work_order_parts SET purchase_price = ROUND(price / (1.0 + markup_percent / 100.0), 2) WHERE purchase_price <= 0 AND price > 0 AND markup_percent > 0 AND COALESCE(manual_markup_override,0) = 0");
    q.exec("UPDATE work_order_parts SET purchase_price = price WHERE purchase_price <= 0 AND price > 0 AND (COALESCE(markup_percent,0) <= 0 OR COALESCE(manual_markup_override,0) <> 0)");
    q.exec("UPDATE work_order_parts SET purchase_amount = ROUND(purchase_price * COALESCE(NULLIF(qty,0),1), 2) WHERE purchase_amount <= 0 AND purchase_price > 0");
    q.exec("UPDATE work_order_parts SET amount = CASE WHEN amount <= 0 THEN ROUND(price * COALESCE(NULLIF(qty,0),1), 2) ELSE amount END WHERE amount <= 0");
    q.exec("UPDATE work_order_parts SET markup_source=CASE WHEN COALESCE(markup_source,'')='' OR COALESCE(markup_source,'policy')='policy' THEN 'legacy' ELSE markup_source END WHERE COALESCE(pricing_updated_at,'')='' AND COALESCE(price,0)>0 AND COALESCE(purchase_price,0)<=0");
    q.exec("UPDATE work_order_parts SET manual_markup_override=1, markup_source='manual' WHERE COALESCE(pricing_updated_at,'')='' AND COALESCE(markup_percent,0)>0 AND COALESCE(manual_markup_override,0) = 0");
    q.exec("UPDATE work_order_parts SET markup_source='manual' WHERE COALESCE(manual_markup_override,0)<>0");
    q.exec("UPDATE work_order_parts SET markup_source='legacy' WHERE COALESCE(pricing_updated_at,'')='' AND COALESCE(manual_markup_override,0) = 0 AND (COALESCE(markup_source,'')='' OR COALESCE(markup_source,'policy')='policy')");
    q.exec("UPDATE work_order_parts SET markup_source='policy' WHERE COALESCE(markup_source,'')=''");
    q.exec("UPDATE work_order_parts SET pricing_updated_at=datetime('now') WHERE COALESCE(pricing_updated_at,'')=''");
    QString identityError;
    if (!OrderIdentityService::ensureCanonicalIdentityIndexes(d, &identityError)) {
        qWarning() << "canonical identity migration stopped:" << identityError;
        return false;
    }
    if (!q.exec("UPDATE work_orders SET works_sum=COALESCE((SELECT SUM(COALESCE(s.amount, COALESCE(s.qty,1)*COALESCE(s.price,0))) FROM work_order_services s WHERE ((TRIM(COALESCE(s.order_uid,''))<>'' AND TRIM(COALESCE(s.order_uid,''))=TRIM(COALESCE(work_orders.order_uid,''))) OR (TRIM(COALESCE(s.order_uid,''))='' AND COALESCE(s.order_number,0)=COALESCE(work_orders.number,0)))),0), parts_sum=COALESCE((SELECT SUM(COALESCE(p.amount, COALESCE(p.qty,1)*COALESCE(p.price,0))) FROM work_order_parts p WHERE ((TRIM(COALESCE(p.order_uid,''))<>'' AND TRIM(COALESCE(p.order_uid,''))=TRIM(COALESCE(work_orders.order_uid,''))) OR (TRIM(COALESCE(p.order_uid,''))='' AND COALESCE(p.order_number,0)=COALESCE(work_orders.number,0)))),0)")) {
        qWarning() << "Workspace order totals failed:" << q.lastError().text();
        return false;
    }
    q.exec("CREATE INDEX IF NOT EXISTS idx_work_order_services_order_uid ON work_order_services(order_uid)");
    q.exec("CREATE INDEX IF NOT EXISTS idx_work_order_services_order_uid_id ON work_order_services(order_uid, id)");
    q.exec("CREATE INDEX IF NOT EXISTS idx_work_order_services_order_number ON work_order_services(order_number)");
    q.exec("CREATE INDEX IF NOT EXISTS idx_work_order_parts_order_uid ON work_order_parts(order_uid)");
    q.exec("CREATE INDEX IF NOT EXISTS idx_work_order_parts_order_uid_id ON work_order_parts(order_uid, id)");
    q.exec("CREATE INDEX IF NOT EXISTS idx_work_order_parts_order_number ON work_order_parts(order_number)");
    q.exec("CREATE INDEX IF NOT EXISTS idx_outsource_repairs_order_uid ON outsource_repairs(order_uid)");
    q.exec("CREATE INDEX IF NOT EXISTS idx_outsource_jobs_order_uid ON outsource_jobs(order_uid)");

    if (!rebuildActiveServicesTableToOrderUid(d)) {
        qWarning() << "Failed to normalize active work_order_services table to order_uid schema";
    }
    if (!rebuildActiveOutsourceRepairsTableToOrderUid(d)) {
        qWarning() << "Failed to normalize active outsource_repairs table to order_uid schema";
    }
    if (!rebuildActiveOutsourceJobsTableToOrderUid(d)) {
        qWarning() << "Failed to normalize active outsource_jobs table to order_uid schema";
    }

    RepositoryWriteBoundary dependentTables(d, &preparationError);
    if (!dependentTables.started()) {
        qWarning() << "Dependent table preparation failed:" << preparationError;
        return false;
    }
    // attachment tables used by work/parts dialog
    if (!q.exec(
            "CREATE TABLE IF NOT EXISTS work_order_service_attachments ("
            "  service_id INTEGER PRIMARY KEY,"
            "  file_path TEXT NOT NULL DEFAULT '',"
            "  display_name TEXT NOT NULL DEFAULT '',"
            "  FOREIGN KEY(service_id) REFERENCES work_order_services(id) ON DELETE CASCADE"
            ");")) {
        qCritical() << "Create work_order_service_attachments failed" << q.lastError().text();
        return false;
    }
    if (!q.exec(
            "CREATE TABLE IF NOT EXISTS work_order_part_attachments ("
            "  part_id INTEGER PRIMARY KEY,"
            "  file_path TEXT NOT NULL DEFAULT '',"
            "  display_name TEXT NOT NULL DEFAULT '',"
            "  invoice_number TEXT NOT NULL DEFAULT '',"
            "  invoice_date TEXT NOT NULL DEFAULT '',"
            "  FOREIGN KEY(part_id) REFERENCES work_order_parts(id) ON DELETE CASCADE"
            ");")) {
        qCritical() << "Create work_order_part_attachments failed" << q.lastError().text();
        return false;
    }
    QString attachmentSchemaError;
    if (!ensureColumnForDb(d, QStringLiteral("work_order_part_attachments"), QStringLiteral("invoice_number"), QStringLiteral("invoice_number TEXT NOT NULL DEFAULT ''"), &attachmentSchemaError)) {
        qCritical() << "Ensure work_order_part_attachments.invoice_number failed" << attachmentSchemaError;
        return false;
    }
    if (!ensureColumnForDb(d, QStringLiteral("work_order_part_attachments"), QStringLiteral("invoice_date"), QStringLiteral("invoice_date TEXT NOT NULL DEFAULT ''"), &attachmentSchemaError)) {
        qCritical() << "Ensure work_order_part_attachments.invoice_date failed" << attachmentSchemaError;
        return false;
    }

    // workshop inventory purchases. Consumption is derived from work-order parts,
    // while incoming materials remain independent from client part orders.
    if (!q.exec(
            "CREATE TABLE IF NOT EXISTS workshop_inventory_purchases ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  purchased_on TEXT NOT NULL,"
            "  material_name TEXT NOT NULL,"
            "  material_key TEXT NOT NULL,"
            "  unit TEXT NOT NULL DEFAULT 'од.',"
            "  quantity REAL NOT NULL CHECK(quantity > 0),"
            "  unit_cost REAL NOT NULL DEFAULT 0 CHECK(unit_cost >= 0),"
            "  supplier TEXT NOT NULL DEFAULT '',"
            "  note TEXT NOT NULL DEFAULT '',"
            "  created_at TEXT NOT NULL DEFAULT (datetime('now'))"
            ");")) {
        qCritical() << "Create workshop_inventory_purchases failed" << q.lastError().text();
        return false;
    }
    if (!q.exec("CREATE INDEX IF NOT EXISTS idx_workshop_inventory_purchases_material_key ON workshop_inventory_purchases(material_key)")
        || !q.exec("CREATE INDEX IF NOT EXISTS idx_workshop_inventory_purchases_date ON workshop_inventory_purchases(purchased_on)")) {
        qCritical() << "Create workshop inventory indexes failed" << q.lastError().text();
        return false;
    }

    // expenses
    if (!AdditionalExpenseService::ensureSchema(d)) return false;
    if (!q.exec(
            "CREATE TABLE IF NOT EXISTS expenses ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  period_from TEXT NOT NULL,"
            "  period_to TEXT NOT NULL,"
            "  elec_prev REAL NOT NULL DEFAULT 0,"
            "  elec_curr REAL NOT NULL DEFAULT 0,"
            "  elec_tariff REAL NOT NULL DEFAULT 0,"
            "  cleaning REAL NOT NULL DEFAULT 0,"
            "  heating REAL NOT NULL DEFAULT 0,"
            "  rent REAL NOT NULL DEFAULT 0,"
            "  amortization REAL NOT NULL DEFAULT 0,"
            "  sw_opendatabot REAL NOT NULL DEFAULT 0,"
            "  sw_tecalliance REAL NOT NULL DEFAULT 0,"
            "  created_at TEXT NOT NULL DEFAULT (datetime('now'))"
            ");")) {
        qCritical() << "Create expenses failed" << q.lastError().text();
        return false;
    }

    
// settings (key-value)
if (!q.exec(
        "CREATE TABLE IF NOT EXISTS settings ("
        "  key TEXT PRIMARY KEY,"
        "  value TEXT NOT NULL DEFAULT ''"
        ");")) {
    qCritical() << "Create settings failed" << q.lastError().text();
    return false;
}

// deleted_imports (to remember removed external rows and never import again)
if (!q.exec(
        "CREATE TABLE IF NOT EXISTS deleted_imports ("
        "  source TEXT NOT NULL,"
        "  external_id TEXT NOT NULL,"
        "  deleted_at TEXT NOT NULL DEFAULT (datetime('now')),"
        "  PRIMARY KEY(source, external_id)"
        ");")) {
    qCritical() << "Create deleted_imports failed" << q.lastError().text();
    return false;
}

// planning_calendar
    if (!q.exec(
            "CREATE TABLE IF NOT EXISTS planning_calendar ("
            "  day TEXT PRIMARY KEY,"
            "  note TEXT NOT NULL DEFAULT ''"
            ");")) {
        qCritical() << "Create planning_calendar failed" << q.lastError().text();
        return false;
    }


// online_bookings: website/app booking synchronization. Website creates requests,
// application writes accepted online requests into Planning only when a local slot is free.
    if (!q.exec(
            "CREATE TABLE IF NOT EXISTS online_bookings ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  booking_uid TEXT NOT NULL UNIQUE,"
            "  client_name TEXT NOT NULL DEFAULT '',"
            "  client_phone TEXT NOT NULL DEFAULT '',"
            "  client_email TEXT NOT NULL DEFAULT '',"
            "  car_make_model TEXT NOT NULL DEFAULT '',"
            "  car_make TEXT NOT NULL DEFAULT '',"
            "  car_model TEXT NOT NULL DEFAULT '',"
            "  car_year TEXT NOT NULL DEFAULT '',"
            "  fuel_type TEXT NOT NULL DEFAULT '',"
            "  engine_volume_l TEXT NOT NULL DEFAULT '',"
            "  odometer_km TEXT NOT NULL DEFAULT '',"
            "  car_number TEXT NOT NULL DEFAULT '',"
            "  service_type TEXT NOT NULL DEFAULT '',"
            "  comment TEXT NOT NULL DEFAULT '',"
            "  date TEXT NOT NULL DEFAULT '',"
            "  time_from TEXT NOT NULL DEFAULT '',"
            "  time_to TEXT NOT NULL DEFAULT '',"
            "  status TEXT NOT NULL DEFAULT 'pending',"
            "  source TEXT NOT NULL DEFAULT 'online',"
            "  planning_day TEXT NOT NULL DEFAULT '',"
            "  planning_hour INTEGER NOT NULL DEFAULT -1,"
            "  planning_synced INTEGER NOT NULL DEFAULT 0,"
            "  rejection_reason TEXT NOT NULL DEFAULT '',"
            "  created_at TEXT NOT NULL DEFAULT (datetime('now')) ,"
            "  updated_at TEXT NOT NULL DEFAULT (datetime('now'))"
            ");")) {
        qCritical() << "Create online_bookings failed" << q.lastError().text();
        return false;
    }
    q.exec("CREATE INDEX IF NOT EXISTS idx_online_bookings_updated_at ON online_bookings(updated_at)");
    q.exec("CREATE INDEX IF NOT EXISTS idx_online_bookings_status ON online_bookings(status)");
    q.exec("CREATE INDEX IF NOT EXISTS idx_online_bookings_day_hour ON online_bookings(planning_day, planning_hour)");
    ensureColumn("online_bookings", "car_make", "car_make TEXT NOT NULL DEFAULT ''");
    ensureColumn("online_bookings", "car_model", "car_model TEXT NOT NULL DEFAULT ''");
    ensureColumn("online_bookings", "car_year", "car_year TEXT NOT NULL DEFAULT ''");
    ensureColumn("online_bookings", "fuel_type", "fuel_type TEXT NOT NULL DEFAULT ''");
    ensureColumn("online_bookings", "engine_volume_l", "engine_volume_l TEXT NOT NULL DEFAULT ''");
    ensureColumn("online_bookings", "odometer_km", "odometer_km TEXT NOT NULL DEFAULT ''");

    // Ensure columns (migrations)
    ensureColumn("expenses", "other_amount", "other_amount REAL NOT NULL DEFAULT 0");
    ensureColumn("expenses", "cleaning_note", "cleaning_note TEXT NOT NULL DEFAULT ''");
    ensureColumn("expenses", "software_note", "software_note TEXT NOT NULL DEFAULT ''");
    ensureColumn("expenses", "heating_note", "heating_note TEXT NOT NULL DEFAULT ''");
    ensureColumn("expenses", "amortization_note", "amortization_note TEXT NOT NULL DEFAULT ''");
    ensureColumn("expenses", "rent_note", "rent_note TEXT NOT NULL DEFAULT ''");
    ensureColumn("expenses", "other_note", "other_note TEXT NOT NULL DEFAULT ''");
    ensureColumn("expenses", "elec_kwh", "elec_kwh REAL NOT NULL DEFAULT 0");
    ensureColumn("expenses", "elec_cost", "elec_cost REAL NOT NULL DEFAULT 0");

    { QSqlQuery q(d); q.exec(
            "CREATE TABLE IF NOT EXISTS electricity_periods ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  period_from TEXT NOT NULL,"
            "  period_to TEXT NOT NULL,"
            "  meter_before REAL NOT NULL DEFAULT 0,"
            "  meter_after REAL NOT NULL DEFAULT 0,"
            "  consumed_kwh REAL NOT NULL DEFAULT 0,"
            "  tariff REAL NOT NULL DEFAULT 0,"
            "  amount REAL NOT NULL DEFAULT 0,"
            "  created_at TEXT NOT NULL DEFAULT (datetime('now'))"
            ");"); }

    { QSqlQuery q(d); q.exec(
            "CREATE TABLE IF NOT EXISTS payroll_order_adjustments ("
            "  master_id TEXT NOT NULL,"
            "  order_number INTEGER NOT NULL,"
            "  absent INTEGER NOT NULL DEFAULT 0,"
            "  deleted INTEGER NOT NULL DEFAULT 0,"
            "  updated_at TEXT NOT NULL DEFAULT (datetime('now')),"
            "  PRIMARY KEY(master_id, order_number)"
            ");"); }
    ensureColumn("payroll_order_adjustments", "absent", "absent INTEGER NOT NULL DEFAULT 0");
    ensureColumn("payroll_order_adjustments", "deleted", "deleted INTEGER NOT NULL DEFAULT 0");
    ensureColumn("payroll_order_adjustments", "updated_at", "updated_at TEXT NOT NULL DEFAULT ''");

    ensureColumn("parts_catalog", "vehicle_brand", "vehicle_brand TEXT NOT NULL DEFAULT ''");
    ensureColumn("parts_catalog", "vehicle_model", "vehicle_model TEXT NOT NULL DEFAULT ''");
    ensureColumn("parts_catalog", "year_from", "year_from INTEGER NOT NULL DEFAULT 0");
    ensureColumn("parts_catalog", "year_to", "year_to INTEGER NOT NULL DEFAULT 0");
    ensureColumn("parts_catalog", "fuel_type", "fuel_type TEXT NOT NULL DEFAULT ''");
    ensureColumn("parts_catalog", "engine_type", "engine_type TEXT NOT NULL DEFAULT ''");
    ensureColumn("parts_catalog", "body_type", "body_type TEXT NOT NULL DEFAULT ''");
    ensureColumn("parts_catalog", "group_name", "group_name TEXT NOT NULL DEFAULT ''");
    ensureColumn("parts_catalog", "supplier", "supplier TEXT NOT NULL DEFAULT ''");
    ensureColumn("parts_catalog", "currency", "currency TEXT NOT NULL DEFAULT 'UAH'");
    ensureColumn("parts_catalog", "unit", "unit TEXT NOT NULL DEFAULT 'шт'");
    ensureColumn("parts_catalog", "note", "note TEXT NOT NULL DEFAULT ''");
    ensureColumn("parts_catalog", "source_type", "source_type TEXT NOT NULL DEFAULT ''");
    ensureColumn("parts_catalog", "source_order_uid_first", "source_order_uid_first TEXT NOT NULL DEFAULT ''");
    ensureColumn("parts_catalog", "source_order_uid_last", "source_order_uid_last TEXT NOT NULL DEFAULT ''");
    ensureColumn("parts_catalog", "first_imported_at", "first_imported_at TEXT NOT NULL DEFAULT ''");
    ensureColumn("parts_catalog", "last_imported_at", "last_imported_at TEXT NOT NULL DEFAULT ''");
    ensureColumn("parts_catalog", "manually_edited", "manually_edited INTEGER NOT NULL DEFAULT 0");
    ensureColumn("parts_catalog", "is_active", "is_active INTEGER NOT NULL DEFAULT 1");
    ensureColumn("outsource_jobs", "company_id", "company_id INTEGER NOT NULL DEFAULT 0");
    ensureColumn("outsource_jobs", "company_name", "company_name TEXT NOT NULL DEFAULT ''");
    ensureColumn("outsource_jobs", "sent_date", "sent_date TEXT NOT NULL DEFAULT ''");
    ensureColumn("outsource_jobs", "return_date", "return_date TEXT NOT NULL DEFAULT ''");
    ensureColumn("outsource_jobs", "part_name", "part_name TEXT NOT NULL DEFAULT ''");
    ensureColumn("outsource_jobs", "company_price", "company_price REAL NOT NULL DEFAULT 0");
    ensureColumn("outsource_jobs", "markup_percent", "markup_percent REAL NOT NULL DEFAULT 0");
    ensureColumn("outsource_jobs", "client_price", "client_price REAL NOT NULL DEFAULT 0");
    ensureColumn("outsource_jobs", "note", "note TEXT NOT NULL DEFAULT ''");
    ensureColumn("work_orders", "order_uid", "order_uid TEXT NOT NULL DEFAULT ''");
    ensureColumn("work_orders", "order_number", "order_number INTEGER NOT NULL DEFAULT 0");
    ensureColumn("work_orders", "order_year", "order_year INTEGER NOT NULL DEFAULT 0");
    ensureColumn("work_orders", "car_model", "car_model TEXT NOT NULL DEFAULT ''");
    ensureColumn("work_orders", "color", "color TEXT NOT NULL DEFAULT ''");
    ensureColumn("work_orders", "car_color", "car_color TEXT NOT NULL DEFAULT ''");
    ensureColumn("work_orders", "vin", "vin TEXT NOT NULL DEFAULT ''");
    ensureColumn("work_orders", "engine_type", "engine_type TEXT NOT NULL DEFAULT ''");
    ensureColumn("work_orders", "engine_volume", "engine_volume TEXT NOT NULL DEFAULT ''");
    ensureColumn("masters", "role", "role TEXT NOT NULL DEFAULT 'master'");
    ensureColumn("masters", "admin_percent", "admin_percent REAL NOT NULL DEFAULT 0");
    ensureColumn("masters", "active", "active INTEGER NOT NULL DEFAULT 1");
    ensureColumn("masters", "active_from", "active_from TEXT NOT NULL DEFAULT ''");
    ensureColumn("masters", "active_to", "active_to TEXT NOT NULL DEFAULT ''");
    ensureColumn("work_order_parts", "ordered_by_admin", "ordered_by_admin INTEGER NOT NULL DEFAULT 1");
    ensureColumn("work_order_services", "note", "note TEXT NOT NULL DEFAULT ''");
    ensureColumn("work_order_services", "paid", "paid INTEGER NOT NULL DEFAULT 1");
    ensureColumn("work_order_services", "parent_id", "parent_id INTEGER");
    ensureColumn("outsource_repairs", "service_id", "service_id INTEGER");
    ensureColumn("work_orders", "master_name_closed", "master_name_closed TEXT NOT NULL DEFAULT ''");
    ensureColumn("work_orders", "car_brand", "car_brand TEXT NOT NULL DEFAULT ''");
    ensureColumn("work_orders", "car_kind", "car_kind TEXT NOT NULL DEFAULT ''");
    ensureColumn("work_orders", "owner_name", "owner_name TEXT NOT NULL DEFAULT ''");
    ensureColumn("service_masters", "master_name_closed", "master_name_closed TEXT NOT NULL DEFAULT ''");
    { QSqlQuery qfix(d); qfix.exec("UPDATE service_masters SET role='master' WHERE role IS NULL OR TRIM(role)=''"); }
    { QSqlQuery qfix(d); qfix.exec("UPDATE work_orders SET color=COALESCE(NULLIF(color,''), NULLIF(car_color,''), '')"); }
    { QSqlQuery qfix(d); qfix.exec("UPDATE work_orders SET car_color=COALESCE(NULLIF(car_color,''), NULLIF(color,''), '')"); }
    { QSqlQuery qfix(d); qfix.exec("UPDATE work_order_parts SET ordered_by_admin=1 WHERE ordered_by_admin IS NULL"); }

    { QSqlQuery q(d); q.exec(
            "CREATE TABLE IF NOT EXISTS catalog_import_runs ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  started_at TEXT NOT NULL DEFAULT (datetime('now')),"
            "  finished_at TEXT NOT NULL DEFAULT '',"
            "  processed_count INTEGER NOT NULL DEFAULT 0,"
            "  eligible_count INTEGER NOT NULL DEFAULT 0,"
            "  added_count INTEGER NOT NULL DEFAULT 0,"
            "  updated_count INTEGER NOT NULL DEFAULT 0,"
            "  skipped_count INTEGER NOT NULL DEFAULT 0,"
            "  skipped_oil_count INTEGER NOT NULL DEFAULT 0,"
            "  error_count INTEGER NOT NULL DEFAULT 0,"
            "  summary_text TEXT NOT NULL DEFAULT ''"
            ");"); }
    { QSqlQuery q(d); q.exec(
            "CREATE TABLE IF NOT EXISTS catalog_import_items ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  run_id INTEGER NOT NULL DEFAULT 0,"
            "  source_part_id INTEGER NOT NULL DEFAULT 0,"
            "  order_uid TEXT NOT NULL DEFAULT '',"
            "  order_number INTEGER NOT NULL DEFAULT 0,"
            "  part_number TEXT NOT NULL DEFAULT '',"
            "  name TEXT NOT NULL DEFAULT '',"
            "  action TEXT NOT NULL DEFAULT '',"
            "  catalog_id INTEGER NOT NULL DEFAULT 0,"
            "  message TEXT NOT NULL DEFAULT '',"
            "  is_oil INTEGER NOT NULL DEFAULT 0"
            ");"); }
    { QSqlQuery q(d); q.exec(
            "CREATE TABLE IF NOT EXISTS parts_catalog_vehicle_map ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  catalog_id INTEGER NOT NULL,"
            "  vehicle_brand TEXT NOT NULL DEFAULT '',"
            "  vehicle_model TEXT NOT NULL DEFAULT '',"
            "  year_from INTEGER NOT NULL DEFAULT 0,"
            "  year_to INTEGER NOT NULL DEFAULT 0,"
            "  fuel_type TEXT NOT NULL DEFAULT '',"
            "  engine_type TEXT NOT NULL DEFAULT '',"
            "  body_type TEXT NOT NULL DEFAULT '',"
            "  source_order_uid TEXT NOT NULL DEFAULT '',"
            "  last_seen_at TEXT NOT NULL DEFAULT (datetime('now'))"
            ");"); }
    { QSqlQuery q(d); q.exec(
            "CREATE TABLE IF NOT EXISTS parts_catalog_sources ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  catalog_id INTEGER NOT NULL,"
            "  source_part_id INTEGER NOT NULL DEFAULT 0,"
            "  order_uid TEXT NOT NULL DEFAULT '',"
            "  order_number INTEGER NOT NULL DEFAULT 0,"
            "  supplier TEXT NOT NULL DEFAULT '',"
            "  note TEXT NOT NULL DEFAULT '',"
            "  imported_at TEXT NOT NULL DEFAULT (datetime('now'))"
            ");"); }

    // Every newly inserted part needs an identity independent of cloud setup.
    // Preserve supplied UIDs and leave historical blanks for explicit migration.
    if(!q.exec("CREATE UNIQUE INDEX IF NOT EXISTS idx_workspace_part_uid_unique ON work_order_parts(part_uid) WHERE TRIM(part_uid)<>''")
        || !q.exec("CREATE TRIGGER IF NOT EXISTS trg_work_order_parts_part_uid_ai AFTER INSERT ON work_order_parts WHEN TRIM(COALESCE(NEW.part_uid,''))='' BEGIN UPDATE work_order_parts SET part_uid='part-' || lower(hex(randomblob(16))) WHERE id=NEW.id; END")) {
        qWarning()<<"Workspace part identity preparation failed"<<q.lastError().text();
        return false;
    }

    if (!columnsReady || !dependentTables.finish()) {
        qWarning() << "Dependent table preparation failed:" << preparationError;
        return false;
    }
    if (!ensureLocalRegistryMirrorSchema(d) || !ensureWorkspaceOrderStateSchema(d)) {
        qCritical() << "Workspace routing schema preparation failed";
        return false;
    }

    // Seed at least one master for fresh DB
    {
        QSqlQuery m(d);
        m.exec("SELECT COUNT(*) FROM masters");
        if (m.next() && m.value(0).toInt() == 0) {
            QSqlQuery ins(d);
            ins.prepare("INSERT INTO masters(id, name, role, admin_percent, active, active_from, active_to) VALUES(?,?,?,?,?,'','')");
            ins.addBindValue("m1");
            ins.addBindValue(QString::fromUtf8("Майстер 1"));
            ins.addBindValue("master");
            ins.addBindValue(0.0);
            ins.addBindValue(1);
            if (!ins.exec()) {
                qWarning() << "Seed master failed" << ins.lastError().text();
            }
        }
    }

    // Do not auto-seed catalog from work orders here.
    // The explicit Catalog import action performs richer mapping, excludes motor oil,
    // preserves more known fields from orders, and avoids silently creating incomplete cards.

// Indexes (best-effort). Note: LIKE '%term%' cannot fully use indexes, but these still help for exact/prefix filters and joins.
q.exec("CREATE INDEX IF NOT EXISTS idx_work_orders_status_opened ON work_orders(status, opened_at)");
q.exec("CREATE INDEX IF NOT EXISTS idx_work_orders_status_closed ON work_orders(status, closed_at)");
q.exec("CREATE INDEX IF NOT EXISTS idx_work_orders_plate_norm ON work_orders(plate_norm)");
q.exec("CREATE INDEX IF NOT EXISTS idx_work_orders_vin ON work_orders(vin)");
q.exec("CREATE INDEX IF NOT EXISTS idx_work_orders_client ON work_orders(client)");
q.exec("CREATE INDEX IF NOT EXISTS idx_work_orders_car ON work_orders(car)");

q.exec("CREATE INDEX IF NOT EXISTS idx_parts_order_number ON work_order_parts(order_number)");
q.exec("CREATE INDEX IF NOT EXISTS idx_work_order_parts_order_uid_id ON work_order_parts(order_uid, id)");
q.exec("CREATE INDEX IF NOT EXISTS idx_work_order_parts_order_number_year ON work_order_parts(order_number, id)");
q.exec("CREATE INDEX IF NOT EXISTS idx_parts_ordered_at ON work_order_parts(ordered_at)");
q.exec("CREATE INDEX IF NOT EXISTS idx_parts_store ON work_order_parts(store)");
q.exec("CREATE INDEX IF NOT EXISTS idx_parts_part_number ON work_order_parts(part_number)");
q.exec("CREATE INDEX IF NOT EXISTS idx_parts_name ON work_order_parts(name)");

q.exec("CREATE INDEX IF NOT EXISTS idx_work_orders_number ON work_orders(number)");
q.exec("CREATE INDEX IF NOT EXISTS idx_work_orders_order_number ON work_orders(order_number)");
q.exec("CREATE INDEX IF NOT EXISTS idx_work_orders_order_year ON work_orders(order_year)");
q.exec("CREATE INDEX IF NOT EXISTS idx_work_orders_vehicle_year ON work_orders(car_year, order_uid)");
q.exec("CREATE INDEX IF NOT EXISTS idx_work_orders_visible_lookup ON work_orders(order_number, order_year DESC, number DESC)");
q.exec("CREATE INDEX IF NOT EXISTS idx_parts_catalog_brand ON parts_catalog(vehicle_brand)");
q.exec("CREATE INDEX IF NOT EXISTS idx_parts_catalog_model ON parts_catalog(vehicle_model)");
q.exec("CREATE INDEX IF NOT EXISTS idx_parts_catalog_fuel ON parts_catalog(fuel_type)");
q.exec("CREATE INDEX IF NOT EXISTS idx_parts_catalog_group ON parts_catalog(group_name)");
q.exec("CREATE INDEX IF NOT EXISTS idx_parts_catalog_oem ON parts_catalog(part_number)");
q.exec("CREATE INDEX IF NOT EXISTS idx_parts_catalog_name ON parts_catalog(name)");
q.exec("CREATE INDEX IF NOT EXISTS idx_parts_catalog_brand_name ON parts_catalog(brand)");
q.exec("CREATE INDEX IF NOT EXISTS idx_parts_catalog_active ON parts_catalog(is_active)");
q.exec("CREATE UNIQUE INDEX IF NOT EXISTS idx_parts_catalog_vehicle_map_unique ON parts_catalog_vehicle_map(catalog_id, vehicle_brand, vehicle_model, year_from, year_to, fuel_type, engine_type, body_type)");
q.exec("CREATE UNIQUE INDEX IF NOT EXISTS idx_parts_catalog_sources_unique ON parts_catalog_sources(catalog_id, source_part_id)");
q.exec("CREATE INDEX IF NOT EXISTS idx_outsource_jobs_order_number ON outsource_jobs(order_number)");
q.exec("CREATE INDEX IF NOT EXISTS idx_outsource_jobs_service_id ON outsource_jobs(service_id)");
q.exec("CREATE INDEX IF NOT EXISTS idx_outsource_jobs_sent_date ON outsource_jobs(sent_date)");
q.exec("CREATE INDEX IF NOT EXISTS idx_work_orders_opened_at ON work_orders(opened_at)");
q.exec("CREATE INDEX IF NOT EXISTS idx_work_orders_closed_at ON work_orders(closed_at)");
q.exec("CREATE INDEX IF NOT EXISTS idx_work_orders_master_id ON work_orders(master_id)");
q.exec("CREATE INDEX IF NOT EXISTS idx_work_orders_paid_combo ON work_orders(status, plate_norm, opened_at)");
q.exec("CREATE INDEX IF NOT EXISTS idx_parts_paid ON work_order_parts(paid)");
q.exec("CREATE INDEX IF NOT EXISTS idx_services_order_number ON work_order_services(order_number)");
q.exec("CREATE INDEX IF NOT EXISTS idx_work_order_services_order_uid_id ON work_order_services(order_uid, id)");
q.exec("CREATE INDEX IF NOT EXISTS idx_work_order_services_order_number_id ON work_order_services(order_number, id)");
q.exec("CREATE INDEX IF NOT EXISTS idx_service_masters_master_id ON service_masters(master_id)");
q.exec("CREATE INDEX IF NOT EXISTS idx_outsource_order_number ON outsource_repairs(order_number)");
q.exec("CREATE INDEX IF NOT EXISTS idx_outsource_sent_at ON outsource_repairs(sent_at)");


QString contractError;
if (!DbSchemaAuditSupport::ensureContractMeta(d, &contractError)) {
    qWarning() << "db_contract_meta ensure failed:" << contractError;
}
if (!DbSchemaAuditSupport::applySupplementalIndexes(d, &contractError)) {
    qWarning() << "Supplemental DB index ensure failed:" << contractError;
}

// FTS5 (best-effort). If the SQLite build lacks FTS5, we'll continue without it.
auto tableExists = [&](const QString& name) -> bool {
    QSqlQuery tq(d);
    tq.prepare("SELECT 1 FROM sqlite_master WHERE type IN ('table','view') AND name=? LIMIT 1");
    tq.addBindValue(name);
    if (!tq.exec()) return false;
    return tq.next();
};

if (!tableExists("work_orders_fts")) {
    if (!q.exec("CREATE VIRTUAL TABLE work_orders_fts USING fts5("
                "plate, car, vin, client, "
                "content='work_orders', content_rowid='number' )")) {
        qWarning() << "FTS create failed (work_orders_fts):" << q.lastError().text();
    } else {
        // initial build
        q.exec("INSERT INTO work_orders_fts(work_orders_fts) VALUES('rebuild')");
        // triggers
        q.exec("CREATE TRIGGER work_orders_ai AFTER INSERT ON work_orders BEGIN "
               "  INSERT INTO work_orders_fts(rowid, plate, car, vin, client) "
               "  VALUES (new.number, new.plate, new.car, new.vin, new.client); "
               "END;");
        q.exec("CREATE TRIGGER work_orders_ad AFTER DELETE ON work_orders BEGIN "
               "  INSERT INTO work_orders_fts(work_orders_fts, rowid, plate, car, vin, client) "
               "  VALUES('delete', old.number, old.plate, old.car, old.vin, old.client); "
               "END;");
        q.exec("CREATE TRIGGER work_orders_au AFTER UPDATE ON work_orders BEGIN "
               "  INSERT INTO work_orders_fts(work_orders_fts, rowid, plate, car, vin, client) "
               "  VALUES('delete', old.number, old.plate, old.car, old.vin, old.client); "
               "  INSERT INTO work_orders_fts(rowid, plate, car, vin, client) "
               "  VALUES (new.number, new.plate, new.car, new.vin, new.client); "
               "END;");
    }
}

if (!tableExists("work_order_parts_fts")) {
    if (!q.exec("CREATE VIRTUAL TABLE work_order_parts_fts USING fts5("
                "name, part_number, store, "
                "content='work_order_parts', content_rowid='id' )")) {
        qWarning() << "FTS create failed (work_order_parts_fts):" << q.lastError().text();
    } else {
        q.exec("INSERT INTO work_order_parts_fts(work_order_parts_fts) VALUES('rebuild')");
        q.exec("CREATE TRIGGER work_order_parts_ai AFTER INSERT ON work_order_parts BEGIN "
               "  INSERT INTO work_order_parts_fts(rowid, name, part_number, store) "
               "  VALUES (new.id, new.name, new.part_number, new.store); "
               "END;");
        q.exec("CREATE TRIGGER work_order_parts_ad AFTER DELETE ON work_order_parts BEGIN "
               "  INSERT INTO work_order_parts_fts(work_order_parts_fts, rowid, name, part_number, store) "
               "  VALUES('delete', old.id, old.name, old.part_number, old.store); "
               "END;");
        q.exec("CREATE TRIGGER work_order_parts_au AFTER UPDATE ON work_order_parts BEGIN "
               "  INSERT INTO work_order_parts_fts(work_order_parts_fts, rowid, name, part_number, store) "
               "  VALUES('delete', old.id, old.name, old.part_number, old.store); "
               "  INSERT INTO work_order_parts_fts(rowid, name, part_number, store) "
               "  VALUES (new.id, new.name, new.part_number, new.store); "
               "END;");
    }
}

if (RuntimeLoggingControl::deepDebugEnabled()) qInfo() << "DB schema OK";
    return true;
}
