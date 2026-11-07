// Extracted from src/infrastructure/db/sqlite/DbArchiveRoutingStageSupport.cpp for P1/P2 monolith reduction.
// Fragment boundary is function-safe; do not split inside macro calls, raw strings or SQL literals.

{
    const QString activePath = AppRuntime::activeDbFilePath();
    const QString legacyPath = AppRuntime::legacyDbFilePath();
    const QString archivePath = AppRuntime::archiveDbFilePath();

    if (QFileInfo::exists(legacyPath) && QFileInfo(activePath).absoluteFilePath() != QFileInfo(legacyPath).absoluteFilePath() && !QFileInfo::exists(archivePath)) {
        QDir().mkpath(QFileInfo(archivePath).absolutePath());
        if (!QFile::copy(legacyPath, archivePath)) {
            qWarning() << "Legacy archive copy failed:" << legacyPath << "->" << archivePath;
        }
    }

    if (!QFileInfo::exists(archivePath)) {
        return upsertActiveOrdersIntoRegistry();
    }

    if (!ensureRegistrySchemaFile()) return false;

    QSqlDatabase d = Database::db();
    QSqlQuery q(d);
    const QString escapedArchive = archivePath;
    q.exec("DETACH DATABASE legacy");
    if (!q.exec(QStringLiteral("ATTACH DATABASE %1 AS legacy").arg(sqlQuoteText(escapedArchive)))) {
        qWarning() << "Attach legacy archive failed:" << q.lastError().text();
        return upsertActiveOrdersIntoRegistry();
    }

    auto detachLegacy = [&]() { QSqlQuery dq(d); dq.exec("DETACH DATABASE legacy"); };

    const QSet<QString> activeMasters = tableColumnsForDb(d, QStringLiteral("masters"));
    const QSet<QString> activeOrders = tableColumnsForDb(d, QStringLiteral("work_orders"));
    const QSet<QString> activeServices = tableColumnsForDb(d, QStringLiteral("work_order_services"));
    const QSet<QString> activeServiceMasters = tableColumnsForDb(d, QStringLiteral("service_masters"));
    const QSet<QString> activeParts = tableColumnsForDb(d, QStringLiteral("work_order_parts"));
    const QSet<QString> activeSettings = tableColumnsForDb(d, QStringLiteral("settings"));
    const QSet<QString> legacyMasters = tableExistsInDb(d, QStringLiteral("masters"), QStringLiteral("legacy")) ? tableColumnsForDb(d, QStringLiteral("masters"), QStringLiteral("legacy")) : QSet<QString>();
    const QSet<QString> legacyOrders = tableExistsInDb(d, QStringLiteral("work_orders"), QStringLiteral("legacy")) ? tableColumnsForDb(d, QStringLiteral("work_orders"), QStringLiteral("legacy")) : QSet<QString>();
    const QSet<QString> legacyServices = tableExistsInDb(d, QStringLiteral("work_order_services"), QStringLiteral("legacy")) ? tableColumnsForDb(d, QStringLiteral("work_order_services"), QStringLiteral("legacy")) : QSet<QString>();
    const QSet<QString> legacyServiceMasters = tableExistsInDb(d, QStringLiteral("service_masters"), QStringLiteral("legacy")) ? tableColumnsForDb(d, QStringLiteral("service_masters"), QStringLiteral("legacy")) : QSet<QString>();
    const QSet<QString> legacyParts = tableExistsInDb(d, QStringLiteral("work_order_parts"), QStringLiteral("legacy")) ? tableColumnsForDb(d, QStringLiteral("work_order_parts"), QStringLiteral("legacy")) : QSet<QString>();
    const QSet<QString> legacySettings = tableExistsInDb(d, QStringLiteral("settings"), QStringLiteral("legacy")) ? tableColumnsForDb(d, QStringLiteral("settings"), QStringLiteral("legacy")) : QSet<QString>();

    QSqlQuery tx(d);
    tx.exec("BEGIN IMMEDIATE TRANSACTION");

    if (!legacyMasters.isEmpty()) {
        QStringList insertCols, selectExprs;
        auto add = [&](const QString& dst, const QString& expr) { if (activeMasters.contains(dst)) { insertCols << sqlQuoteIdent(dst); selectExprs << expr; } };
        add("id", firstAvailableExpr(legacyMasters, {"id"}, "''"));
        add("name", firstAvailableExpr(legacyMasters, {"name"}, "''"));
        add("role", firstAvailableExpr(legacyMasters, {"role"}, "'master'"));
        add("admin_percent", firstAvailableExpr(legacyMasters, {"admin_percent"}, "0"));
        add("active", firstAvailableExpr(legacyMasters, {"active"}, "1"));
        if (!insertCols.isEmpty()) {
            const QString sql = QStringLiteral("INSERT OR IGNORE INTO masters(%1) SELECT %2 FROM legacy.masters").arg(insertCols.join(','), selectExprs.join(','));
            if (!q.exec(sql)) qWarning() << "Legacy masters import failed:" << q.lastError().text();
        }
    }

    if (!legacyOrders.isEmpty()) {
        QStringList insertCols, selectExprs;
        auto add = [&](const QString& dst, const QString& expr) { if (activeOrders.contains(dst)) { insertCols << sqlQuoteIdent(dst); selectExprs << expr; } };
        const QString numberExpr = firstAvailableExpr(legacyOrders, {"number"}, "0");
        const QString colorExpr = firstAvailableExpr(legacyOrders, {"color", "car_color"}, "''");
        const QString fallbackUidExpr = QStringLiteral("('legacy_' || CAST((%1) AS TEXT))").arg(numberExpr);
        const QString uidExpr = legacyOrders.contains(QStringLiteral("order_uid"))
            ? QStringLiteral("COALESCE(NULLIF(order_uid,''), %1)").arg(fallbackUidExpr)
            : fallbackUidExpr;
        const QString visibleOrderExpr = legacyOrders.contains(QStringLiteral("order_number"))
            ? QStringLiteral("CASE WHEN COALESCE(order_number,0)>0 THEN order_number ELSE %1 END").arg(numberExpr)
            : numberExpr;
        const QString yearExpr = legacyOrders.contains(QStringLiteral("order_year"))
            ? QStringLiteral("CASE WHEN COALESCE(order_year,0)>0 THEN order_year ELSE CASE WHEN substr(TRIM(COALESCE(opened_at,'')),5,1)='-' AND substr(TRIM(COALESCE(opened_at,'')),8,1)='-' THEN CAST(substr(TRIM(COALESCE(opened_at,'')),1,4) AS INTEGER) WHEN substr(TRIM(COALESCE(opened_at,'')),3,1)='.' AND substr(TRIM(COALESCE(opened_at,'')),6,1)='.' AND length(TRIM(COALESCE(opened_at,'')))>=8 THEN 2000 + CAST(substr(TRIM(COALESCE(opened_at,'')),7,2) AS INTEGER) ELSE CAST(strftime('%Y','now') AS INTEGER) END END")
            : QStringLiteral("CASE WHEN substr(TRIM(COALESCE(opened_at,'')),5,1)='-' AND substr(TRIM(COALESCE(opened_at,'')),8,1)='-' THEN CAST(substr(TRIM(COALESCE(opened_at,'')),1,4) AS INTEGER) WHEN substr(TRIM(COALESCE(opened_at,'')),3,1)='.' AND substr(TRIM(COALESCE(opened_at,'')),6,1)='.' AND length(TRIM(COALESCE(opened_at,'')))>=8 THEN 2000 + CAST(substr(TRIM(COALESCE(opened_at,'')),7,2) AS INTEGER) ELSE CAST(strftime('%Y','now') AS INTEGER) END");
        add("number", numberExpr);
        add("order_uid", uidExpr);
        add("order_number", visibleOrderExpr);
        add("order_year", yearExpr);
        add("opened_at", firstAvailableExpr(legacyOrders, {"opened_at"}, "datetime('now')"));
        add("closed_at", firstAvailableExpr(legacyOrders, {"closed_at"}, "NULL"));
        add("status", firstAvailableExpr(legacyOrders, {"status"}, "'open'"));
        add("plate", firstAvailableExpr(legacyOrders, {"plate"}, "''"));
        add("plate_norm", firstAvailableExpr(legacyOrders, {"plate_norm", "plate"}, "''"));
        add("vin", firstAvailableExpr(legacyOrders, {"vin"}, "''"));
        add("car_year", firstAvailableExpr(legacyOrders, {"car_year"}, "0"));
        add("odometer", firstAvailableExpr(legacyOrders, {"odometer"}, "0"));
        add("engine_type", firstAvailableExpr(legacyOrders, {"engine_type"}, "''"));
        add("engine_volume", firstAvailableExpr(legacyOrders, {"engine_volume"}, "''"));
        add("fuel_type", firstAvailableExpr(legacyOrders, {"fuel_type"}, "''"));
        add("car", firstAvailableExpr(legacyOrders, {"car"}, "''"));
        add("client", firstAvailableExpr(legacyOrders, {"client", "owner_name"}, "''"));
        add("client_phone", firstAvailableExpr(legacyOrders, {"client_phone"}, "''"));
        add("master_id", firstAvailableExpr(legacyOrders, {"master_id"}, "(SELECT id FROM masters ORDER BY id LIMIT 1)"));
        add("works_sum", firstAvailableExpr(legacyOrders, {"works_sum"}, "0"));
        add("parts_sum", firstAvailableExpr(legacyOrders, {"parts_sum"}, "0"));
        add("discount_mode", firstAvailableExpr(legacyOrders, {"discount_mode"}, "'legacy'"));
        add("discount", firstAvailableExpr(legacyOrders, {"discount"}, "0"));
        add("master_name_closed", firstAvailableExpr(legacyOrders, {"master_name_closed"}, "''"));
        add("car_model", firstAvailableExpr(legacyOrders, {"car_model"}, "''"));
        add("car_brand", firstAvailableExpr(legacyOrders, {"car_brand"}, "''"));
        add("car_kind", firstAvailableExpr(legacyOrders, {"car_kind"}, "''"));
        add("color", colorExpr);
        add("car_color", firstAvailableExpr(legacyOrders, {"car_color", "color"}, colorExpr));
        add("owner_name", firstAvailableExpr(legacyOrders, {"owner_name", "client"}, "''"));
        if (!insertCols.isEmpty()) {
            const QString sql = QStringLiteral("INSERT OR IGNORE INTO work_orders(%1) SELECT %2 FROM legacy.work_orders WHERE %3 > 0").arg(insertCols.join(','), selectExprs.join(','), numberExpr);
            if (!q.exec(sql)) qWarning() << "Legacy work_orders import failed:" << q.lastError().text();
        }
    }

    if (!legacyServices.isEmpty()) {
        QStringList insertCols, selectExprs;
        auto add = [&](const QString& dst, const QString& expr) { if (activeServices.contains(dst)) { insertCols << sqlQuoteIdent(dst); selectExprs << expr; } };
        add("id", firstAvailableExpr(legacyServices, {"id"}, "NULL"));
        add("order_number", firstAvailableExpr(legacyServices, {"order_number"}, "0"));
        add("title", firstAvailableExpr(legacyServices, {"title", "name"}, "''"));
        add("qty", firstAvailableExpr(legacyServices, {"qty"}, "1"));
        add("price", firstAvailableExpr(legacyServices, {"price"}, "0"));
        add("amount", firstAvailableExpr(legacyServices, {"amount"}, "COALESCE(qty,1)*COALESCE(price,0)"));
        if (!insertCols.isEmpty()) {
            const QString sql = QStringLiteral("INSERT OR IGNORE INTO work_order_services(%1) SELECT %2 FROM legacy.work_order_services WHERE %3 IN (SELECT number FROM work_orders)").arg(insertCols.join(','), selectExprs.join(','), firstAvailableExpr(legacyServices, {"order_number"}, "0"));
            if (!q.exec(sql)) qWarning() << "Legacy services import failed:" << q.lastError().text();
        }
    }

    if (!legacyServiceMasters.isEmpty()) {
        QStringList insertCols, selectExprs;
        auto add = [&](const QString& dst, const QString& expr) { if (activeServiceMasters.contains(dst)) { insertCols << sqlQuoteIdent(dst); selectExprs << expr; } };
        add("service_id", firstAvailableExpr(legacyServiceMasters, {"service_id"}, "0"));
        add("master_id", firstAvailableExpr(legacyServiceMasters, {"master_id"}, "''"));
        add("share_percent", firstAvailableExpr(legacyServiceMasters, {"share_percent"}, "100"));
        add("role", firstAvailableExpr(legacyServiceMasters, {"role"}, "'master'"));
        add("master_name_closed", firstAvailableExpr(legacyServiceMasters, {"master_name_closed"}, "''"));
        if (!insertCols.isEmpty()) {
            const QString sql = QStringLiteral("INSERT OR IGNORE INTO service_masters(%1) SELECT %2 FROM legacy.service_masters").arg(insertCols.join(','), selectExprs.join(','));
            if (!q.exec(sql)) qWarning() << "Legacy service_masters import failed:" << q.lastError().text();
        }
    }

    if (!legacyParts.isEmpty()) {
        QStringList insertCols, selectExprs;
        auto add = [&](const QString& dst, const QString& expr) { if (activeParts.contains(dst)) { insertCols << sqlQuoteIdent(dst); selectExprs << expr; } };
        const QString orderExpr = firstAvailableExpr(legacyParts, {"order_number", "work_order", "work_order_id", "work_order_number"}, "0");
        const QString orderedAtExpr = firstAvailableExpr(legacyParts, {"ordered_at", "date", "created_at"}, "''");
        const QString deliveryDateExpr = firstAvailableExpr(legacyParts, {"delivery_date"}, "''");
        const QString qtyExpr = firstAvailableExpr(legacyParts, {"qty"}, "1");
        const QString priceExpr = firstAvailableExpr(legacyParts, {"price", "purchase_price"}, "0");
        const QString amountExpr = firstAvailableExpr(legacyParts, {"amount", "purchase_amount"}, QStringLiteral("(COALESCE(%1,1) * COALESCE(%2,0))").arg(qtyExpr, priceExpr));
        add("id", firstAvailableExpr(legacyParts, {"id"}, "NULL"));
        add("order_number", orderExpr);
        add("ordered_at", orderedAtExpr);
        add("delivery_date", deliveryDateExpr);
        add("store", firstAvailableExpr(legacyParts, {"store"}, "''"));
        add("part_number", firstAvailableExpr(legacyParts, {"part_number"}, "''"));
        add("name", firstAvailableExpr(legacyParts, {"name"}, "''"));
        add("note", firstAvailableExpr(legacyParts, {"note"}, "''"));
        add("qty", qtyExpr);
        add("price", priceExpr);
        add("amount", amountExpr);
        add("service_id", firstAvailableExpr(legacyParts, {"service_id"}, "0"));
        add("ordered_by_admin", firstAvailableExpr(legacyParts, {"ordered_by_admin"}, "1"));
        add("paid", firstAvailableExpr(legacyParts, {"paid"}, "0"));
        add("purchase_price", firstAvailableExpr(legacyParts, {"purchase_price", "price"}, priceExpr));
        add("purchase_amount", firstAvailableExpr(legacyParts, {"purchase_amount", "amount"}, amountExpr));
        add("markup_percent", firstAvailableExpr(legacyParts, {"markup_percent"}, "0"));
        add("work_order", firstAvailableExpr(legacyParts, {"work_order", "order_number", "work_order_id", "work_order_number"}, orderExpr));
        add("work_order_id", firstAvailableExpr(legacyParts, {"work_order_id", "order_number", "work_order", "work_order_number"}, orderExpr));
        add("work_order_number", firstAvailableExpr(legacyParts, {"work_order_number", "order_number", "work_order", "work_order_id"}, orderExpr));
        add("date", firstAvailableExpr(legacyParts, {"date", "ordered_at", "created_at"}, orderedAtExpr));
        add("created_at", firstAvailableExpr(legacyParts, {"created_at", "ordered_at", "date"}, orderedAtExpr));
        if (!insertCols.isEmpty()) {
            const QString sql = QStringLiteral("INSERT OR IGNORE INTO work_order_parts(%1) SELECT %2 FROM legacy.work_order_parts WHERE %3 IN (SELECT number FROM work_orders)").arg(insertCols.join(','), selectExprs.join(','), orderExpr);
            if (!q.exec(sql)) qWarning() << "Legacy parts import failed:" << q.lastError().text();
        }
    }

    if (!legacySettings.isEmpty() && activeSettings.contains("key") && activeSettings.contains("value")) {
        if (!q.exec(QStringLiteral("INSERT OR IGNORE INTO settings(key, value) SELECT key, value FROM legacy.settings"))) {
            qWarning() << "Legacy settings import failed:" << q.lastError().text();
        }
    }

    if (activeParts.contains("work_order")) q.exec("UPDATE work_order_parts SET work_order=order_number WHERE COALESCE(work_order,0)=0");
    if (activeParts.contains("work_order_id")) q.exec("UPDATE work_order_parts SET work_order_id=order_number WHERE COALESCE(work_order_id,0)=0");
    if (activeParts.contains("work_order_number")) q.exec("UPDATE work_order_parts SET work_order_number=order_number WHERE COALESCE(work_order_number,0)=0");
    if (activeParts.contains("date")) q.exec("UPDATE work_order_parts SET date=COALESCE(NULLIF(date,''), COALESCE(ordered_at,''), '') WHERE COALESCE(date,'')=''");
    if (activeParts.contains("created_at")) q.exec("UPDATE work_order_parts SET created_at=COALESCE(NULLIF(created_at,''), COALESCE(ordered_at,''), '') WHERE COALESCE(created_at,'')=''");
    q.exec(
        "INSERT OR IGNORE INTO service_masters(service_id, master_id, share_percent, role, master_name_closed) "
        "SELECT s.id, wo.master_id, 100.0, 'master', COALESCE(wo.master_name_closed, m.name, '') "
        "FROM work_order_services s "
        "JOIN work_orders wo ON wo.number = s.order_number "
        "LEFT JOIN masters m ON m.id = wo.master_id "
        "WHERE wo.master_id IS NOT NULL AND wo.master_id <> '' "
        "AND NOT EXISTS (SELECT 1 FROM service_masters sm WHERE sm.service_id = s.id)");

    tx.exec("COMMIT");
    detachLegacy();
    return upsertActiveOrdersIntoRegistry();
}

