// Extracted from src/infrastructure/db/sqlite/DbArchiveRoutingStageSupport.cpp for P1/P2 monolith reduction.
// Fragment boundary is function-safe; do not split inside macro calls, raw strings or SQL literals.

{
    if (prepareSchema) {
        if (!ensureLocalRegistryMirrorSchema(database)) return false;
    } else if (!database.isValid() || !database.isOpen()) return false;
    QSqlQuery up(database);
    up.prepare(QStringLiteral(
        "INSERT INTO order_registry(order_number, order_uid, visible_order_number, order_year, storage_kind, storage_path, source_db, imported_at, last_seen_at, status, opened_at, closed_at, plate, plate_norm, vin, car, client, master_id, master_name_closed, works_sum, parts_sum) "
        "VALUES(?, ?, ?, ?, ?, ?, ?, ?, datetime('now'), ?,?,?,?,?,?,?,?,?,?,?,?) "
        "ON CONFLICT(order_number) DO UPDATE SET "
        "order_uid=CASE WHEN TRIM(COALESCE(excluded.order_uid,''))<>'' THEN excluded.order_uid ELSE order_registry.order_uid END, "
        "visible_order_number=CASE WHEN COALESCE(excluded.visible_order_number,0)>0 THEN excluded.visible_order_number ELSE order_registry.visible_order_number END, "
        "order_year=CASE WHEN COALESCE(excluded.order_year,0)>0 THEN excluded.order_year ELSE order_registry.order_year END, "
        "storage_kind=excluded.storage_kind, storage_path=excluded.storage_path, source_db=excluded.source_db, "
        "imported_at=CASE WHEN COALESCE(excluded.imported_at,'')<>'' THEN excluded.imported_at ELSE order_registry.imported_at END, "
        "last_seen_at=datetime('now'), status=excluded.status, opened_at=excluded.opened_at, closed_at=excluded.closed_at, "
        "plate=excluded.plate, plate_norm=excluded.plate_norm, vin=excluded.vin, car=excluded.car, client=excluded.client, "
        "master_id=excluded.master_id, master_name_closed=excluded.master_name_closed, works_sum=excluded.works_sum, parts_sum=excluded.parts_sum"));
    up.addBindValue(orderNumber);
    up.addBindValue(nonNullText(orderUid));
    up.addBindValue(visibleOrderNumber);
    up.addBindValue(orderYear);
    up.addBindValue(nonNullText(storageKind));
    up.addBindValue(nonNullText(storagePath));
    up.addBindValue(nonNullText(sourceDb));
    up.addBindValue(nonNullText(importedAt));
    up.addBindValue(nonNullText(status));
    up.addBindValue(nonNullText(openedAt));
    up.addBindValue(nonNullText(closedAt));
    up.addBindValue(nonNullText(plate));
    up.addBindValue(nonNullText(plateNorm));
    up.addBindValue(nonNullText(vin));
    up.addBindValue(nonNullText(car));
    up.addBindValue(nonNullText(client));
    up.addBindValue(nonNullText(masterId));
    up.addBindValue(nonNullText(masterNameClosed));
    up.addBindValue(worksSum);
    up.addBindValue(partsSum);
    if (!up.exec() || up.numRowsAffected()!=1) {
        qWarning() << "Registry mirror upsert failed for order" << orderNumber << up.lastError().text();
        return false;
    }
    return true;
}

void removeRegistryMirrorRow(QSqlDatabase database, int orderNumber)
{
    if (!ensureLocalRegistryMirrorSchema(database)) return;
    QSqlQuery q(database);
    q.prepare("DELETE FROM order_registry WHERE order_number=?");
    q.addBindValue(orderNumber);
    if (!q.exec()) qWarning() << "Registry mirror delete failed for order" << orderNumber << q.lastError().text();
}

