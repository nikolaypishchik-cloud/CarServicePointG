// Extracted from src/infrastructure/db/sqlite/DbArchiveRoutingStageSupport.cpp for P1/P2 monolith reduction.
// Fragment boundary is function-safe; do not split inside macro calls, raw strings or SQL literals.

static bool clearLocalRegistryMirror(QSqlDatabase database, QString* errorText)
{
    if (!ensureLocalRegistryMirrorSchema(database)) {
        if (errorText) *errorText = QStringLiteral("Local registry mirror schema is unavailable");
        return false;
    }
    QSqlQuery q(database);
    if (!q.exec(QStringLiteral("DELETE FROM order_registry"))) {
        if (errorText) *errorText = q.lastError().text();
        return false;
    }
    return true;
}

static bool clearRegistryFileDerivedTables(QString* errorText)
{
    if (!ensurePartRegistrySchemaFile()) {
        if (errorText) *errorText = QStringLiteral("Registry DB is unavailable");
        return false;
    }
    QSqlDatabase r = QSqlDatabase::database(dbRegistryConnectionName());
    if (!r.isOpen() && !r.open()) {
        if (errorText) *errorText = r.lastError().text();
        return false;
    }

    MutationExecutor::Request request;
    request.sourceModule = QStringLiteral("DbArchiveRoutingSupport.clearRegistryFileDerivedTables");
    request.entityType = QStringLiteral("registry_file");
    request.entityId = QStringLiteral("derived_tables");
    request.operation = QStringLiteral("clear");
    request.changedTables = {QStringLiteral("part_registry"), QStringLiteral("order_registry")};
    request.beginErrorFallback = QStringLiteral("Failed to start registry-file clear transaction");
    request.commitErrorFallback = QStringLiteral("Failed to commit registry-file clear transaction");

    const auto tx = MutationExecutor::execute(r, request, [&](QSqlDatabase& txDb, QString* txError) {
        QSqlQuery q(txDb);
        if (!q.exec(QStringLiteral("DELETE FROM part_registry"))) {
            if (txError) *txError = q.lastError().text();
            return false;
        }
        if (!q.exec(QStringLiteral("DELETE FROM order_registry"))) {
            if (txError) *txError = q.lastError().text();
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

bool importArchivedRegistryFromDbPath(const QString& dbPath, QString* errorText, QSet<QString>* importedUids)
{
    const QString normalizedPath = dbPath.trimmed();
    if (normalizedPath.isEmpty() || !QFileInfo::exists(normalizedPath)) {
        if(errorText)*errorText=QStringLiteral("Archive source path is empty or missing: %1").arg(normalizedPath);
        return false;
    }
    if (!ensurePartRegistrySchemaFile()) {
        if (errorText) *errorText = QStringLiteral("Registry DB is unavailable");
        return false;
    }

    const QString connName = QStringLiteral("carservice_registry_rebuild_%1")
        .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    QSet<QString> verifiedUids;
    bool ok = true;
    QString localError;
    bool reading=false,publishing=false;
    auto target=QSqlDatabase::database(dbRegistryConnectionName(),false);
    const auto rollback=qScopeGuard([&]{if(publishing){QSqlQuery q(target);if(!q.exec("ROLLBACK TO archive_file_publication"))qCritical()<<"Archive file rollback failed"<<q.lastError().text();if(!q.exec("RELEASE archive_file_publication"))qCritical()<<"Archive file release failed"<<q.lastError().text();}});
    {
        QSqlDatabase source = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
        source.setDatabaseName(normalizedPath);
        source.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        DbThreadConnectionSupport::registerConnectionName(connName,normalizedPath);
        if (!source.open()) {
            localError = source.lastError().text();
            ok = false;
        } else if (!tableExistsInDb(source, QStringLiteral("work_orders"))
                   || !tableExistsInDb(source,QStringLiteral("work_order_services"))
                   || !tableExistsInDb(source,QStringLiteral("work_order_parts"))) {
            localError=QStringLiteral("Archive source is not a prepared canonical archive");ok=false;
        } else if(!(reading=source.transaction())) {
            localError=source.lastError().text();ok=false;
        } else if(!validateArchiveShardIdentity(source,&localError)) {
            ok=false;
        } else {
            QSqlQuery publication(target);
            if(!publication.exec("SAVEPOINT archive_file_publication")){localError=publication.lastError().text();ok=false;}
            else {
            publishing=true;
            QSqlDatabase registryDb = QSqlDatabase::database(dbRegistryConnectionName());
            const QSet<QString> archiveOrderCols = tableColumnsForDb(source, QStringLiteral("work_orders"));
            auto archiveExpr = [&](const QString& columnName, const QString& fallbackExpr) {
                return archiveOrderCols.contains(columnName)
                    ? QStringLiteral("COALESCE(%1,%2)").arg(sqlQuoteIdent(columnName), fallbackExpr)
                    : fallbackExpr;
            };
            const QString orderUidExpr = archiveExpr(QStringLiteral("order_uid"), QStringLiteral("''"));
            const QString visibleOrderExpr = archiveOrderCols.contains(QStringLiteral("order_number"))
                ? QStringLiteral("COALESCE(order_number, number)")
                : QStringLiteral("COALESCE(number,0)");
            const QString orderYearExpr = archiveOrderCols.contains(QStringLiteral("order_year"))
                ? QStringLiteral("COALESCE(NULLIF(order_year,0), CAST(strftime('%Y', substr(COALESCE(opened_at,''),1,10)) AS INTEGER), CAST(strftime('%Y','now') AS INTEGER))")
                : QStringLiteral("CAST(strftime('%Y', substr(COALESCE(opened_at,''),1,10)) AS INTEGER)");
            const QString readSql = QStringLiteral(
                    "SELECT COALESCE(number,0), %1, %2, %3, %4, %5, %6, %7, %8, %9, %10, %11, %12, %13, %14, %15 "
                    "FROM work_orders")
                    .arg(orderUidExpr,
                         visibleOrderExpr,
                         orderYearExpr,
                         archiveExpr(QStringLiteral("status"), QStringLiteral("''")),
                         archiveExpr(QStringLiteral("opened_at"), QStringLiteral("''")),
                         archiveExpr(QStringLiteral("closed_at"), QStringLiteral("''")),
                         archiveExpr(QStringLiteral("plate"), QStringLiteral("''")),
                         archiveExpr(QStringLiteral("plate_norm"), QStringLiteral("''")),
                         archiveExpr(QStringLiteral("vin"), QStringLiteral("''")),
                         archiveExpr(QStringLiteral("car"), QStringLiteral("''")),
                         archiveExpr(QStringLiteral("client"), QStringLiteral("''")),
                         archiveExpr(QStringLiteral("master_id"), QStringLiteral("''")),
                         archiveExpr(QStringLiteral("master_name_closed"), QStringLiteral("''")),
                         archiveExpr(QStringLiteral("works_sum"), QStringLiteral("0")),
                         archiveExpr(QStringLiteral("parts_sum"), QStringLiteral("0")));
            QSqlQuery read(source);
            if (!read.exec(readSql)) {
                localError = read.lastError().text();
                ok = false;
            } else {
                QSqlQuery up(registryDb);
                up.prepare(QStringLiteral(
                    "INSERT INTO order_registry(order_number, order_uid, visible_order_number, order_year, storage_kind, storage_path, source_db, imported_at, last_seen_at, status, opened_at, closed_at, plate, plate_norm, vin, car, client, master_id, master_name_closed, works_sum, parts_sum) "
                    "VALUES(?, ?, ?, ?, 'archive', ?, ?, COALESCE((SELECT imported_at FROM order_registry WHERE order_number=?), ''), datetime('now'), ?,?,?,?,?,?,?,?,?,?,?,?) "
                    "ON CONFLICT(order_number) DO UPDATE SET "
                    "order_uid=CASE WHEN TRIM(COALESCE(excluded.order_uid,''))<>'' THEN excluded.order_uid ELSE order_registry.order_uid END, "
                    "visible_order_number=CASE WHEN COALESCE(excluded.visible_order_number,0)>0 THEN excluded.visible_order_number ELSE order_registry.visible_order_number END, "
                    "order_year=CASE WHEN COALESCE(excluded.order_year,0)>0 THEN excluded.order_year ELSE order_registry.order_year END, "
                    "storage_kind=excluded.storage_kind, storage_path=excluded.storage_path, source_db=excluded.source_db, "
                    "last_seen_at=excluded.last_seen_at, status=excluded.status, opened_at=excluded.opened_at, closed_at=excluded.closed_at, "
                    "plate=excluded.plate, plate_norm=excluded.plate_norm, vin=excluded.vin, car=excluded.car, client=excluded.client, "
                    "master_id=excluded.master_id, master_name_closed=excluded.master_name_closed, works_sum=excluded.works_sum, parts_sum=excluded.parts_sum"));

                const bool hasParts = tableExistsInDb(source, QStringLiteral("work_order_parts"));
                const QSet<QString> archivePartCols = hasParts
                    ? tableColumnsForDb(source, QStringLiteral("work_order_parts"))
                    : QSet<QString>();
                while (read.next()) {
                    const int orderNumber = read.value(0).toInt();
                    const QString orderUid = nonNullVariantText(read.value(1));
                    const int visibleOrderNumber = read.value(2).toInt();
                    const int orderYear = read.value(3).toInt();
                    const QString status = nonNullVariantText(read.value(4));
                    const QString openedAt = nonNullVariantText(read.value(5));
                    const QString closedAt = nonNullVariantText(read.value(6));
                    const QString plate = nonNullVariantText(read.value(7));
                    const QString plateNorm = nonNullVariantText(read.value(8));
                    const QString vin = nonNullVariantText(read.value(9));
                    const QString car = nonNullVariantText(read.value(10));
                    const QString client = nonNullVariantText(read.value(11));
                    const QString masterId = nonNullVariantText(read.value(12));
                    const QString masterNameClosed = nonNullVariantText(read.value(13));
                    const double worksSum = read.value(14).toDouble();
                    const double partsSum = read.value(15).toDouble();

                    verifiedUids.insert(orderUid);
                    QSqlQuery owner(registryDb);
                    if (!owner.prepare(QStringLiteral("SELECT order_uid,visible_order_number,order_year,storage_kind FROM order_registry WHERE order_number=?"))) {
                        localError=owner.lastError().text();ok=false;break;
                    }
                    owner.addBindValue(orderNumber);
                    if (!owner.exec()) {localError=owner.lastError().text();ok=false;break;}
                    if (owner.next()) {
                        if (owner.value(0).toString()!=orderUid || owner.value(1).toInt()!=visibleOrderNumber || owner.value(2).toInt()!=orderYear) {
                            localError=QStringLiteral("Archive publication conflicts with recorded order identity");ok=false;break;
                        }
                        if (owner.value(3).toString()==QStringLiteral("active")) continue;
                    } else if (owner.lastError().isValid()) {localError=owner.lastError().text();ok=false;break;}

                    up.addBindValue(orderNumber);
                    up.addBindValue(orderUid);
                    up.addBindValue(visibleOrderNumber);
                    up.addBindValue(orderYear);
                    up.addBindValue(nonNullText(normalizedPath));
                    up.addBindValue(nonNullText(normalizedPath));
                    up.addBindValue(orderNumber);
                    up.addBindValue(status);
                    up.addBindValue(openedAt);
                    up.addBindValue(closedAt);
                    up.addBindValue(plate);
                    up.addBindValue(plateNorm);
                    up.addBindValue(vin);
                    up.addBindValue(car);
                    up.addBindValue(client);
                    up.addBindValue(masterId);
                    up.addBindValue(masterNameClosed);
                    up.addBindValue(worksSum);
                    up.addBindValue(partsSum);
                    if (!up.exec() || up.numRowsAffected()!=1) {
                        localError = QStringLiteral("Archive header publication failed: %1").arg(up.lastError().text());
                        ok = false;
                        break;
                    }

                    // Retire only projections with a verified immutable owner in this archive.
                    QSqlQuery previous(registryDb);
                    if (!previous.prepare(QStringLiteral("SELECT order_number,order_uid,part_uid,part_ref FROM part_registry WHERE storage_kind='archive' AND storage_path=? AND (order_number=? OR order_uid=?)"))) {
                        localError=previous.lastError().text();ok=false;break;
                    }
                    previous.addBindValue(normalizedPath);previous.addBindValue(orderNumber);previous.addBindValue(orderUid);
                    if (!previous.exec()) {localError=previous.lastError().text();ok=false;break;}
                    while (previous.next()) {
                        if (previous.value(0).toInt()!=orderNumber || previous.value(1).toString()!=orderUid || previous.value(2).toString().trimmed().isEmpty()) {
                            localError=QStringLiteral("Archive part projection has unknown or conflicting ownership");ok=false;break;
                        }
                        QSqlQuery identity(source);
                        if (!identity.prepare(QStringLiteral("SELECT id,order_uid FROM work_order_parts WHERE part_uid=?"))) {localError=identity.lastError().text();ok=false;break;}
                        identity.addBindValue(previous.value(2));
                        if (!identity.exec()) {localError=identity.lastError().text();ok=false;break;}
                        if (identity.next()) {
                            if (identity.value(0).toInt()!=previous.value(3).toInt() || identity.value(1).toString()!=orderUid) {
                                localError=QStringLiteral("Archive part projection conflicts with source UID");ok=false;break;
                            }
                        } else if (identity.lastError().isValid()) {localError=identity.lastError().text();ok=false;break;}
                    }
                    if (previous.lastError().isValid()) {localError=previous.lastError().text();ok=false;}
                    previous.finish();
                    if (!ok) break;
                    if (!removePartRegistryFileRows(orderNumber,QStringLiteral("archive"),normalizedPath,false)) {
                        localError=QStringLiteral("Failed to replace verified archive part projections");ok=false;break;
                    }
                    if (!hasParts) continue;
                    auto archivePartExpr = [&](const QString& columnName, const QString& fallbackExpr) {
                        return archivePartCols.contains(columnName)
                            ? QStringLiteral("COALESCE(%1,%2)").arg(sqlQuoteIdent(columnName), fallbackExpr)
                            : fallbackExpr;
                    };
                    const QString partRefExpr = archivePartCols.contains(QStringLiteral("id"))
                        ? QStringLiteral("COALESCE(id,0)")
                        : QStringLiteral("0");
                    const QString orderByExpr = archivePartCols.contains(QStringLiteral("id"))
                        ? QStringLiteral("id ASC")
                        : QStringLiteral("rowid ASC");
                    QSqlQuery partQ(source);
                    partQ.prepare(QStringLiteral(
                        "SELECT %1, %2, %3, %4, %5, %6, part_uid, order_uid "
                        "FROM work_order_parts WHERE order_number=? ORDER BY %7")
                        .arg(partRefExpr,
                             archivePartExpr(QStringLiteral("ordered_at"), QStringLiteral("''")),
                             archivePartExpr(QStringLiteral("delivery_date"), QStringLiteral("''")),
                             archivePartExpr(QStringLiteral("part_number"), QStringLiteral("''")),
                             archivePartExpr(QStringLiteral("name"), QStringLiteral("''")),
                             archivePartExpr(QStringLiteral("note"), QStringLiteral("''")),
                             orderByExpr));
                    partQ.addBindValue(orderNumber);
                    if (!partQ.exec()) {
                        localError = partQ.lastError().text();
                        ok = false;
                        break;
                    }
                    while (partQ.next()) {
                        if (partQ.value(7).toString()!=orderUid || partQ.value(6).toString().trimmed().isEmpty()) {
                            localError=QStringLiteral("Archive part source identity changed");ok=false;break;
                        }
                        const int partRef = partQ.value(0).toInt();
                        const QString orderedAt = partQ.value(1).toString();
                        const QString deliveryDate = partQ.value(2).toString();
                        const QString partNumber = partQ.value(3).toString();
                        const QString name = partQ.value(4).toString();
                        const QString note = partQ.value(5).toString();
                        const QString entryKey = makePartRegistryEntryKey(QStringLiteral("archive"), normalizedPath, orderNumber, partRef, orderedAt, partNumber, name);
                        if (!upsertPartRegistryFileRow(entryKey, orderNumber, partRef, QStringLiteral("archive"), normalizedPath, normalizedPath,
                                                       orderedAt, deliveryDate, partNumber, name, note,
                                                       plate, plateNorm, vin, car, client, false, orderUid, partQ.value(6).toString())) {
                            localError = QStringLiteral("Failed to upsert archived part registry rows from %1").arg(normalizedPath);
                            ok = false;
                            break;
                        }
                    }
                    if (partQ.lastError().isValid()) {localError=partQ.lastError().text();ok=false;}
                    if (!ok) break;
                }
                if (read.lastError().isValid()) {localError=read.lastError().text();ok=false;}
            }
            }
        }
        if(reading && !(ok?source.commit():source.rollback())){localError=source.lastError().text();ok=false;}
        source.close();
    }
    SqliteConnectionUtils::safeCloseAndRemoveDatabase(connName);
    if(ok && publishing){QSqlQuery publication(target);if(!publication.exec("RELEASE archive_file_publication")){localError=publication.lastError().text();ok=false;}else publishing=false;}
    if (!ok && errorText) *errorText = localError;
    if(ok && importedUids)*importedUids=verifiedUids;
    return ok;
}

bool syncRegistryProjectionInOwnedTransaction(QSqlDatabase database, QString* errorText)
{
    auto fail=[&](const QString& message){if(errorText)*errorText=message;return false;};
    auto registry=QSqlDatabase::database(dbRegistryConnectionName(),false);
    if(!TransactionContext::hasActiveTransaction(database) || !TransactionContext::hasActiveTransaction(registry))
        return fail(QStringLiteral("Registry mirror projection requires source and target transaction owners"));
    // Every recorded identity must remain represented before any mirror row changes.
    QSqlQuery previous(database);
    if(!previous.exec("SELECT order_number,order_uid,visible_order_number,order_year FROM order_registry"))return fail(previous.lastError().text());
    while(previous.next()) {
        QSqlQuery owner(registry);
        if(!owner.prepare("SELECT order_uid,visible_order_number,order_year FROM order_registry WHERE order_number=?"))return fail(owner.lastError().text());
        owner.addBindValue(previous.value(0));
        if(!owner.exec())return fail(owner.lastError().text());
        if(!owner.next() || owner.value(0)!=previous.value(1) || owner.value(1)!=previous.value(2) || owner.value(2)!=previous.value(3))
            return fail(QStringLiteral("Local registry identity is missing or conflicts with the prepared projection"));
    }
    if(previous.lastError().isValid())return fail(previous.lastError().text());
    previous.finish();
    QSqlQuery read(registry);
    if(!read.exec("SELECT order_number,order_uid,visible_order_number,order_year,storage_kind,storage_path,source_db,imported_at,status,opened_at,closed_at,plate,plate_norm,vin,car,client,master_id,master_name_closed,works_sum,parts_sum FROM order_registry ORDER BY order_number"))return fail(read.lastError().text());
    while(read.next()) {
        if(!upsertRegistryMirrorRow(database,read.value(0).toInt(),read.value(1).toString(),read.value(2).toInt(),read.value(3).toInt(),
            read.value(4).toString(),read.value(5).toString(),read.value(6).toString(),read.value(7).toString(),read.value(8).toString(),
            read.value(9).toString(),read.value(10).toString(),read.value(11).toString(),read.value(12).toString(),read.value(13).toString(),
            read.value(14).toString(),read.value(15).toString(),read.value(16).toString(),read.value(17).toString(),
            read.value(18).toDouble(),read.value(19).toDouble(),false))return fail(QStringLiteral("Prepared registry mirror row was not written"));
    }
    if(read.lastError().isValid())return fail(read.lastError().text());
    return true;
}

bool rebuildRegistryDerivedState(QSqlDatabase database, QString* errorText)
{
    auto fail=[&](const QString& message){if(errorText)*errorText=message;return false;};
    if(database.connectionName()!=Database::db().connectionName())return fail(QStringLiteral("Registry rebuild requires the runtime workspace"));
    if(!validateStartupRegistryIdentity(database,AppRuntime::registryDbFilePath(),errorText))return false;
    if(!ensurePartRegistrySchemaFile())return fail(QStringLiteral("Registry schema unavailable"));
    auto registry=QSqlDatabase::database(dbRegistryConnectionName(),false);
    TransactionContext sourceTx(database,QStringLiteral("RegistryRebuildSource")),targetTx(registry,QStringLiteral("RegistryRebuildTarget"));
    if(!sourceTx.begin(QStringLiteral("Cannot reserve rebuild source"),errorText))return false;
    const auto rollback=qScopeGuard([&]{QString detail;if(targetTx.isActive() && !targetTx.rollback(QString(),&detail))qCritical()<<"Registry rebuild target rollback failed"<<detail;if(sourceTx.isActive() && !sourceTx.rollback(QString(),&detail))qCritical()<<"Registry rebuild source rollback failed"<<detail;});
    if(!targetTx.begin(QStringLiteral("Cannot reserve rebuild target"),errorText))return false;
    QSet<QString> paths;
    const QString archive=AppRuntime::archiveDbFilePath();
    if(QFileInfo::exists(archive))paths.insert(archive);
    QDir directory(QDir(AppRuntime::dataDir()).filePath(QStringLiteral("archive_shards")));
    for(const auto& name:directory.entryList({QStringLiteral("orders_*.db")},QDir::Files,QDir::Name))paths.insert(directory.filePath(name));
    QSqlQuery references(registry);
    if(!references.exec("SELECT DISTINCT storage_path FROM order_registry WHERE storage_kind='archive'"))return fail(references.lastError().text());
    while(references.next())paths.insert(references.value(0).toString());
    if(references.lastError().isValid())return fail(references.lastError().text());
    references.finish();
    QSet<QString> canonicalPaths;
    for(const auto& path:paths) {
        const QString canonical=QFileInfo(path).canonicalFilePath();
        if(canonical.isEmpty())return fail(QStringLiteral("Recorded archive source is missing: %1").arg(path));
        canonicalPaths.insert(canonical);
        if(path!=canonical) {
            QSqlQuery normalize(registry);
            if(!normalize.prepare("UPDATE part_registry SET storage_path=? WHERE storage_kind='archive' AND storage_path=?"))return fail(normalize.lastError().text());
            normalize.addBindValue(canonical);normalize.addBindValue(path);
            if(!normalize.exec())return fail(normalize.lastError().text());
            QSqlQuery remaining(registry);
            if(!remaining.prepare("SELECT 1 FROM part_registry WHERE storage_kind='archive' AND storage_path=? LIMIT 1"))return fail(remaining.lastError().text());
            remaining.addBindValue(path);
            if(!remaining.exec() || remaining.next() || remaining.lastError().isValid())return fail(QStringLiteral("Archive projection path normalization did not complete"));
        }
    }
    paths=canonicalPaths;
    QSet<QString> nativeUids,seen;
    QMap<QString,QString> workingArchiveSources,verifiedArchiveSources;
    QList<int> numbers,nativeNumbers,archiveWorkingNumbers;
    QSqlQuery orders(database);
    if(!orders.exec("SELECT w.number,w.order_uid,s.origin,s.source_db FROM work_orders w LEFT JOIN workspace_order_state s ON s.order_number=w.number ORDER BY w.number"))return fail(orders.lastError().text());
    while(orders.next()) {
        const int number=orders.value(0).toInt();const QString uid=orders.value(1).toString();
        numbers.append(number);seen.insert(uid);
        if(orders.value(2).toString()==QStringLiteral("native")){nativeNumbers.append(number);nativeUids.insert(uid);}
        else {
            if(orders.value(2).toString()!=QStringLiteral("legacy_copy"))return fail(QStringLiteral("Workspace origin requires reconciliation before rebuild"));
            archiveWorkingNumbers.append(number);workingArchiveSources.insert(uid,orders.value(3).toString());
        }
    }
    if(orders.lastError().isValid())return fail(orders.lastError().text());
    orders.finish();
    for(const int number:nativeNumbers)if(!publishCommittedOrderRegistryProjection(database,number,errorText))return false;
    QStringList sortedPaths=paths.values();sortedPaths.sort();
    QSet<QString> archiveUids;
    for(const auto& path:sortedPaths) {
        QSet<QString> imported;
        if(!importArchivedRegistryFromDbPath(path,errorText,&imported))return false;
        for(const auto& uid:imported) {
            if(archiveUids.contains(uid) && !nativeUids.contains(uid))return fail(QStringLiteral("Multiple physical archives claim the same order UID"));
            archiveUids.insert(uid);seen.insert(uid);verifiedArchiveSources.insert(uid,path);
        }
    }
    for(auto it=workingArchiveSources.cbegin();it!=workingArchiveSources.cend();++it) {
        const QString recorded=QFileInfo(it.value()).canonicalFilePath();
        const QString verified=QFileInfo(verifiedArchiveSources.value(it.key())).canonicalFilePath();
        if(recorded.isEmpty() || verified.isEmpty() || recorded!=verified)
            return fail(QStringLiteral("Archive-backed workspace UID is absent from its recorded source; explicit reconciliation required"));
    }
    // A verified archive-backed workspace may contain newer committed edits.
    for(const int number:archiveWorkingNumbers)if(!publishCommittedOrderRegistryProjection(database,number,errorText))return false;
    QSqlQuery coverage(registry);
    if(!coverage.exec("SELECT order_uid FROM order_registry"))return fail(coverage.lastError().text());
    while(coverage.next())if(!seen.contains(coverage.value(0).toString()))return fail(QStringLiteral("Recorded registry identity has no verified source; explicit reconciliation required"));
    if(coverage.lastError().isValid())return fail(coverage.lastError().text());
    coverage.finish();
    if(!coverage.exec("SELECT 1 FROM part_registry p LEFT JOIN order_registry o ON o.order_number=p.order_number AND o.order_uid=p.order_uid WHERE o.order_number IS NULL OR TRIM(p.part_uid)='' LIMIT 1"))return fail(coverage.lastError().text());
    if(coverage.next())return fail(QStringLiteral("Unresolved part projection ownership requires reconciliation"));
    if(coverage.lastError().isValid())return fail(coverage.lastError().text());
    coverage.finish();
    if(!syncRegistryProjectionInOwnedTransaction(database,errorText))return false;
    for(const int number:numbers) {
        QSqlQuery ack(database);
        if(!ack.prepare("UPDATE workspace_order_state SET published_registry_revision=registry_revision WHERE order_number=? AND order_uid=(SELECT order_uid FROM work_orders WHERE number=?)"))return fail(ack.lastError().text());
        ack.addBindValue(number);ack.addBindValue(number);
        if(!ack.exec() || ack.numRowsAffected()!=1)return fail(QStringLiteral("Rebuild source acknowledgement failed: %1").arg(ack.lastError().text()));
    }
    QSqlQuery pending(registry);
    if(!pending.exec("INSERT INTO registry_meta(key,value) VALUES('registry_rebuild_pending','1') ON CONFLICT(key) DO UPDATE SET value='1'") || pending.numRowsAffected()!=1)return fail(QStringLiteral("Rebuild recovery marker was not recorded"));
    pending.finish();
    if(!targetTx.commit(QStringLiteral("Registry rebuild publication failed"),errorText))return false;
    if(!sourceTx.commit(QStringLiteral("Registry rebuild mirror commit failed"),errorText))return false;
    TransactionContext completed(registry,QStringLiteral("RegistryRebuildCompleted"));
    if(!completed.begin(QStringLiteral("Cannot complete registry rebuild"),errorText))return false;
    const auto completionRollback=qScopeGuard([&]{QString detail;if(completed.isActive() && !completed.rollback(QString(),&detail))qCritical()<<"Registry rebuild completion rollback failed"<<detail;});
    if(!pending.exec("UPDATE registry_meta SET value='0' WHERE key='registry_rebuild_pending'") || pending.numRowsAffected()!=1)return fail(QStringLiteral("Rebuild recovery marker was not cleared"));
    pending.finish();
    return completed.commit(QStringLiteral("Cannot commit registry rebuild completion"),errorText);
}

QString makePartRegistryEntryKey(const QString& storageKind, const QString& storagePath, int orderNumber, int partRef,
                                        const QString& orderedAt, const QString& partNumber, const QString& name)
{
    QString base = QStringLiteral("%1|%2|%3|%4|%5|%6|%7")
        .arg(nonNullText(storageKind), nonNullText(storagePath), QString::number(orderNumber), QString::number(partRef),
             nonNullText(orderedAt), nonNullText(partNumber), nonNullText(name));
    base.replace(QLatin1Char('\n'), QLatin1Char(' '));
    return base;
}

bool upsertPartRegistryFileRow(const QString& entryKey, int orderNumber, int partRef,
                                      const QString& storageKind, const QString& storagePath, const QString& sourceDb,
                                      const QString& orderedAt, const QString& deliveryDate,
                                      const QString& partNumber, const QString& name, const QString& note,
                                      const QString& plate, const QString& plateNorm, const QString& vin,
                                      const QString& car, const QString& client, bool prepareSchema, const QString& orderUid, const QString& partUid)
{
    if (prepareSchema && !ensurePartRegistrySchemaFile()) return false;
    QSqlDatabase d = QSqlDatabase::database(dbRegistryConnectionName(),false);
    if(orderUid.trimmed().isEmpty()!=partUid.trimmed().isEmpty())return false;
    if(!partUid.trimmed().isEmpty()) {
        QSqlQuery owner(d);
        if(!owner.prepare(QStringLiteral("SELECT 1 FROM part_registry WHERE storage_kind=? AND storage_path=? AND part_uid=? AND (order_uid<>? OR order_number<>? OR part_ref<>?) LIMIT 1")))return false;
        owner.addBindValue(storageKind);owner.addBindValue(storagePath);owner.addBindValue(partUid);owner.addBindValue(orderUid);owner.addBindValue(orderNumber);owner.addBindValue(partRef);
        if(!owner.exec() || owner.next() || owner.lastError().isValid())return false;
    }
    QSqlQuery q(d);
    if (!q.prepare(QStringLiteral(
        "INSERT INTO part_registry(entry_key, order_number, part_ref, storage_kind, storage_path, source_db, ordered_at, delivery_date, part_number, name, note, plate, plate_norm, vin, car, client, order_uid, part_uid) "
        "VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(entry_key) DO UPDATE SET "
        "order_number=excluded.order_number, part_ref=excluded.part_ref, storage_kind=excluded.storage_kind, storage_path=excluded.storage_path, "
        "source_db=excluded.source_db, ordered_at=excluded.ordered_at, delivery_date=excluded.delivery_date, part_number=excluded.part_number, "
        "name=excluded.name, note=excluded.note, plate=excluded.plate, plate_norm=excluded.plate_norm, vin=excluded.vin, car=excluded.car, client=excluded.client WHERE part_registry.order_uid=excluded.order_uid AND part_registry.part_uid=excluded.part_uid"))) return false;
    q.addBindValue(nonNullText(entryKey));
    q.addBindValue(orderNumber);
    q.addBindValue(partRef);
    q.addBindValue(nonNullText(storageKind));
    q.addBindValue(nonNullText(storagePath));
    q.addBindValue(nonNullText(sourceDb));
    q.addBindValue(nonNullText(orderedAt));
    q.addBindValue(nonNullText(deliveryDate));
    q.addBindValue(nonNullText(partNumber));
    q.addBindValue(nonNullText(name));
    q.addBindValue(nonNullText(note));
    q.addBindValue(nonNullText(plate));
    q.addBindValue(nonNullText(plateNorm));
    q.addBindValue(nonNullText(vin));
    q.addBindValue(nonNullText(car));
    q.addBindValue(nonNullText(client));
    q.addBindValue(nonNullText(orderUid));q.addBindValue(nonNullText(partUid));
    if (!q.exec() || q.numRowsAffected()!=1) {
        qWarning() << "part_registry upsert failed for order" << orderNumber << q.lastError().text();
        return false;
    }
    return true;
}

bool removePartRegistryFileRows(int orderNumber, const QString& storageKind, const QString& storagePath, bool prepareSchema)
{
    if (prepareSchema && !ensurePartRegistrySchemaFile()) return false;
    QSqlDatabase d=QSqlDatabase::database(dbRegistryConnectionName(),false);
    QString where=QStringLiteral(" WHERE order_number=?");
    QList<QVariant> values{orderNumber};
    if (!storageKind.trimmed().isEmpty()) {where+=QStringLiteral(" AND storage_kind=?"); values<<storageKind.trimmed();}
    if (!storagePath.trimmed().isEmpty()) {where+=QStringLiteral(" AND storage_path=?"); values<<storagePath.trimmed();}
    QSqlQuery remove(d);
    if (!remove.prepare(QStringLiteral("DELETE FROM part_registry")+where)) return false;
    for (const auto& value:values) remove.addBindValue(value);
    if (!remove.exec()) {qWarning()<<"part_registry delete failed for order"<<orderNumber<<remove.lastError().text();return false;}
    QSqlQuery remaining(d);
    if (!remaining.prepare(QStringLiteral("SELECT 1 FROM part_registry")+where+QStringLiteral(" LIMIT 1"))) return false;
    for (const auto& value:values) remaining.addBindValue(value);
    if (!remaining.exec() || remaining.next() || remaining.lastError().isValid()) {
        qWarning()<<"part_registry delete did not clear expected rows for order"<<orderNumber<<remaining.lastError().text();return false;
    }
    return true;
}

static QString archiveShardsDirPath()
{
    QDir d(AppRuntime::dataDir());
    d.mkpath(QStringLiteral("archive_shards"));
    d.cd(QStringLiteral("archive_shards"));
    AppRuntime::protectPathAtRest(d.absolutePath());
    return d.absolutePath();
}

QString archiveShardFilePathForOrder(int orderNumber)
{
    const int bucketStart = qMax(1, ((qMax(1, orderNumber) - 1) / 1000) * 1000 + 1);
    const int bucketEnd = bucketStart + 999;
    QDir d(archiveShardsDirPath());
    return d.filePath(QStringLiteral("orders_%1_%2.db")
                      .arg(bucketStart, 6, 10, QChar('0'))
                      .arg(bucketEnd, 6, 10, QChar('0')));
}

bool ensureArchiveShardSchemaByPath(const QString& dbPath)
{
    if(dbPath.trimmed().isEmpty())return false;
    const bool existed=QFileInfo::exists(dbPath);
    if(!QDir().mkpath(QFileInfo(dbPath).absolutePath()))return false;
    const QString connName=QStringLiteral("archive_schema_%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    const auto close=qScopeGuard([&]{SqliteConnectionUtils::safeCloseAndRemoveDatabase(connName);});
    auto d=QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),connName);
    d.setDatabaseName(dbPath);DbThreadConnectionSupport::registerConnectionName(connName,dbPath);
    if(!d.open()){qWarning()<<"Archive shard open failed"<<d.lastError().text();return false;}
    auto run=[&](const QString& sql){QSqlQuery q(d);if(q.exec(sql))return true;qWarning()<<"Archive schema SQL failed"<<q.lastError().text();return false;};
    // Preserve an existing archive's journal mode. Only a new empty file is initialized.
    if(!run("PRAGMA busy_timeout=5000") || !run("PRAGMA synchronous=FULL"))return false;
    if(!existed && !run("PRAGMA journal_mode=WAL"))return false;
    if(!run("BEGIN IMMEDIATE"))return false;
    bool active=true;
    const auto rollback=qScopeGuard([&]{if(active && !run("ROLLBACK"))qCritical()<<"Archive schema rollback failed";});
    QString identityError;
    if(!validateArchiveShardIdentity(d,&identityError)){qWarning()<<identityError;return false;}
    const bool ok=
run(
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
                ");") &&
            run(
                "CREATE TABLE IF NOT EXISTS work_order_services ("
                "  id INTEGER PRIMARY KEY,"
                "  order_uid TEXT NOT NULL DEFAULT '',"
                "  order_number INTEGER NOT NULL DEFAULT 0,"
                "  title TEXT NOT NULL,"
                "  qty REAL NOT NULL DEFAULT 1,"
                "  price REAL NOT NULL DEFAULT 0,"
                "  amount REAL NOT NULL DEFAULT 0"
                ");") &&
            run(
                "CREATE TABLE IF NOT EXISTS service_masters ("
                "  service_id INTEGER NOT NULL,"
                "  master_id TEXT NOT NULL,"
                "  share_percent REAL NOT NULL DEFAULT 100,"
                "  role TEXT NOT NULL DEFAULT 'master',"
                "  master_name_closed TEXT NOT NULL DEFAULT '',"
                "  PRIMARY KEY(service_id, master_id)"
                ");") &&
            run(
                "CREATE TABLE IF NOT EXISTS work_order_parts ("
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
                "  pricing_calc_version INTEGER NOT NULL DEFAULT 1"
                ");");
    if(!ok)return false;
    auto column=[&](const QString& table,const QString& name,const QString& definition){
        return tableColumnsForDb(d,table).contains(name) || run(QStringLiteral("ALTER TABLE %1 ADD COLUMN %2").arg(sqlQuoteIdent(table),definition));
    };
    if(!column("work_orders","discount_mode","discount_mode TEXT NOT NULL DEFAULT 'legacy'"))return false;
    if(!column("work_orders","order_uid","order_uid TEXT NOT NULL DEFAULT ''"))return false;
    if(!column("work_orders","order_number","order_number INTEGER NOT NULL DEFAULT 0"))return false;
    if(!column("work_orders","order_year","order_year INTEGER NOT NULL DEFAULT 0"))return false;
    for(const auto& table:QStringList{QStringLiteral("work_order_services"),QStringLiteral("work_order_parts")}) {
        if(!column(table,"order_uid","order_uid TEXT NOT NULL DEFAULT ''"))return false;
    }
    if(!column("work_order_parts","part_uid","part_uid TEXT NOT NULL DEFAULT ''"))return false;
    if(!column("work_order_parts","installed_at","installed_at TEXT NOT NULL DEFAULT ''"))return false;
    if(!column("work_order_parts","manufacturer","manufacturer TEXT NOT NULL DEFAULT ''"))return false;
    if(!column("work_order_parts","manual_markup_override","manual_markup_override INTEGER NOT NULL DEFAULT 0"))return false;
    if(!column("work_order_parts","markup_source","markup_source TEXT NOT NULL DEFAULT 'legacy'"))return false;
    if(!column("work_order_parts","price_input_mode","price_input_mode TEXT NOT NULL DEFAULT 'unknown'"))return false;
    if(!column("work_order_parts","pricing_updated_at","pricing_updated_at TEXT NOT NULL DEFAULT ''"))return false;
    if(!column("work_order_parts","pricing_calc_version","pricing_calc_version INTEGER NOT NULL DEFAULT 1"))return false;
    if(!run("CREATE INDEX IF NOT EXISTS idx_arch_orders_status ON work_orders(status, opened_at)"))return false;
    if(!run("CREATE INDEX IF NOT EXISTS idx_arch_orders_order_number ON work_orders(order_number)"))return false;
    if(!run("CREATE INDEX IF NOT EXISTS idx_arch_orders_order_year ON work_orders(order_year)"))return false;
    if(!run("CREATE UNIQUE INDEX IF NOT EXISTS idx_arch_orders_uid_unique ON work_orders(order_uid)"))return false;
    if(!run("CREATE UNIQUE INDEX IF NOT EXISTS idx_arch_orders_year_number_unique ON work_orders(order_year, order_number)"))return false;
    if(!run("CREATE INDEX IF NOT EXISTS idx_arch_services_order_uid ON work_order_services(order_uid)"))return false;
    if(!run("CREATE INDEX IF NOT EXISTS idx_arch_services_order ON work_order_services(order_number)"))return false;
    if(!run("CREATE INDEX IF NOT EXISTS idx_arch_parts_order_uid ON work_order_parts(order_uid)"))return false;
    if(!run("CREATE INDEX IF NOT EXISTS idx_arch_parts_order ON work_order_parts(order_number)"))return false;
    if(!run("CREATE INDEX IF NOT EXISTS idx_arch_parts_partno ON work_order_parts(part_number)"))return false;
    if(!run("CREATE UNIQUE INDEX IF NOT EXISTS idx_arch_parts_part_uid_unique ON work_order_parts(part_uid) WHERE TRIM(part_uid)<>''"))return false;
    if(!run("COMMIT"))return false;
    active=false;
    AppRuntime::protectPathAtRest(dbPath);
    return true;
}

bool ensureLocalRegistryMirrorSchema(QSqlDatabase database)
{
    if (!database.isValid()) return false;
    if (!database.isOpen() && !database.open()) return false;
    QSqlQuery q(database);
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
        qWarning() << "Local registry mirror schema failed:" << q.lastError().text();
        return false;
    }
    const QSet<QString> registryCols = tableColumnsForDb(database, QStringLiteral("order_registry"));
    auto ensureRegistryColumn = [&](const QString& colName, const QString& ddl) {
        if (registryCols.contains(colName)) return true;
        QSqlQuery alter(database);
        return alter.exec(QStringLiteral("ALTER TABLE order_registry ADD COLUMN %1;").arg(ddl));
    };
    if (!ensureRegistryColumn(QStringLiteral("order_uid"), QStringLiteral("order_uid TEXT NOT NULL DEFAULT ''"))) return false;
    if (!ensureRegistryColumn(QStringLiteral("visible_order_number"), QStringLiteral("visible_order_number INTEGER NOT NULL DEFAULT 0"))) return false;
    if (!ensureRegistryColumn(QStringLiteral("order_year"), QStringLiteral("order_year INTEGER NOT NULL DEFAULT 0"))) return false;
    if (!ensureRegistryColumn(QStringLiteral("master_id"), QStringLiteral("master_id TEXT NOT NULL DEFAULT ''"))) return false;
    if (!ensureRegistryColumn(QStringLiteral("master_name_closed"), QStringLiteral("master_name_closed TEXT NOT NULL DEFAULT ''"))) return false;
    if (!ensureRegistryColumn(QStringLiteral("works_sum"), QStringLiteral("works_sum REAL NOT NULL DEFAULT 0"))) return false;
    if (!ensureRegistryColumn(QStringLiteral("parts_sum"), QStringLiteral("parts_sum REAL NOT NULL DEFAULT 0"))) return false;
    if (!q.exec("UPDATE order_registry SET order_uid=COALESCE((SELECT NULLIF(TRIM(COALESCE(w.order_uid,'')), '') FROM work_orders w WHERE w.number=order_registry.order_number), CASE WHEN COALESCE(order_number,0)>0 THEN 'rewrite_' || order_number ELSE '' END) WHERE TRIM(COALESCE(order_uid,''))=''")) return false;
    if (!q.exec("UPDATE order_registry SET visible_order_number=order_number WHERE COALESCE(visible_order_number,0)=0")) return false;
    if (!q.exec("UPDATE order_registry SET order_year=(SELECT COALESCE(NULLIF(w.order_year,0), CAST(strftime('%Y', substr(COALESCE(w.opened_at,''),1,10)) AS INTEGER), CAST(strftime('%Y','now') AS INTEGER)) FROM work_orders w WHERE w.number=order_registry.order_number) WHERE COALESCE(order_year,0)<=0")) return false;
    if (!q.exec("DROP INDEX IF EXISTS idx_local_registry_order_uid_unique")) return false;
    if (!q.exec("CREATE UNIQUE INDEX IF NOT EXISTS idx_local_registry_order_uid_unique_nonempty ON order_registry(order_uid) WHERE TRIM(COALESCE(order_uid,''))<>''")) return false;
    if (!q.exec("CREATE INDEX IF NOT EXISTS idx_local_registry_legacy_internal_order ON order_registry(order_number)")) return false;
    if (!q.exec("CREATE INDEX IF NOT EXISTS idx_local_registry_visible_year ON order_registry(order_year, visible_order_number)")) return false;
    if (!q.exec("CREATE INDEX IF NOT EXISTS idx_local_registry_visible_lookup ON order_registry(visible_order_number, order_year DESC, order_number DESC)")) return false;
    if (!q.exec("CREATE INDEX IF NOT EXISTS idx_local_registry_plate_norm ON order_registry(plate_norm)")) return false;
    if (!q.exec("CREATE INDEX IF NOT EXISTS idx_local_registry_status_opened ON order_registry(status, opened_at)")) return false;
    if (!q.exec("CREATE INDEX IF NOT EXISTS idx_local_registry_closed_at ON order_registry(closed_at)")) return false;
    return true;
}

bool upsertRegistryMirrorRow(QSqlDatabase database, int orderNumber, const QString& orderUid, int visibleOrderNumber, int orderYear,
                                    const QString& storageKind, const QString& storagePath,
                                    const QString& sourceDb, const QString& importedAt, const QString& status,
                                    const QString& openedAt, const QString& closedAt, const QString& plate,
                                    const QString& plateNorm, const QString& vin, const QString& car, const QString& client,
                                    const QString& masterId, const QString& masterNameClosed, double worksSum, double partsSum, bool prepareSchema)