bool importSingleArchivedOrderToActiveFromPath(const QString& archivePath, int orderNumber, QString* errorText)
{
    if (!QFileInfo::exists(archivePath)) {
        if (errorText) *errorText = QStringLiteral("Archive DB not found: %1").arg(archivePath);
        return false;
    }

    QSqlDatabase d = Database::db();
    QSqlQuery q(d);
    q.exec("DETACH DATABASE legacy_single");
    if (!q.exec(QStringLiteral("ATTACH DATABASE %1 AS legacy_single").arg(sqlQuoteText(archivePath)))) {
        if (errorText) *errorText = q.lastError().text();
        return false;
    }

    auto detachLegacy = [&]() {
        QSqlQuery dq(d);
        dq.exec("DETACH DATABASE legacy_single");
    };

    const QSet<QString> legacyOrders = tableExistsInDb(d, QStringLiteral("work_orders"), QStringLiteral("legacy_single")) ? tableColumnsForDb(d, QStringLiteral("work_orders"), QStringLiteral("legacy_single")) : QSet<QString>();
    const QSet<QString> legacyServices = tableExistsInDb(d, QStringLiteral("work_order_services"), QStringLiteral("legacy_single")) ? tableColumnsForDb(d, QStringLiteral("work_order_services"), QStringLiteral("legacy_single")) : QSet<QString>();
    const QSet<QString> legacyServiceMasters = tableExistsInDb(d, QStringLiteral("service_masters"), QStringLiteral("legacy_single")) ? tableColumnsForDb(d, QStringLiteral("service_masters"), QStringLiteral("legacy_single")) : QSet<QString>();
    const QSet<QString> legacyParts = tableExistsInDb(d, QStringLiteral("work_order_parts"), QStringLiteral("legacy_single")) ? tableColumnsForDb(d, QStringLiteral("work_order_parts"), QStringLiteral("legacy_single")) : QSet<QString>();
    const QSet<QString> legacyOutsource = tableExistsInDb(d, QStringLiteral("outsource_repairs"), QStringLiteral("legacy_single")) ? tableColumnsForDb(d, QStringLiteral("outsource_repairs"), QStringLiteral("legacy_single")) : QSet<QString>();
    const QSet<QString> legacyMasters = tableExistsInDb(d, QStringLiteral("masters"), QStringLiteral("legacy_single")) ? tableColumnsForDb(d, QStringLiteral("masters"), QStringLiteral("legacy_single")) : QSet<QString>();

    if (legacyOrders.isEmpty()) {
        detachLegacy();
        if (errorText) *errorText = QStringLiteral("work_orders table not found in archive DB");
        return false;
    }

    // Validate source identities before any destructive target operation.
    auto validateSourceParts = [&]() -> bool {
        if (!legacyOrders.contains(QStringLiteral("order_uid"))
            || !legacyOrders.contains(QStringLiteral("order_year"))
            || !legacyOrders.contains(QStringLiteral("order_number"))) {
            if (errorText) *errorText = QStringLiteral("Archive canonical order identity is unavailable");
            return false;
        }
        QSqlQuery root(d);
        root.prepare(QStringLiteral("SELECT order_uid,order_year,order_number FROM legacy_single.work_orders WHERE number=?"));
        root.addBindValue(orderNumber);
        if (!root.exec() || !root.next()) {
            if (errorText) *errorText = QStringLiteral("Archive order identity cannot be read: %1").arg(root.lastError().text());
            return false;
        }
        const QString sourceUid = root.value(0).toString();
        const int sourceYear = root.value(1).toInt();
        const int sourceNumber = root.value(2).toInt();
        if (!OrderDeletionPublication::checkOpenAllowed(d, orderNumber, sourceUid, errorText)) return false;
        if (orderNumber <= 0 || sourceYear <= 0 || sourceNumber <= 0
            || sourceUid.trimmed().isEmpty() || sourceUid != sourceUid.trimmed()) {
            if (errorText) *errorText = QStringLiteral("Archive order UID is invalid");
            return false;
        }
        QSqlQuery target(d);
        target.prepare(QStringLiteral(
            "SELECT number,order_uid,order_year,order_number FROM main.work_orders "
            "WHERE number=? OR order_uid=? OR (order_year=? AND order_number=?)"));
        target.addBindValue(orderNumber);
        target.addBindValue(sourceUid);
        target.addBindValue(sourceYear);
        target.addBindValue(sourceNumber);
        if (!target.exec()) {
            if (errorText) *errorText = target.lastError().text();
            return false;
        }
        while (target.next()) {
            if (target.value(0).toInt() != orderNumber || target.value(1).toString() != sourceUid
                || target.value(2).toInt() != sourceYear || target.value(3).toInt() != sourceNumber) {
                if (errorText) *errorText = QStringLiteral("Archive identity conflicts with an existing working order");
                return false;
            }
        }
        if (target.lastError().isValid()) {
            if (errorText) *errorText = target.lastError().text();
            return false;
        }
        if (legacyParts.isEmpty()) return true;
        if (!legacyParts.contains(QStringLiteral("part_uid"))
            || !legacyParts.contains(QStringLiteral("order_uid"))
            || !legacyParts.contains(QStringLiteral("order_number"))) {
            if (errorText) *errorText = QStringLiteral("Archive part identity columns are unavailable");
            return false;
        }
        QSet<QString> seen;
        QSqlQuery parts(d);
        parts.prepare(QStringLiteral("SELECT part_uid,order_uid,order_number FROM legacy_single.work_order_parts WHERE order_uid=? OR order_number=?"));
        parts.addBindValue(sourceUid);
        parts.addBindValue(orderNumber);
        if (!parts.exec()) {
            if (errorText) *errorText = parts.lastError().text();
            return false;
        }
        while (parts.next()) {
            const QString uid = parts.value(0).toString();
            if (uid.trimmed().isEmpty() || uid != uid.trimmed() || seen.contains(uid)
                || parts.value(1).toString() != sourceUid || parts.value(2).toInt() != orderNumber) {
                if (errorText) *errorText = QStringLiteral("Archive part UID or parent identity is missing or conflicting");
                return false;
            }
            seen.insert(uid);
        }
        if (parts.lastError().isValid()) {
            if (errorText) *errorText = parts.lastError().text();
            return false;
        }
        return true;
    };
    if (!validateSourceParts()) {
        detachLegacy();
        return false;
    }

    MutationExecutor::Request request;
    request.sourceModule = QStringLiteral("DbArchiveRoutingSupport::importSingleArchivedOrderToActiveFromPath");
    request.entityType = QStringLiteral("order");
    request.entityId = QString::number(orderNumber);
    request.operation = QStringLiteral("import_archived_order_to_active");
    request.changedTables = {QStringLiteral("work_orders"), QStringLiteral("work_order_services"), QStringLiteral("service_masters"),
                             QStringLiteral("work_order_parts"), QStringLiteral("outsource_repairs"), QStringLiteral("outsource_jobs"),
                             QStringLiteral("workspace_order_state"), QStringLiteral("order_registry"),
                             QStringLiteral("work_order_part_attachments"), QStringLiteral("work_order_service_attachments"),
                             QStringLiteral("router_cache_service_masters"), QStringLiteral("router_cache_outsource_repairs"),
                             QStringLiteral("router_cache_work_order_parts"), QStringLiteral("router_cache_work_order_services"),
                             QStringLiteral("router_cache_work_orders")};
    request.beginErrorFallback = QStringLiteral("Archived order import transaction begin failed");
    request.commitErrorFallback = QStringLiteral("Archived order import transaction commit failed");

    const auto tx = MutationExecutor::execute(d, request, [&](QSqlDatabase& txDb, QString* txError) {
        QSqlDatabase d = txDb;
        auto fail = [&](const QString& msg) {
            if (txError) *txError = msg;
            return false;
        };
        if (!purgeOrderGraphRowsInOwnedTransaction(d, orderNumber, txError, false)) return false;
        for (const QString& table : {QStringLiteral("order_registry"), QStringLiteral("workspace_order_state")}) {
            QSqlQuery remove(d);
            remove.prepare(QStringLiteral("DELETE FROM %1 WHERE order_number=?").arg(table));
            remove.addBindValue(orderNumber);
            if (!remove.exec()) return fail(remove.lastError().text());
            QSqlQuery verify(d);
            verify.prepare(QStringLiteral("SELECT 1 FROM %1 WHERE order_number=? LIMIT 1").arg(table));
            verify.addBindValue(orderNumber);
            if (!verify.exec()) return fail(verify.lastError().text());
            if (verify.next()) return fail(QStringLiteral("Order projection removal did not persist: %1").arg(table));
            if (verify.lastError().isValid()) return fail(verify.lastError().text());
        }

    auto ensureMasterExists = [&](const QString& rawMasterId, const QString& fallbackName) -> QString {
        QString candidate = rawMasterId.trimmed();
        auto existsMaster = [&](const QString& id) -> bool {
            if (id.trimmed().isEmpty()) return false;
            QSqlQuery chk(d);
            chk.prepare("SELECT 1 FROM masters WHERE id=? LIMIT 1");
            chk.addBindValue(id);
            return chk.exec() && chk.next();
        };

        if (existsMaster(candidate)) return candidate;

        if (!candidate.isEmpty() && !legacyMasters.isEmpty() && legacyMasters.contains(QStringLiteral("id"))) {
            QSqlQuery importMaster(d);
            importMaster.prepare(
                QStringLiteral(
                    "INSERT OR IGNORE INTO masters(id, name, role, admin_percent, active, active_from, active_to) "
                    "SELECT COALESCE(id,''), COALESCE(%1,''), COALESCE(%2,'master'), COALESCE(%3,0), COALESCE(%4,1), COALESCE(%5,''), COALESCE(%6,'') "
                    "FROM legacy_single.masters WHERE id=?")
                    .arg(firstAvailableExpr(legacyMasters, {"name"}, "''"),
                         firstAvailableExpr(legacyMasters, {"role"}, "'master'"),
                         firstAvailableExpr(legacyMasters, {"admin_percent"}, "0"),
                         firstAvailableExpr(legacyMasters, {"active"}, "1"),
                         firstAvailableExpr(legacyMasters, {"active_from"}, "''"),
                         firstAvailableExpr(legacyMasters, {"active_to"}, "''")));
            importMaster.addBindValue(candidate);
            importMaster.exec();
            if (existsMaster(candidate)) return candidate;
        }

        QString displayName = fallbackName.trimmed();
        if (displayName.isEmpty()) displayName = QString::fromUtf8("Архівний майстер");
        QString syntheticId = candidate;
        if (syntheticId.isEmpty()) {
            syntheticId = QStringLiteral("__archive__%1").arg(QString::number(static_cast<qulonglong>(qHash(displayName))));
        }
        if (!syntheticId.startsWith(QStringLiteral("__archive__"))) {
            syntheticId = QStringLiteral("__archive__%1").arg(syntheticId);
        }
        QSqlQuery ensureFallback(d);
        ensureFallback.prepare("INSERT OR IGNORE INTO masters(id, name, role, admin_percent, active, active_from, active_to) VALUES(?, ?, 'master', 0, 1, '', '')");
        ensureFallback.addBindValue(syntheticId);
        ensureFallback.addBindValue(displayName);
        if (!ensureFallback.exec()) {
            return QString();
        }
        return syntheticId;
    };

    const QString numberExpr = firstAvailableExpr(legacyOrders, {"number"}, "0");
    const QString colorExpr = firstAvailableExpr(legacyOrders, {"color", "car_color"}, "''");
    const QString fallbackUidExpr = QStringLiteral("('legacy_single_' || CAST((%1) AS TEXT))").arg(numberExpr);
    const QString uidExpr = legacyOrders.contains(QStringLiteral("order_uid"))
        ? QStringLiteral("COALESCE(NULLIF(order_uid,''), %1)").arg(fallbackUidExpr)
        : fallbackUidExpr;
    const QString visibleOrderExpr = legacyOrders.contains(QStringLiteral("order_number"))
        ? QStringLiteral("CASE WHEN COALESCE(order_number,0)>0 THEN order_number ELSE %1 END").arg(numberExpr)
        : numberExpr;
    const QString yearExpr = legacyOrders.contains(QStringLiteral("order_year"))
        ? QStringLiteral("CASE WHEN COALESCE(order_year,0)>0 THEN order_year ELSE CASE WHEN substr(TRIM(COALESCE(opened_at,'')),5,1)='-' AND substr(TRIM(COALESCE(opened_at,'')),8,1)='-' THEN CAST(substr(TRIM(COALESCE(opened_at,'')),1,4) AS INTEGER) WHEN substr(TRIM(COALESCE(opened_at,'')),3,1)='.' AND substr(TRIM(COALESCE(opened_at,'')),6,1)='.' AND length(TRIM(COALESCE(opened_at,'')))>=8 THEN 2000 + CAST(substr(TRIM(COALESCE(opened_at,'')),7,2) AS INTEGER) ELSE CAST(strftime('%Y','now') AS INTEGER) END END")
        : QStringLiteral("CASE WHEN substr(TRIM(COALESCE(opened_at,'')),5,1)='-' AND substr(TRIM(COALESCE(opened_at,'')),8,1)='-' THEN CAST(substr(TRIM(COALESCE(opened_at,'')),1,4) AS INTEGER) WHEN substr(TRIM(COALESCE(opened_at,'')),3,1)='.' AND substr(TRIM(COALESCE(opened_at,'')),6,1)='.' AND length(TRIM(COALESCE(opened_at,'')))>=8 THEN 2000 + CAST(substr(TRIM(COALESCE(opened_at,'')),7,2) AS INTEGER) ELSE CAST(strftime('%Y','now') AS INTEGER) END");

    QSqlQuery readOrder(d);
    readOrder.prepare(
        QStringLiteral(
            "SELECT %1 AS number, %2 AS order_uid, %3 AS order_number, %4 AS order_year, "
            "%5 AS opened_at, %6 AS closed_at, %7 AS status, %8 AS plate, %9 AS plate_norm, %10 AS vin, %11 AS car_year, "
            "%12 AS odometer, %13 AS engine_type, %14 AS engine_volume, %15 AS fuel_type, %16 AS car, %17 AS client, "
            "%18 AS client_phone, %19 AS master_id, %20 AS works_sum, %21 AS parts_sum, %22 AS discount, %23 AS master_name_closed, "
            "%24 AS car_model, %25 AS car_brand, %26 AS car_kind, %27 AS color, %28 AS car_color, %29 AS owner_name, %30 AS discount_mode "
            "FROM legacy_single.work_orders WHERE %1=? LIMIT 1")
            .arg(numberExpr,
                 uidExpr,
                 visibleOrderExpr,
                 yearExpr,
                 firstAvailableExpr(legacyOrders, {"opened_at"}, "datetime('now')"),
                 firstAvailableExpr(legacyOrders, {"closed_at"}, "NULL"),
                 firstAvailableExpr(legacyOrders, {"status"}, "'open'"),
                 firstAvailableExpr(legacyOrders, {"plate"}, "''"),
                 firstAvailableExpr(legacyOrders, {"plate_norm", "plate"}, "''"),
                 firstAvailableExpr(legacyOrders, {"vin"}, "''"),
                 firstAvailableExpr(legacyOrders, {"car_year"}, "0"),
                 firstAvailableExpr(legacyOrders, {"odometer"}, "0"),
                 firstAvailableExpr(legacyOrders, {"engine_type"}, "''"),
                 firstAvailableExpr(legacyOrders, {"engine_volume"}, "''"),
                 firstAvailableExpr(legacyOrders, {"fuel_type"}, "''"),
                 firstAvailableExpr(legacyOrders, {"car"}, "''"),
                 firstAvailableExpr(legacyOrders, {"client", "owner_name"}, "''"),
                 firstAvailableExpr(legacyOrders, {"client_phone"}, "''"),
                 firstAvailableExpr(legacyOrders, {"master_id"}, "''"),
                 firstAvailableExpr(legacyOrders, {"works_sum"}, "0"),
                 firstAvailableExpr(legacyOrders, {"parts_sum"}, "0"),
                 firstAvailableExpr(legacyOrders, {"discount"}, "0"),
                 firstAvailableExpr(legacyOrders, {"master_name_closed"}, "''"),
                 firstAvailableExpr(legacyOrders, {"car_model"}, "''"),
                 firstAvailableExpr(legacyOrders, {"car_brand"}, "''"),
                 firstAvailableExpr(legacyOrders, {"car_kind"}, "''"),
                 colorExpr,
                 firstAvailableExpr(legacyOrders, {"car_color", "color"}, colorExpr),
                 firstAvailableExpr(legacyOrders, {"owner_name", "client"}, "''"), firstAvailableExpr(legacyOrders, {"discount_mode"}, "'legacy'")));
    readOrder.addBindValue(orderNumber);
    if (!readOrder.exec()) return fail(readOrder.lastError().text());
    if (!readOrder.next()) return fail(QStringLiteral("Order %1 not found in archive DB").arg(orderNumber));

    QString safeOrderUid = readOrder.value(1).toString().trimmed();
    if (safeOrderUid.isEmpty()) safeOrderUid = QStringLiteral("legacy_single_%1").arg(orderNumber);
    QString safeMasterId = ensureMasterExists(readOrder.value(18).toString(), readOrder.value(22).toString());
    if (safeMasterId.trimmed().isEmpty()) return fail(QStringLiteral("Failed to ensure master for migrated order %1").arg(orderNumber));

    QSqlQuery insOrder(d);
    insOrder.prepare(
        "INSERT INTO work_orders(number, order_uid, order_number, order_year, opened_at, closed_at, status, plate, plate_norm, vin, car_year, odometer, engine_type, engine_volume, fuel_type, car, client, client_phone, master_id, works_sum, parts_sum, discount, master_name_closed, car_model, car_brand, car_kind, color, car_color, owner_name, discount_mode) "
        "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
    insOrder.addBindValue(readOrder.value(0));
    insOrder.addBindValue(safeOrderUid);
    insOrder.addBindValue(readOrder.value(2));
    insOrder.addBindValue(readOrder.value(3));
    for (int i = 4; i <= 17; ++i) insOrder.addBindValue(readOrder.value(i));
    insOrder.addBindValue(safeMasterId);
    for (int i = 19; i <= 29; ++i) insOrder.addBindValue(readOrder.value(i));
    if (!insOrder.exec()) return fail(insOrder.lastError().text());

    QMap<int, int> serviceIdMap;
    struct PendingParentLink { int newServiceId = 0; int oldParentId = 0; };
    QList<PendingParentLink> pendingParents;

    if (!legacyServices.isEmpty()) {
        const QString svcOrderExpr = firstAvailableExpr(legacyServices, {"order_number"}, "0");
        QSqlQuery readSvc(d);
        readSvc.prepare(
            QStringLiteral(
                "SELECT %1 AS legacy_id, %2 AS title, %3 AS qty, %4 AS price, %5 AS amount, %6 AS note, %7 AS paid, %8 AS parent_id "
                "FROM legacy_single.work_order_services WHERE %9=? ORDER BY %1 ASC")
                .arg(firstAvailableExpr(legacyServices, {"id"}, "0"),
                     firstAvailableExpr(legacyServices, {"title", "name"}, "''"),
                     firstAvailableExpr(legacyServices, {"qty"}, "1"),
                     firstAvailableExpr(legacyServices, {"price"}, "0"),
                     firstAvailableExpr(legacyServices, {"amount"}, "(COALESCE(qty,1)*COALESCE(price,0))"),
                     firstAvailableExpr(legacyServices, {"note"}, "''"),
                     firstAvailableExpr(legacyServices, {"paid"}, "1"),
                     firstAvailableExpr(legacyServices, {"parent_id"}, "0"),
                     svcOrderExpr));
        readSvc.addBindValue(orderNumber);
        if (!readSvc.exec()) return fail(readSvc.lastError().text());
        while (readSvc.next()) {
            const int oldServiceId = readSvc.value(0).toInt();
            QSqlQuery insSvc(d);
            insSvc.prepare("INSERT INTO work_order_services(order_uid, order_number, title, qty, price, amount, note, paid, parent_id) VALUES(?,?,?,?,?,?,?,?,?)");
            insSvc.addBindValue(safeOrderUid);
            insSvc.addBindValue(orderNumber);
            insSvc.addBindValue(readSvc.value(1).toString());
            insSvc.addBindValue(readSvc.value(2));
            insSvc.addBindValue(readSvc.value(3));
            insSvc.addBindValue(readSvc.value(4));
            insSvc.addBindValue(readSvc.value(5).toString());
            insSvc.addBindValue(readSvc.value(6));
            insSvc.addBindValue(0);
            if (!insSvc.exec()) return fail(insSvc.lastError().text());
            const int newServiceId = insSvc.lastInsertId().toInt();
            serviceIdMap.insert(oldServiceId, newServiceId);
            PendingParentLink link;
            link.newServiceId = newServiceId;
            link.oldParentId = readSvc.value(7).toInt();
            pendingParents.append(link);
        }
    }

    for (const PendingParentLink& link : pendingParents) {
        if (link.newServiceId <= 0 || link.oldParentId <= 0) continue;
        const int newParentId = serviceIdMap.value(link.oldParentId, 0);
        if (newParentId <= 0) continue;
        QSqlQuery upParent(d);
        upParent.prepare("UPDATE work_order_services SET parent_id=? WHERE id=?");
        upParent.addBindValue(newParentId);
        upParent.addBindValue(link.newServiceId);
        if (!upParent.exec()) return fail(upParent.lastError().text());
    }

    if (!legacyServiceMasters.isEmpty() && !serviceIdMap.isEmpty()) {
        QSqlQuery readSm(d);
        readSm.prepare(
            QStringLiteral(
                "SELECT %1 AS service_id, %2 AS master_id, %3 AS share_percent, %4 AS role, %5 AS master_name_closed "
                "FROM legacy_single.service_masters WHERE service_id IN (SELECT %6 FROM legacy_single.work_order_services WHERE %7=?) ORDER BY service_id ASC")
                .arg(firstAvailableExpr(legacyServiceMasters, {"service_id"}, "0"),
                     firstAvailableExpr(legacyServiceMasters, {"master_id"}, "''"),
                     firstAvailableExpr(legacyServiceMasters, {"share_percent"}, "100"),
                     firstAvailableExpr(legacyServiceMasters, {"role"}, "'master'"),
                     firstAvailableExpr(legacyServiceMasters, {"master_name_closed"}, "''"),
                     firstAvailableExpr(legacyServices, {"id"}, "0"),
                     firstAvailableExpr(legacyServices, {"order_number"}, "0")));
        readSm.addBindValue(orderNumber);
        if (!readSm.exec()) return fail(readSm.lastError().text());
        while (readSm.next()) {
            const int newServiceId = serviceIdMap.value(readSm.value(0).toInt(), 0);
            if (newServiceId <= 0) continue;
            const QString linkedMasterId = ensureMasterExists(readSm.value(1).toString(), readSm.value(4).toString());
            if (linkedMasterId.trimmed().isEmpty()) return fail(QStringLiteral("Failed to ensure service master for order %1").arg(orderNumber));
            QSqlQuery insSm(d);
            insSm.prepare("INSERT OR REPLACE INTO service_masters(service_id, master_id, share_percent, role, master_name_closed) VALUES(?,?,?,?,?)");
            insSm.addBindValue(newServiceId);
            insSm.addBindValue(linkedMasterId);
            insSm.addBindValue(readSm.value(2));
            insSm.addBindValue(readSm.value(3).toString());
            insSm.addBindValue(readSm.value(4).toString());
            if (!insSm.exec()) return fail(insSm.lastError().text());
        }
    }

    if (!legacyParts.isEmpty()) {
        const QString partOrderExpr = firstAvailableExpr(legacyParts, {"order_number", "work_order", "work_order_id", "work_order_number"}, "0");
        const QString qtyExpr = firstAvailableExpr(legacyParts, {"qty"}, "1");
        const QString priceExpr = firstAvailableExpr(legacyParts, {"price", "purchase_price"}, "0");
        const QString amountExpr = firstAvailableExpr(legacyParts, {"amount", "purchase_amount"}, QStringLiteral("(COALESCE(%1,1) * COALESCE(%2,0))").arg(qtyExpr, priceExpr));
        // Preserve optional canonical fields when present in older archive schemas.
        QStringList supplementalColumns;
        for (const QString& name : {
                 QStringLiteral("installed_at"), QStringLiteral("manual_markup_override"),
                 QStringLiteral("markup_source"), QStringLiteral("price_input_mode"),
                 QStringLiteral("pricing_updated_at"), QStringLiteral("pricing_calc_version"),
                 QStringLiteral("status"), QStringLiteral("installed"), QStringLiteral("installed_by"),
                 QStringLiteral("request_uid"), QStringLiteral("requested_at"),
                 QStringLiteral("requested_by"), QStringLiteral("requested_by_uid"),
                 QStringLiteral("ordered_by"), QStringLiteral("ordered_by_uid"),
                 QStringLiteral("order_comment"), QStringLiteral("node_group"), QStringLiteral("component"),
                 QStringLiteral("metadata_json"), QStringLiteral("created_at"), QStringLiteral("updated_at")}) {
            if (legacyParts.contains(name)) supplementalColumns.append(sqlQuoteIdent(name));
        }
        const QString supplementalSql = supplementalColumns.isEmpty()
            ? QString() : QStringLiteral(", ") + supplementalColumns.join(QStringLiteral(", "));
        const QString supplementalBindings = QStringLiteral(",?").repeated(supplementalColumns.size());
        QSqlQuery readPart(d);
        readPart.prepare(
            QStringLiteral(
                "SELECT %1 AS ordered_at, %2 AS delivery_date, %3 AS store, %4 AS part_number, %5 AS name, %6 AS manufacturer, %7 AS note, %8 AS qty, %9 AS price, %10 AS amount, %11 AS service_id, %12 AS ordered_by_admin, %13 AS paid, %14 AS purchase_price, %15 AS purchase_amount, %16 AS markup_percent, part_uid%19 "
                "FROM legacy_single.work_order_parts WHERE %17=? ORDER BY %18 ASC")
                .arg(firstAvailableExpr(legacyParts, {"ordered_at", "date", "created_at"}, "''"),
                     firstAvailableExpr(legacyParts, {"delivery_date"}, "''"),
                     firstAvailableExpr(legacyParts, {"store"}, "''"),
                     firstAvailableExpr(legacyParts, {"part_number", "oem", "oem_number", "article", "article_number"}, "''"),
                     firstAvailableExpr(legacyParts, {"name", "title", "part_name"}, "''"),
                     firstAvailableExpr(legacyParts, {"manufacturer", "brand"}, "''"),
                     firstAvailableExpr(legacyParts, {"note", "comment", "notes"}, "''"),
                     qtyExpr,
                     priceExpr,
                     amountExpr,
                     firstAvailableExpr(legacyParts, {"service_id"}, "0"),
                     firstAvailableExpr(legacyParts, {"ordered_by_admin"}, "1"),
                     firstAvailableExpr(legacyParts, {"paid"}, "0"),
                     firstAvailableExpr(legacyParts, {"purchase_price", "price"}, priceExpr),
                     firstAvailableExpr(legacyParts, {"purchase_amount", "amount"}, amountExpr),
                     firstAvailableExpr(legacyParts, {"markup_percent"}, "0"),
                     partOrderExpr,
                     firstAvailableExpr(legacyParts, {"id"}, "0"), supplementalSql));
        readPart.addBindValue(orderNumber);
        if (!readPart.exec()) return fail(readPart.lastError().text());
        while (readPart.next()) {
            QSqlQuery insPart(d);
            insPart.prepare(QStringLiteral(
                "INSERT INTO work_order_parts(part_uid, order_uid, order_number, ordered_at, delivery_date, store, part_number, name, manufacturer, note, qty, price, amount, service_id, ordered_by_admin, paid, purchase_price, purchase_amount, markup_percent%1) "
                "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?%2)").arg(supplementalSql, supplementalBindings));
            insPart.addBindValue(readPart.value(16));
            insPart.addBindValue(safeOrderUid);
            insPart.addBindValue(orderNumber);
            insPart.addBindValue(readPart.value(0).toString());
            insPart.addBindValue(readPart.value(1).toString());
            insPart.addBindValue(readPart.value(2).toString());
            insPart.addBindValue(readPart.value(3));
            insPart.addBindValue(readPart.value(4).toString());
            insPart.addBindValue(readPart.value(5).toString());
            insPart.addBindValue(readPart.value(6).toString());
            insPart.addBindValue(readPart.value(7));
            insPart.addBindValue(readPart.value(8));
            insPart.addBindValue(readPart.value(9));
            insPart.addBindValue(serviceIdMap.value(readPart.value(10).toInt(), 0));
            insPart.addBindValue(readPart.value(11));
            insPart.addBindValue(readPart.value(12));
            insPart.addBindValue(readPart.value(13));
            insPart.addBindValue(readPart.value(14));
            insPart.addBindValue(readPart.value(15));
            for (int index = 0; index < supplementalColumns.size(); ++index)
                insPart.addBindValue(readPart.value(17 + index));
            if (!insPart.exec() || insPart.numRowsAffected() != 1)
                return fail(QStringLiteral("Archive part insert failed: %1").arg(insPart.lastError().text()));
        }
        if (readPart.lastError().isValid()) return fail(readPart.lastError().text());
    }

    if (!legacyOutsource.isEmpty()) {
        const QString outsourceOrderExpr = firstAvailableExpr(legacyOutsource, {"order_number"}, "0");
        QSqlQuery readOut(d);
        readOut.prepare(
            QStringLiteral(
                "SELECT %1 AS sent_at, %2 AS company, %3 AS part_name, %4 AS repair_cost, %5 AS client_price, %6 AS note, %7 AS created_at, %8 AS service_id "
                "FROM legacy_single.outsource_repairs WHERE %9=? ORDER BY %10 ASC")
                .arg(firstAvailableExpr(legacyOutsource, {"sent_at"}, "''"),
                     firstAvailableExpr(legacyOutsource, {"company"}, "''"),
                     firstAvailableExpr(legacyOutsource, {"part_name"}, "''"),
                     firstAvailableExpr(legacyOutsource, {"repair_cost"}, "0"),
                     firstAvailableExpr(legacyOutsource, {"client_price"}, "0"),
                     firstAvailableExpr(legacyOutsource, {"note"}, "''"),
                     firstAvailableExpr(legacyOutsource, {"created_at", "sent_at"}, "''"),
                     firstAvailableExpr(legacyOutsource, {"service_id"}, "0"),
                     outsourceOrderExpr,
                     firstAvailableExpr(legacyOutsource, {"id"}, "0")));
        readOut.addBindValue(orderNumber);
        if (!readOut.exec()) return fail(readOut.lastError().text());
        while (readOut.next()) {
            const int newServiceId = serviceIdMap.value(readOut.value(7).toInt(), 0);
            QSqlQuery insOut(d);
            insOut.prepare(
                "INSERT INTO outsource_repairs(order_uid, order_number, sent_at, company, part_name, repair_cost, client_price, note, created_at, service_id) "
                "VALUES(?,?,?,?,?,?,?,?,?,?)");
            insOut.addBindValue(safeOrderUid);
            insOut.addBindValue(orderNumber);
            insOut.addBindValue(readOut.value(0).toString());
            insOut.addBindValue(readOut.value(1).toString());
            insOut.addBindValue(readOut.value(2).toString());
            insOut.addBindValue(readOut.value(3));
            insOut.addBindValue(readOut.value(4));
            insOut.addBindValue(readOut.value(5).toString());
            insOut.addBindValue(readOut.value(6).toString());
            insOut.addBindValue(newServiceId);
            if (!insOut.exec()) return fail(insOut.lastError().text());

            if (newServiceId > 0) {
                QSqlQuery insJob(d);
                insJob.prepare(
                    "INSERT INTO outsource_jobs(order_uid, order_number, service_id, company_id, company_name, sent_date, return_date, part_name, company_price, markup_percent, client_price, note) "
                    "VALUES(?,?,?,?,?,?,?,?,?,?,?,?)");
                insJob.addBindValue(safeOrderUid);
                insJob.addBindValue(orderNumber);
                insJob.addBindValue(newServiceId);
                insJob.addBindValue(0);
                insJob.addBindValue(readOut.value(1).toString());
                insJob.addBindValue(readOut.value(0).toString());
                insJob.addBindValue(QString());
                insJob.addBindValue(readOut.value(2).toString());
                insJob.addBindValue(readOut.value(3));
                insJob.addBindValue(0);
                insJob.addBindValue(readOut.value(4));
                insJob.addBindValue(readOut.value(5).toString());
                if (!insJob.exec()) return fail(insJob.lastError().text());
            }
        }
    }

        Database::refreshOrderRegistry(orderNumber, nullptr);
        return true;
    });

    detachLegacy();
    if (!tx.ok) {
        if (errorText) *errorText = tx.errorText;
        return false;
    }
    return true;
}

bool importSingleLegacyOrderToActive(int orderNumber, QString* errorText)
{
    return importSingleArchivedOrderToActiveFromPath(AppRuntime::archiveDbFilePath(), orderNumber, errorText);
}

static bool pruneActiveClosedOrdersToArchive(QSqlDatabase database, QString* errorText)
{
    return OrderArchiveShardSqlSupport::pruneActiveClosedOrdersToArchive(database, errorText);
}

bool purgeHydratedArchiveCopiesFromActive(QSqlDatabase database, QString* errorText)
{
    return OrderArchiveShardSqlSupport::purgeHydratedArchiveCopiesFromActive(database, errorText);
}