static bool readArchivedOrderRegistryMetadata(const QString& archivePath, int orderNumber,
                                              QString* masterId, QString* masterNameClosed,
                                              double* worksSum, double* partsSum)
{
    const QString path = archivePath.trimmed();
    if (path.isEmpty() || !QFileInfo::exists(path) || orderNumber <= 0) return false;

    const QString connName = QStringLiteral("carservice_archive_probe_%1_%2")
        .arg(orderNumber)
        .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    bool ok = false;
    {
        QSqlDatabase probe = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
        probe.setDatabaseName(path);
        if (!probe.open()) {
            probe.close();
        } else {
            const QSet<QString> archiveOrderCols = tableColumnsForDb(probe, QStringLiteral("work_orders"));
            auto archiveExpr = [&](const QString& columnName, const QString& fallbackExpr) {
                return archiveOrderCols.contains(columnName)
                    ? QStringLiteral("COALESCE(%1,%2)").arg(sqlQuoteIdent(columnName), fallbackExpr)
                    : fallbackExpr;
            };
            QSqlQuery q(probe);
            q.prepare(QStringLiteral("SELECT %1, %2, %3, %4 FROM work_orders WHERE number=? LIMIT 1")
                      .arg(archiveExpr(QStringLiteral("master_id"), QStringLiteral("''")),
                           archiveExpr(QStringLiteral("master_name_closed"), QStringLiteral("''")),
                           archiveExpr(QStringLiteral("works_sum"), QStringLiteral("0")),
                           archiveExpr(QStringLiteral("parts_sum"), QStringLiteral("0"))));
            q.addBindValue(orderNumber);
            ok = q.exec() && q.next();
            if (ok) {
                if (masterId) *masterId = q.value(0).toString();
                if (masterNameClosed) *masterNameClosed = q.value(1).toString();
                if (worksSum) *worksSum = q.value(2).toDouble();
                if (partsSum) *partsSum = q.value(3).toDouble();
            }
            probe.close();
        }
    }
    SqliteConnectionUtils::safeCloseAndRemoveDatabase(connName);
    return ok;
}

bool syncLocalRegistryMirrorFromRegistryFile(QSqlDatabase database, QString* errorText)
{
    if (!ensureLocalRegistryMirrorSchema(database)) {
        if (errorText) *errorText = QStringLiteral("Local registry mirror schema is unavailable");
        return false;
    }
    if (!ensureRegistrySchemaFile()) {
        if (errorText) *errorText = QStringLiteral("Registry DB is unavailable");
        return false;
    }

    const QString registryPath = AppRuntime::registryDbFilePath().trimmed();
    if (registryPath.isEmpty()) {
        if (errorText) *errorText = QStringLiteral("Registry DB path is empty");
        return false;
    }

    const QString connName = QStringLiteral("carservice_registry_sync_%1")
                                 .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    bool ok = true;
    QString localError;
    {
        QSqlDatabase source = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
        source.setDatabaseName(registryPath);
        if (!source.open()) {
            localError = QStringLiteral("Registry DB open failed: %1").arg(source.lastError().text());
            ok = false;
        } else {
            const QSet<QString> fileCols = tableColumnsForDb(source, QStringLiteral("order_registry"));
            auto fileExpr = [&](const QString& columnName, const QString& fallbackExpr) {
                return fileCols.contains(columnName)
                           ? QStringLiteral("COALESCE(%1,%2)").arg(sqlQuoteIdent(columnName), fallbackExpr)
                           : fallbackExpr;
            };

            QSqlQuery read(source);
            const QString readSql = QStringLiteral(
                                        "SELECT COALESCE(order_number,0), %1, %2, %3, %4, %5, %6, %7, %8, %9, %10, %11, %12, %13, %14, %15, %16, %17, %18, %19 "
                                        "FROM order_registry ORDER BY order_number ASC")
                                        .arg(fileExpr(QStringLiteral("order_uid"), QStringLiteral("''")),
                                             fileExpr(QStringLiteral("visible_order_number"), QStringLiteral("0")),
                                             fileExpr(QStringLiteral("order_year"), QStringLiteral("0")),
                                             fileExpr(QStringLiteral("storage_kind"), QStringLiteral("'active'")),
                                             fileExpr(QStringLiteral("storage_path"), QStringLiteral("''")),
                                             fileExpr(QStringLiteral("source_db"), QStringLiteral("''")),
                                             fileExpr(QStringLiteral("imported_at"), QStringLiteral("''")),
                                             fileExpr(QStringLiteral("status"), QStringLiteral("''")),
                                             fileExpr(QStringLiteral("opened_at"), QStringLiteral("''")),
                                             fileExpr(QStringLiteral("closed_at"), QStringLiteral("''")),
                                             fileExpr(QStringLiteral("plate"), QStringLiteral("''")),
                                             fileExpr(QStringLiteral("plate_norm"), QStringLiteral("''")),
                                             fileExpr(QStringLiteral("vin"), QStringLiteral("''")),
                                             fileExpr(QStringLiteral("car"), QStringLiteral("''")),
                                             fileExpr(QStringLiteral("client"), QStringLiteral("''")),
                                             fileExpr(QStringLiteral("master_id"), QStringLiteral("''")),
                                             fileExpr(QStringLiteral("master_name_closed"), QStringLiteral("''")),
                                             fileExpr(QStringLiteral("works_sum"), QStringLiteral("0")),
                                             fileExpr(QStringLiteral("parts_sum"), QStringLiteral("0")));
            if (!read.exec(readSql)) {
                localError = QStringLiteral("Registry file read failed: %1").arg(read.lastError().text());
                ok = false;
            } else {
                MutationExecutor::Request request;
                request.sourceModule = QStringLiteral("DbArchiveRoutingSupport::syncLocalRegistryMirrorFromRegistryFile");
                request.entityType = QStringLiteral("registry");
                request.entityId = QStringLiteral("order_registry");
                request.operation = QStringLiteral("sync_local_registry_mirror_from_registry_file");
                request.changedTables = {QStringLiteral("order_registry"), QStringLiteral("temp_registry_sync_seen")};
                request.beginErrorFallback = QStringLiteral("Registry sync transaction begin failed");
                request.commitErrorFallback = QStringLiteral("Registry sync transaction commit failed");

                const auto tx = MutationExecutor::execute(database, request, [&](QSqlDatabase& txDb, QString* txError) {
                    auto fail = [&](const QString& message) {
                        localError = message;
                        if (txError) *txError = message;
                        return false;
                    };

                    QSet<int> seenOrderNumbers;
                    while (read.next()) {
                        const int orderNumber = read.value(0).toInt();
                        if (orderNumber <= 0) {
                            continue;
                        }
                        seenOrderNumbers.insert(orderNumber);
                        if (!upsertRegistryMirrorRow(txDb,
                                                     orderNumber,
                                                     nonNullVariantText(read.value(1)),
                                                     read.value(2).toInt(),
                                                     read.value(3).toInt(),
                                                     nonNullVariantText(read.value(4)),
                                                     nonNullVariantText(read.value(5)),
                                                     nonNullVariantText(read.value(6)),
                                                     nonNullVariantText(read.value(7)),
                                                     nonNullVariantText(read.value(8)),
                                                     nonNullVariantText(read.value(9)),
                                                     nonNullVariantText(read.value(10)),
                                                     nonNullVariantText(read.value(11)),
                                                     nonNullVariantText(read.value(12)),
                                                     nonNullVariantText(read.value(13)),
                                                     nonNullVariantText(read.value(14)),
                                                     nonNullVariantText(read.value(15)),
                                                     nonNullVariantText(read.value(16)),
                                                     nonNullVariantText(read.value(17)),
                                                     read.value(18).toDouble(),
                                                     read.value(19).toDouble())) {
                            return fail(QStringLiteral("Registry file -> local mirror upsert failed for order %1").arg(orderNumber));
                        }
                    }
                    if (read.lastError().isValid() && !read.lastError().text().trimmed().isEmpty()) {
                        return fail(QStringLiteral("Registry file cursor read failed: %1").arg(read.lastError().text()));
                    }

                    struct RepairCandidate {
                        int orderNumber = 0;
                        QString storagePath;
                        QString sourceDb;
                    };
                    QVector<RepairCandidate> repairRows;
                    QSqlQuery repairCandidates(txDb);
                    repairCandidates.prepare(QStringLiteral(
                        "SELECT order_number, COALESCE(storage_path,''), COALESCE(source_db,'') "
                        "FROM order_registry "
                        "WHERE COALESCE(storage_kind,'')='archive' "
                        "AND (TRIM(COALESCE(master_name_closed,''))='' OR COALESCE(works_sum,0)=0 OR COALESCE(parts_sum,0)=0)"));
                    if (!repairCandidates.exec()) {
                        return fail(QStringLiteral("Registry repair candidate scan failed: %1").arg(repairCandidates.lastError().text()));
                    }
                    while (repairCandidates.next()) {
                        RepairCandidate row;
                        row.orderNumber = repairCandidates.value(0).toInt();
                        row.storagePath = repairCandidates.value(1).toString().trimmed();
                        row.sourceDb = repairCandidates.value(2).toString().trimmed();
                        repairRows.push_back(row);
                    }
                    if (repairCandidates.lastError().isValid() && !repairCandidates.lastError().text().trimmed().isEmpty()) {
                        return fail(QStringLiteral("Registry repair candidate fetch failed: %1").arg(repairCandidates.lastError().text()));
                    }

                    for (const RepairCandidate& row : repairRows) {
                        QString masterId;
                        QString masterNameClosed;
                        double worksSum = 0.0;
                        double partsSum = 0.0;
                        const QString archivePath = !row.storagePath.isEmpty() ? row.storagePath : row.sourceDb;
                        if (!readArchivedOrderRegistryMetadata(archivePath, row.orderNumber, &masterId, &masterNameClosed, &worksSum, &partsSum)) {
                            continue;
                        }

                        QSqlQuery localUpdate(txDb);
                        localUpdate.prepare(QStringLiteral(
                            "UPDATE order_registry SET master_id=?, master_name_closed=?, works_sum=?, parts_sum=? WHERE order_number=?"));
                        localUpdate.addBindValue(nonNullText(masterId));
                        localUpdate.addBindValue(nonNullText(masterNameClosed));
                        localUpdate.addBindValue(worksSum);
                        localUpdate.addBindValue(partsSum);
                        localUpdate.addBindValue(row.orderNumber);
                        if (!localUpdate.exec()) {
                            return fail(QStringLiteral("Local registry repair update failed: %1").arg(localUpdate.lastError().text()));
                        }
                    }

                    QSqlQuery tempCreate(txDb);
                    if (!tempCreate.exec(QStringLiteral("CREATE TEMP TABLE IF NOT EXISTS temp_registry_sync_seen(order_number INTEGER PRIMARY KEY)"))) {
                        return fail(QStringLiteral("Temp registry sync table create failed: %1").arg(tempCreate.lastError().text()));
                    }

                    QSqlQuery tempClear(txDb);
                    if (!tempClear.exec(QStringLiteral("DELETE FROM temp_registry_sync_seen"))) {
                        return fail(QStringLiteral("Temp registry sync table clear failed: %1").arg(tempClear.lastError().text()));
                    }

                    QSqlQuery tempInsert(txDb);
                    tempInsert.prepare(QStringLiteral("INSERT OR IGNORE INTO temp_registry_sync_seen(order_number) VALUES(?)"));
                    for (int orderNumber : seenOrderNumbers) {
                        tempInsert.addBindValue(orderNumber);
                        if (!tempInsert.exec()) {
                            return fail(QStringLiteral("Temp registry sync seen insert failed: %1").arg(tempInsert.lastError().text()));
                        }
                    }

                    QSqlQuery purge(txDb);
                    purge.prepare(QStringLiteral(
                        "DELETE FROM order_registry "
                        "WHERE order_number NOT IN (SELECT order_number FROM temp_registry_sync_seen) "
                        "AND order_number NOT IN (SELECT number FROM work_orders)"));
                    if (!purge.exec()) {
                        return fail(QStringLiteral("Local registry purge failed: %1").arg(purge.lastError().text()));
                    }

                    QSqlQuery tempDrop(txDb);
                    tempDrop.exec(QStringLiteral("DROP TABLE IF EXISTS temp_registry_sync_seen"));
                    return true;
                });

                if (!tx.ok) {
                    localError = tx.errorText;
                    ok = false;
                }
            }
            source.close();
        }
    }
    SqliteConnectionUtils::safeCloseAndRemoveDatabase(connName);
    if (!ok) {
        if (errorText) {
            *errorText = localError.trimmed().isEmpty()
                             ? QStringLiteral("Registry file -> local mirror sync failed")
                             : localError;
        }
        return false;
    }
    return true;
}
bool ensureWorkspaceOrderStateSchema(QSqlDatabase database)
{
    if (!database.isValid()) return false;
    if (!database.isOpen() && !database.open()) return false;
    QSqlQuery q(database);
    if (!q.exec(
            "CREATE TABLE IF NOT EXISTS workspace_order_state ("
            "  order_number INTEGER PRIMARY KEY,"
            "  origin TEXT NOT NULL DEFAULT 'native',"
            "  source_db TEXT NOT NULL DEFAULT '',"
            "  hydrated_at TEXT NOT NULL DEFAULT '',"
            "  modified_at TEXT NOT NULL DEFAULT ''"
            ");")) {
        qWarning() << "Workspace order state schema failed:" << q.lastError().text();
        return false;
    }
    const QSet<QString> stateCols = tableColumnsForDb(database, QStringLiteral("workspace_order_state"));
    auto ensureStateColumn = [&](const QString& colName, const QString& ddl) {
        if (stateCols.contains(colName)) return true;
        QSqlQuery alter(database);
        return alter.exec(QStringLiteral("ALTER TABLE workspace_order_state ADD COLUMN %1;").arg(ddl));
    };
    if (!ensureStateColumn(QStringLiteral("order_uid"), QStringLiteral("order_uid TEXT NOT NULL DEFAULT ''"))) return false;
    if (!q.exec("UPDATE workspace_order_state SET order_uid=COALESCE((SELECT NULLIF(TRIM(COALESCE(w.order_uid,'')), '') FROM work_orders w WHERE w.number=workspace_order_state.order_number), CASE WHEN COALESCE(order_number,0)>0 THEN 'rewrite_' || order_number ELSE '' END) WHERE TRIM(COALESCE(order_uid,''))=''")) return false;
    if (!ensureStateColumn(QStringLiteral("registry_revision"), QStringLiteral("registry_revision INTEGER NOT NULL DEFAULT 0 CHECK(registry_revision>=0)"))) return false;
    if (!ensureStateColumn(QStringLiteral("published_registry_revision"), QStringLiteral("published_registry_revision INTEGER NOT NULL DEFAULT 0 CHECK(published_registry_revision>=0)"))) return false;
    if (!q.exec("CREATE UNIQUE INDEX IF NOT EXISTS idx_workspace_order_state_uid_unique ON workspace_order_state(order_uid)")) return false;
    if (!q.exec("CREATE INDEX IF NOT EXISTS idx_workspace_order_state_origin ON workspace_order_state(origin)")) return false;
    return true;
}

static bool purgeOrderGraphRowsInOwnedTransaction(QSqlDatabase txDb, int orderNumber,
                                                  QString* txError, bool verifyArchive)
{
    if (!TransactionContext::hasActiveTransaction(txDb)) {
        if (txError) *txError = QStringLiteral("Order graph purge requires an owned transaction");
        return false;
    }
        auto fail = [&](const QString& msg) {
            if (txError) *txError = msg;
            return false;
        };
        QSqlQuery q(txDb);
        if (verifyArchive) {
            // Acquire writer reservations on BOTH databases before final comparison.
            // Concurrent writers cannot change either graph between verification and DELETE.
            if (!q.exec("UPDATE main.work_orders SET number=number WHERE 0") ||
                !q.exec("UPDATE archive_rollover.work_orders SET number=number WHERE 0")) return fail(q.lastError().text());
            if (!OrderArchiveShardSqlSupport::verifyCopiedOrderGraph(txDb, orderNumber, txError)) return false;
            QSqlQuery identity(txDb);
            if (!identity.prepare(QStringLiteral("SELECT o.order_uid FROM main.work_orders o WHERE o.number=? AND %1").arg(OrderStoragePolicyService::closedOrderPredicate("o")))) return fail(identity.lastError().text());
            identity.addBindValue(orderNumber);
            if (!identity.exec() || !identity.next()) return fail("Archive source identity missing");
            const auto uid=identity.value(0).toString();identity.finish();
            // Completion and source DELETE share this transaction: neither can survive alone.
            if (!ArchiveTransferJournal::advance(txDb, uid, "complete", txError)) return false;
        }
    q.prepare("DELETE FROM work_order_part_attachments WHERE part_id IN (SELECT id FROM work_order_parts WHERE order_number=?)");
    q.addBindValue(orderNumber);
    if (!q.exec() && !q.lastError().text().contains(QStringLiteral("no such table"), Qt::CaseInsensitive)) return fail(q.lastError().text());
    q.finish();
    q.prepare("DELETE FROM work_order_service_attachments WHERE service_id IN (SELECT id FROM work_order_services WHERE order_number=?)");
    q.addBindValue(orderNumber);
    if (!q.exec() && !q.lastError().text().contains(QStringLiteral("no such table"), Qt::CaseInsensitive)) return fail(q.lastError().text());
    q.finish();
    q.prepare("DELETE FROM service_masters WHERE service_id IN (SELECT id FROM work_order_services WHERE order_number=?)");
    q.addBindValue(orderNumber);
    if (!q.exec()) return fail(q.lastError().text());
    q.finish();
    q.prepare("DELETE FROM outsource_jobs WHERE order_number=?");
    q.addBindValue(orderNumber);
    if (!q.exec() && !q.lastError().text().contains(QStringLiteral("no such table"), Qt::CaseInsensitive)) return fail(q.lastError().text());
    q.finish();
    q.prepare("DELETE FROM outsource_repairs WHERE order_number=?");
    q.addBindValue(orderNumber);
    if (!q.exec() && !q.lastError().text().contains(QStringLiteral("no such table"), Qt::CaseInsensitive)) return fail(q.lastError().text());
    q.finish();
    q.prepare("DELETE FROM router_cache_service_masters WHERE service_id IN (SELECT id FROM router_cache_work_order_services WHERE order_number=?)");
    q.addBindValue(orderNumber);
    if (!q.exec() && !q.lastError().text().contains(QStringLiteral("no such table"), Qt::CaseInsensitive)) return fail(q.lastError().text());
    q.finish();
    q.prepare("DELETE FROM router_cache_outsource_repairs WHERE order_number=?");
    q.addBindValue(orderNumber);
    if (!q.exec() && !q.lastError().text().contains(QStringLiteral("no such table"), Qt::CaseInsensitive)) return fail(q.lastError().text());
    q.finish();
    q.prepare("DELETE FROM router_cache_work_order_parts WHERE order_number=?");
    q.addBindValue(orderNumber);
    if (!q.exec() && !q.lastError().text().contains(QStringLiteral("no such table"), Qt::CaseInsensitive)) return fail(q.lastError().text());
    q.finish();
    q.prepare("DELETE FROM router_cache_work_order_services WHERE order_number=?");
    q.addBindValue(orderNumber);
    if (!q.exec() && !q.lastError().text().contains(QStringLiteral("no such table"), Qt::CaseInsensitive)) return fail(q.lastError().text());
    q.finish();
    q.prepare("DELETE FROM router_cache_work_orders WHERE number=?");
    q.addBindValue(orderNumber);
    if (!q.exec() && !q.lastError().text().contains(QStringLiteral("no such table"), Qt::CaseInsensitive)) return fail(q.lastError().text());
    q.finish();
    q.prepare("DELETE FROM work_order_parts WHERE order_number=?");
    q.addBindValue(orderNumber);
    if (!q.exec()) return fail(q.lastError().text());
    q.finish();
    q.prepare("DELETE FROM work_order_services WHERE order_number=?");
    q.addBindValue(orderNumber);
    if (!q.exec()) return fail(q.lastError().text());
    q.finish();
    q.prepare("DELETE FROM work_orders WHERE number=?");
    q.addBindValue(orderNumber);
    if (!q.exec()) return fail(q.lastError().text());
        if (verifyArchive) ArchiveTransferJournal::crashPoint("before_purge_commit");
        return true;
}

static bool purgeOrderGraphImpl(QSqlDatabase database, int orderNumber, QString* errorText, bool verifyArchive)
{
    if (orderNumber <= 0) return true;

    MutationExecutor::Request request;
    request.sourceModule = QStringLiteral("DbArchiveRoutingSupport");
    request.entityType = QStringLiteral("order_graph");
    request.entityId = QString::number(orderNumber);
    request.operation = QStringLiteral("purge_active_order_graph");
    request.description = QStringLiteral("Purge active order graph by internal order number");
    request.changedTables = {QStringLiteral("service_masters"), QStringLiteral("outsource_jobs"), QStringLiteral("outsource_repairs"),
                             QStringLiteral("router_cache_service_masters"), QStringLiteral("router_cache_outsource_repairs"),
                             QStringLiteral("router_cache_work_order_parts"), QStringLiteral("router_cache_work_order_services"), QStringLiteral("router_cache_work_orders"),
                             QStringLiteral("work_order_part_attachments"), QStringLiteral("work_order_service_attachments"),
                             QStringLiteral("work_order_parts"), QStringLiteral("work_order_services"), QStringLiteral("work_orders")};
    request.beginErrorFallback = QStringLiteral("Не вдалося розпочати видалення active order graph.");
    request.commitErrorFallback = QStringLiteral("Не вдалося завершити видалення active order graph.");

    const auto tx = MutationExecutor::execute(database, request, [&](QSqlDatabase& txDb, QString* txError) {
        return purgeOrderGraphRowsInOwnedTransaction(txDb, orderNumber, txError, verifyArchive);
    });
    if (!tx.ok) {
        if (errorText) *errorText = tx.errorText;
        return false;
    }
    removeRegistryMirrorRow(database, orderNumber);
    removeWorkspaceOrderStateEntry(database, orderNumber);
    return true;
}


bool purgeActiveOrderGraph(QSqlDatabase database, int orderNumber, QString* errorText)
{
    return purgeOrderGraphImpl(database, orderNumber, errorText, false);
}

bool purgeVerifiedArchivedOrderGraph(QSqlDatabase database, int orderNumber, QString* errorText)
{
    return purgeOrderGraphImpl(database, orderNumber, errorText, true);
}


bool syncPartRegistryFromLiveOrder(QSqlDatabase database, int orderNumber, const QString& storageKind,
                                  const QString& storagePath, const QString& sourceDb)
{
    if (orderNumber <= 0) return true;
    if (!ensurePartRegistrySchemaFile()) return false;

    QSqlQuery orderQ(database);
    orderQ.prepare(QStringLiteral("SELECT COALESCE(plate,''), COALESCE(plate_norm,''), COALESCE(vin,''), COALESCE(car,''), COALESCE(client,''),COALESCE(order_uid,'') FROM work_orders WHERE number=?"));
    orderQ.addBindValue(orderNumber);
    if (!orderQ.exec()) return false;
    if (!orderQ.next()) {
        if (orderQ.lastError().isValid()) return false;
        return removePartRegistryFileRows(orderNumber, storageKind, storagePath, false);
    }
    const QString plate = orderQ.value(0).toString();
    const QString plateNorm = orderQ.value(1).toString();
    const QString vin = orderQ.value(2).toString();
    const QString car = orderQ.value(3).toString();
    const QString client = orderQ.value(4).toString();

    const QString orderUid=orderQ.value(5).toString();
    if (orderUid.trimmed().isEmpty()) return false;
    QSqlQuery identity(database);
    if (!identity.prepare(QStringLiteral("SELECT 1 FROM work_order_parts WHERE (order_number=? AND COALESCE(order_uid,'')<>?) OR (order_uid=? AND COALESCE(order_number,0)<>?) LIMIT 1"))) return false;
    identity.addBindValue(orderNumber);identity.addBindValue(orderUid);identity.addBindValue(orderUid);identity.addBindValue(orderNumber);
    if (!identity.exec() || identity.next() || identity.lastError().isValid()) {
        qWarning()<<"Part registry source identity conflict for order"<<orderNumber<<identity.lastError().text();return false;
    }
    QSqlQuery sourceUid(database);
    if(!sourceUid.prepare(QStringLiteral("SELECT 1 FROM work_order_parts p WHERE p.order_number=? AND (TRIM(COALESCE(p.part_uid,''))='' OR (SELECT COUNT(*) FROM work_order_parts x WHERE x.part_uid=p.part_uid)<>1) LIMIT 1")))return false;
    sourceUid.addBindValue(orderNumber);
    if(!sourceUid.exec() || sourceUid.next() || sourceUid.lastError().isValid())return false;
    QSqlQuery previous(QSqlDatabase::database(dbRegistryConnectionName(),false));
    if(!previous.prepare(QStringLiteral("SELECT order_uid,part_uid,part_ref FROM part_registry WHERE order_number=? AND storage_kind=? AND storage_path=?")))return false;
    previous.addBindValue(orderNumber);previous.addBindValue(storageKind);previous.addBindValue(storagePath);
    if(!previous.exec())return false;
    while(previous.next()) {
        // Numeric agreement is not proof of historical UID ownership.
        if(previous.value(0).toString()!=orderUid || previous.value(1).toString().trimmed().isEmpty())return false;
        QSqlQuery owner(database);
        if(!owner.prepare(QStringLiteral("SELECT 1 FROM work_order_parts WHERE part_uid=? AND (order_uid<>? OR id<>?) LIMIT 1")))return false;
        owner.addBindValue(previous.value(1));owner.addBindValue(orderUid);owner.addBindValue(previous.value(2));
        if(!owner.exec() || owner.next() || owner.lastError().isValid())return false;
    }
    if(previous.lastError().isValid())return false;
    previous.finish();
    if (!removePartRegistryFileRows(orderNumber, storageKind, storagePath, false)) return false;

    QSqlQuery partQ(database);
    partQ.prepare(QStringLiteral("SELECT COALESCE(id,0), COALESCE(ordered_at,''), COALESCE(delivery_date,''), COALESCE(part_number,''), COALESCE(name,''), COALESCE(note,''),part_uid FROM work_order_parts WHERE order_number=? ORDER BY id ASC"));
    partQ.addBindValue(orderNumber);
    if (!partQ.exec()) return false;
    while (partQ.next()) {
        const int partRef = partQ.value(0).toInt();
        const QString orderedAt = partQ.value(1).toString();
        const QString deliveryDate = partQ.value(2).toString();
        const QString partNumber = partQ.value(3).toString();
        const QString name = partQ.value(4).toString();
        const QString note = partQ.value(5).toString();
        const QString entryKey = makePartRegistryEntryKey(storageKind, storagePath, orderNumber, partRef, orderedAt, partNumber, name);
        if (!upsertPartRegistryFileRow(entryKey, orderNumber, partRef, storageKind, storagePath, sourceDb,
                                       orderedAt, deliveryDate, partNumber, name, note,
                                       plate, plateNorm, vin, car, client, false, orderUid, partQ.value(6).toString())) {
            return false;
        }
    }
    return !partQ.lastError().isValid();
}

bool upsertActiveOrdersIntoRegistry(QString* errorText)
{
    auto fail=[&](const QString& message){if(errorText)*errorText=message;return false;};
    auto source=Database::db(errorText);
    if(!validateStartupRegistryIdentity(source,AppRuntime::registryDbFilePath(),errorText))return false;
    if(!ensureRegistrySchemaFile())return fail(QStringLiteral("Registry schema is unavailable"));
    // A durable batch marker covers projection repair even when source revisions
    // are already acknowledged. Do not invent new source revisions on startup.
    auto registry=QSqlDatabase::database(dbRegistryConnectionName(),false);
    {
        TransactionContext pendingTx(registry,QStringLiteral("RegistryBulkPending"));
        if(!pendingTx.begin(QStringLiteral("Cannot start registry bulk marker"),errorText))return false;
        const auto pendingTxRollback=qScopeGuard([&]{if(pendingTx.isActive()){QString ignored;pendingTx.rollback(QString(),&ignored);}});
        QSqlQuery pending(registry);
        if(!pending.exec(QStringLiteral("INSERT INTO registry_meta(key,value) VALUES('runtime_bulk_pending','1') ON CONFLICT(key) DO UPDATE SET value='1'")))return fail(pending.lastError().text());
        if(pending.numRowsAffected()!=1)return fail(QStringLiteral("Registry bulk marker was not recorded"));
        pending.finish();
        if(!pendingTx.commit(QStringLiteral("Cannot commit registry bulk marker"),errorText))return false;
    }
    // Newly materialized source state and its first revision commit atomically.
    TransactionContext tx(source,QStringLiteral("RegistryBulkIntent"));
    if(!tx.begin(QStringLiteral("Cannot start registry bulk intentions"),errorText))return false;
    const auto txRollback=qScopeGuard([&]{if(tx.isActive()){QString ignored;tx.rollback(QString(),&ignored);}});
    QList<int> numbers;
    QSqlQuery read(source);
    if(!read.exec(QStringLiteral("SELECT w.number,s.order_number FROM work_orders w LEFT JOIN workspace_order_state s ON s.order_number=w.number ORDER BY w.number")))return fail(read.lastError().text());
    QList<int> missingState;
    while(read.next()){numbers.append(read.value(0).toInt());if(read.value(1).isNull())missingState.append(read.value(0).toInt());}
    if(read.lastError().isValid())return fail(read.lastError().text());
    read.finish();
    for(const int number:missingState)if(!Database::finalizeOrderWorkspaceInTransaction(source,number,errorText))return false;
    if(!tx.commit(QStringLiteral("Cannot commit registry bulk intentions"),errorText))return false;
    for(const int number:numbers)if(!Database::refreshOrderRegistry(number,errorText))return false;
    TransactionContext metaTx(registry,QStringLiteral("RegistryBulkMetadata"));
    if(!metaTx.begin(QStringLiteral("Cannot start registry metadata publication"),errorText))return false;
    const auto metaTxRollback=qScopeGuard([&]{if(metaTx.isActive()){QString ignored;metaTx.rollback(QString(),&ignored);}});
    const QString archivePath=QFileInfo::exists(AppRuntime::archiveDbFilePath())?AppRuntime::archiveDbFilePath():QStringLiteral("");
    const QList<QPair<QString,QString>> metadata{{QStringLiteral("active_path"),AppRuntime::activeDbFilePath()},{QStringLiteral("archive_path"),archivePath},{QStringLiteral("runtime_bulk_pending"),QStringLiteral("0")}};
    for(const auto& entry:metadata) {
        QSqlQuery meta(registry);
        if(!meta.prepare(QStringLiteral("INSERT INTO registry_meta(key,value) VALUES(?,?) ON CONFLICT(key) DO UPDATE SET value=excluded.value")))return fail(meta.lastError().text());
        meta.addBindValue(entry.first);meta.addBindValue(entry.second);
        if(!meta.exec())return fail(meta.lastError().text());
        if(meta.numRowsAffected()!=1)return fail(QStringLiteral("Registry metadata publication was not recorded"));
    }
    return metaTx.commit(QStringLiteral("Cannot commit registry metadata publication"),errorText);
}

static bool importLegacyArchiveIntoActive()
