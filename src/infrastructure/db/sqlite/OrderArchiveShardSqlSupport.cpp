#include "infrastructure/db/sqlite/OrderArchiveShardSqlSupport.h"

#include "runtime/AppRuntime.h"
#include "core/services/MutationExecutor.h"
#include "core/services/OrderStoragePolicyService.h"
#include "infrastructure/db/sqlite/Db.h"
#include "infrastructure/db/sqlite/DbArchiveInternals.h"
#include "infrastructure/db/sqlite/DbFacadeSupport.h"
#include "infrastructure/db/sqlite/DbContext.h"
#include "infrastructure/db/sqlite/DbWorkspaceOrderStateSupport.h"

#include <QList>
#include <QMap>
#include <QScopeGuard>
#include "infrastructure/db/sqlite/ArchiveTransferJournal.h"
#include <QSqlError>
#include <QSqlQuery>

namespace OrderArchiveShardSqlSupport {

namespace {

struct PartInfo {
    int id = 0;
    QString uid;
    QString orderedAt;
    QString deliveryDate;
    QString partNumber;
    QString name;
    QString note;
};

} // namespace


// Read-only guard for every table currently deleted by purgeActiveOrderGraph.
// Run on the same connection/transaction as the copy, before committing it.
// This is a safety boundary, not a replacement for the durable archive journal.
bool verifyCopiedOrderGraph(QSqlDatabase database, int orderNumber, QString* errorText)
{
    auto fail = [&](const QString& detail) {
        if (errorText) *errorText = QStringLiteral("Archive verification failed; source retained: %1").arg(detail);
        return false;
    };
    if (orderNumber <= 0) return fail(QStringLiteral("Invalid internal order number"));
    struct Slice { const char* table; const char* predicate; bool required; };
    const Slice slices[] = {
        {"work_orders", "number=?", true},
        {"work_order_services", "order_number=?", true},
        {"work_order_parts", "order_number=?", true},
        {"service_masters", "service_id IN (SELECT id FROM main.work_order_services WHERE order_number=?)", true},
        {"work_order_part_attachments", "part_id IN (SELECT id FROM main.work_order_parts WHERE order_number=?)", false},
        {"work_order_service_attachments", "service_id IN (SELECT id FROM main.work_order_services WHERE order_number=?)", false},
        {"outsource_jobs", "order_number=?", false},
        {"outsource_repairs", "order_number=?", false}
    };
    auto quoteIdentifier = [](QString text) { return '"' + text.replace('"', "\"\"") + '"'; };
    auto columns = [&](const QString& schema, const QString& table, QStringList* names) {
        QSqlQuery query(database);
        if (!query.exec(QStringLiteral("PRAGMA %1.table_info(%2)").arg(schema, quoteIdentifier(table))))
            return fail(query.lastError().text());
        while (query.next()) names->append(query.value(1).toString());
        return true;
    };
    for (const auto& slice : slices) {
        const QString table = QString::fromLatin1(slice.table);
        const QString predicate = QString::fromLatin1(slice.predicate);
        QStringList sourceColumns, targetColumns;
        if (!columns(QStringLiteral("main"), table, &sourceColumns)) return false;
        if (sourceColumns.isEmpty()) {
            if (slice.required) return fail(table + QStringLiteral(": source table missing"));
            continue;
        }
        QSqlQuery count(database);
        count.prepare(QStringLiteral("SELECT COUNT(*) FROM main.%1 WHERE %2").arg(quoteIdentifier(table), predicate));
        count.addBindValue(orderNumber);
        if (!count.exec() || !count.next()) return fail(table + ": " + count.lastError().text());
        const auto sourceCount = count.value(0).toLongLong();
        count.finish();
        if (table == QStringLiteral("work_orders") && sourceCount != 1)
            return fail(QStringLiteral("Expected exactly one source order"));
        if (!columns(QStringLiteral("archive_rollover"), table, &targetColumns)) return false;
        if (targetColumns.isEmpty()) {
            if (sourceCount == 0) continue;
            return fail(table + QStringLiteral(": destination table missing"));
        }
        QStringList expressions;
        for (const auto& column : sourceColumns) {
            if (!targetColumns.contains(column)) {
                if (sourceCount == 0) continue;
                return fail(table + '.' + column + QStringLiteral(": destination column missing"));
            }
            // Compare native values with binary collation, including embedded NULs,
            // and storage classes; GROUP BY counts preserve duplicate multiplicity.
            expressions << QStringLiteral("typeof(%1),%1 COLLATE BINARY").arg(quoteIdentifier(column));
        }
        if (expressions.isEmpty()) return fail(table + QStringLiteral(": no comparable columns"));
        const auto fields = expressions.join(',');
        const auto from = QStringLiteral("SELECT %1,COUNT(*) FROM main.%2 WHERE %3 GROUP BY %1")
                              .arg(fields, quoteIdentifier(table), predicate);
        const auto to = QStringLiteral("SELECT %1,COUNT(*) FROM archive_rollover.%2 WHERE %3 GROUP BY %1")
                            .arg(fields, quoteIdentifier(table), predicate);
        for (const auto& comparison : {from + " EXCEPT " + to, to + " EXCEPT " + from}) {
            QSqlQuery difference(database);
            difference.prepare(comparison);
            difference.addBindValue(orderNumber);
            difference.addBindValue(orderNumber);
            if (!difference.exec()) return fail(table + ": " + difference.lastError().text());
            if (difference.next()) return fail(table + QStringLiteral(": source and archive differ"));
            if (difference.lastError().isValid()) return fail(table + ": " + difference.lastError().text());
        }
    }
    return true;
}


bool copyCompleteOrderGraph(QSqlDatabase database, int orderNumber, QString* errorText)
{
    auto fail = [&](const QString& text) { if (errorText) *errorText = text; return false; };
    auto ident = [](QString value) { return '"' + value.replace('"', "\"\"") + '"'; };
    struct Slice { QString table, predicate; bool required; };
    const QList<Slice> slices = {
        {"work_orders", "number=?", true},
        {"work_order_services", "order_number=?", true},
        {"work_order_parts", "order_number=?", true},
        {"service_masters", "service_id IN (SELECT id FROM main.work_order_services WHERE order_number=?)", true},
        {"work_order_part_attachments", "part_id IN (SELECT id FROM main.work_order_parts WHERE order_number=?)", false},
        {"work_order_service_attachments", "service_id IN (SELECT id FROM main.work_order_services WHERE order_number=?)", false},
        {"outsource_jobs", "order_number=?", false},
        {"outsource_repairs", "order_number=?", false}
    };
    // Never replace another order merely because its internal number collides.
    QSqlQuery identity(database);
    if (!identity.prepare("SELECT a.order_uid,s.order_uid FROM archive_rollover.work_orders a JOIN main.work_orders s ON s.number=a.number WHERE s.number=?"))
        return fail(identity.lastError().text());
    identity.addBindValue(orderNumber);
    if (!identity.exec()) return fail(identity.lastError().text());
    if (identity.next() && identity.value(0).toString() != identity.value(1).toString())
        return fail(QStringLiteral("Archive order identity conflict; source retained"));
    identity.finish();
    QList<Slice> present;
    QMap<QString, QStringList> fields;
    for (const auto& slice : slices) {
        QSqlQuery schema(database);
        if (!schema.prepare("SELECT sql FROM main.sqlite_master WHERE type='table' AND name=?")) return fail(schema.lastError().text());
        schema.addBindValue(slice.table);
        if (!schema.exec()) return fail(schema.lastError().text());
        if (!schema.next()) {
            if (slice.required) return fail("Missing source table: " + slice.table);
            continue;
        }
        const QString ddl = schema.value(0).toString();
        const int body = ddl.indexOf('(');
        if (body < 0) return fail("Unsupported source schema: " + slice.table);
        schema.finish();
        if (!schema.exec("CREATE TABLE IF NOT EXISTS archive_rollover." + ident(slice.table) + ddl.mid(body))) return fail(schema.lastError().text());
        QStringList target;
        if (!schema.exec("PRAGMA archive_rollover.table_info(" + ident(slice.table) + ")")) return fail(schema.lastError().text());
        while (schema.next()) target << schema.value(1).toString();
        if (!schema.exec("PRAGMA main.table_info(" + ident(slice.table) + ")")) return fail(schema.lastError().text());
        QStringList source;
        while (schema.next()) source << schema.value(1).toString();
        schema.finish();
        for (const auto& column : source) {
            // No default or conversion: preserve NULL and the source storage class.
            if (!target.contains(column) && !schema.exec("ALTER TABLE archive_rollover." + ident(slice.table) + " ADD COLUMN " + ident(column)))
                return fail(schema.lastError().text());
            fields[slice.table] << ident(column);
        }
        present << slice;
    }
    // Children first, using destination parent IDs, including leftovers from a retry.
    for (auto it = present.crbegin(); it != present.crend(); ++it) {
        QString predicate = it->predicate; predicate.replace("main.", "archive_rollover.");
        QSqlQuery remove(database);
        if (!remove.prepare("DELETE FROM archive_rollover." + ident(it->table) + " WHERE " + predicate)) return fail(remove.lastError().text());
        remove.addBindValue(orderNumber);
        if (!remove.exec()) return fail(remove.lastError().text());
    }
    for (const auto& slice : present) {
        const auto columns = fields.value(slice.table).join(',');
        QSqlQuery copy(database);
        if (!copy.prepare("INSERT INTO archive_rollover." + ident(slice.table) + "(" + columns + ") SELECT " + columns + " FROM main." + ident(slice.table) + " WHERE " + slice.predicate))
            return fail(copy.lastError().text());
        copy.addBindValue(orderNumber);
        if (!copy.exec()) return fail(copy.lastError().text());
    }
    return verifyCopiedOrderGraph(database, orderNumber, errorText);
}

bool archiveSingleActiveOrderToShard(QSqlDatabase database, int orderNumber, QString* errorText)
{
    if (orderNumber <= 0) return true;
    auto fail=[&](const QString& text){if(errorText)*errorText=text;return false;};
    if(!validateStartupRegistryIdentity(database,AppRuntime::registryDbFilePath(),errorText))return false;

    const QString headerSql=QStringLiteral(
        "SELECT order_uid,order_number,order_year,COALESCE(status,''),COALESCE(opened_at,''),COALESCE(closed_at,''),"
        "COALESCE(plate,''),COALESCE(plate_norm,''),COALESCE(vin,''),COALESCE(car,''),COALESCE(client,''),"
        "COALESCE(master_id,''),COALESCE(master_name_closed,''),COALESCE(works_sum,0),COALESCE(parts_sum,0),typeof(order_number),typeof(order_year) FROM %1.work_orders WHERE number=?");
    QSqlQuery readOrder(database);
    if(!readOrder.prepare(headerSql.arg(QStringLiteral("main"))))return fail(readOrder.lastError().text());
    readOrder.addBindValue(orderNumber);
    if (!readOrder.exec() || !readOrder.next()) {
        if (errorText) *errorText = readOrder.lastError().text().isEmpty()
            ? QStringLiteral("Order not found in active DB")
            : readOrder.lastError().text();
        return false;
    }

    const QString orderUid = readOrder.value(0).toString();
    const int visibleOrderNumber = readOrder.value(1).toInt();
    const int orderYear = readOrder.value(2).toInt();
    readOrder.finish();
    QSqlQuery sourceParts(database);
    if(!sourceParts.prepare(QStringLiteral("SELECT 1 FROM work_order_parts p WHERE (p.order_number=? OR p.order_uid=?) AND (COALESCE(p.order_uid,'')<>? OR COALESCE(p.order_number,0)<>? OR TRIM(COALESCE(p.part_uid,''))='' OR (SELECT COUNT(*) FROM work_order_parts x WHERE x.part_uid=p.part_uid)<>1) LIMIT 1")))return fail(sourceParts.lastError().text());
    sourceParts.addBindValue(orderNumber);sourceParts.addBindValue(orderUid);sourceParts.addBindValue(orderUid);sourceParts.addBindValue(orderNumber);
    if(!sourceParts.exec())return fail(sourceParts.lastError().text());
    if(sourceParts.next())return fail(QStringLiteral("Archive source part UID requires reconciliation"));
    if(sourceParts.lastError().isValid())return fail(sourceParts.lastError().text());
    sourceParts.finish();
    const QString shardPath = archiveShardFilePathForOrder(orderNumber);
    if (!ensureArchiveShardSchemaByPath(shardPath)) {
        if (errorText) *errorText = QStringLiteral("Archive shard schema is unavailable: %1").arg(shardPath);
        return false;
    }

    if (!ArchiveTransferJournal::begin(database, orderUid, orderNumber, shardPath, errorText)) return false;
    ArchiveTransferJournal::crashPoint("prepared");

    QSqlQuery q(database);
    q.exec(QStringLiteral("DETACH DATABASE archive_rollover"));
    if (!q.exec(QStringLiteral("ATTACH DATABASE %1 AS archive_rollover").arg(sqlQuoteText(shardPath)))) {
        if (errorText) *errorText = q.lastError().text();
        return false;
    }
    auto detachArchive = [&]() {
        QSqlQuery dq(database);
        dq.exec(QStringLiteral("DETACH DATABASE archive_rollover"));
    };

    const auto mutationResult = MutationExecutor::execute(
        database,
        MutationExecutor::Request{
            QStringLiteral("OrderArchiveShardSqlSupport"),
            QStringLiteral("order.archive"),
            QString::number(orderNumber),
            QStringLiteral("archive_single_order_to_shard"),
            QStringLiteral("Archive active order graph to rollover shard"),
            {QStringLiteral("archive_rollover.work_orders"),
             QStringLiteral("archive_rollover.work_order_services"),
             QStringLiteral("archive_rollover.service_masters"),
             QStringLiteral("archive_rollover.work_order_parts")},
            QStringLiteral("Failed to begin archive rollover transaction"),
            QStringLiteral("Failed to commit archive rollover transaction")
        },
        [&](QSqlDatabase& txDb, QString* stepError) {
            return copyCompleteOrderGraph(txDb, orderNumber, stepError);
        });
    const auto detachOnExit = qScopeGuard(detachArchive);
    if (!mutationResult.ok) {
        if (errorText) *errorText = mutationResult.errorText;
        return false;
    }

    ArchiveTransferJournal::crashPoint("copy_committed");
    if (!ArchiveTransferJournal::advance(database, orderUid, "copied", errorText) ||
        !verifyCopiedOrderGraph(database, orderNumber, errorText) ||
        !ArchiveTransferJournal::advance(database, orderUid, "verified", errorText)) return false;

    ArchiveTransferJournal::crashPoint("verified");
    // Read publication fields from the physical copy just verified above.
    // No cached numeric reference or pre-copy metadata supplies its identity.
    QSqlQuery physicalHeader(database);
    if(!physicalHeader.prepare(headerSql.arg(QStringLiteral("archive_rollover"))))return fail(physicalHeader.lastError().text());
    physicalHeader.addBindValue(orderNumber);
    if(!physicalHeader.exec())return fail(physicalHeader.lastError().text());
    if(!physicalHeader.next() || physicalHeader.value(0).toString()!=orderUid
        || physicalHeader.value(1).toLongLong()!=visibleOrderNumber || physicalHeader.value(2).toLongLong()!=orderYear
        || physicalHeader.value(15).toString()!=QStringLiteral("integer") || physicalHeader.value(16).toString()!=QStringLiteral("integer"))return fail(QStringLiteral("Physical archive header identity changed"));
    const QString status=physicalHeader.value(3).toString();
    const QString openedAt=physicalHeader.value(4).toString();
    const QString closedAt=physicalHeader.value(5).toString();
    const QString plate=physicalHeader.value(6).toString();
    const QString plateNorm=physicalHeader.value(7).toString();
    const QString vin=physicalHeader.value(8).toString();
    const QString car=physicalHeader.value(9).toString();
    const QString client=physicalHeader.value(10).toString();
    const QString masterId=physicalHeader.value(11).toString();
    const QString masterNameClosed=physicalHeader.value(12).toString();
    const double worksSum=physicalHeader.value(13).toDouble(),partsSum=physicalHeader.value(14).toDouble();
    physicalHeader.finish();
    QList<PartInfo> parts;QSqlQuery physicalPart(database);
    if(!physicalPart.prepare(QStringLiteral("SELECT id,COALESCE(ordered_at,''),COALESCE(delivery_date,''),COALESCE(part_number,''),COALESCE(name,''),COALESCE(note,''),part_uid,order_uid FROM archive_rollover.work_order_parts WHERE order_number=? ORDER BY id")))return fail(physicalPart.lastError().text());
    physicalPart.addBindValue(orderNumber);
    if(!physicalPart.exec())return fail(physicalPart.lastError().text());
    while(physicalPart.next()) {
        if(physicalPart.value(6).toString().trimmed().isEmpty() || physicalPart.value(7).toString()!=orderUid)return fail(QStringLiteral("Physical archive part identity requires reconciliation"));
        PartInfo info;info.id=physicalPart.value(0).toInt();info.orderedAt=physicalPart.value(1).toString();info.deliveryDate=physicalPart.value(2).toString();info.partNumber=physicalPart.value(3).toString();info.name=physicalPart.value(4).toString();info.note=physicalPart.value(5).toString();info.uid=physicalPart.value(6).toString();parts.append(info);
    }
    if(physicalPart.lastError().isValid())return fail(physicalPart.lastError().text());
    physicalPart.finish();
    if (!ensurePartRegistrySchemaFile()) {
        if (errorText) *errorText = QStringLiteral("Registry DB is unavailable");
        return false;
    }

    QSqlDatabase r = QSqlDatabase::database(dbRegistryConnectionName());
    TransactionContext registryTx(r,QStringLiteral("ArchiveRegistryPublication"));
    if(!registryTx.begin(QStringLiteral("Cannot start archive registry publication"),errorText))return false;
    const auto registryRollback=qScopeGuard([&]{if(registryTx.isActive()){QString ignored;registryTx.rollback(QString(),&ignored);}});
    QSqlQuery previous(r);
    if(!previous.prepare(QStringLiteral("SELECT order_number,order_uid,part_uid,part_ref FROM part_registry WHERE order_number=? OR order_uid=?")))return fail(previous.lastError().text());
    previous.addBindValue(orderNumber);previous.addBindValue(orderUid);
    if(!previous.exec())return fail(previous.lastError().text());
    while(previous.next()) {
        if(previous.value(0).toInt()!=orderNumber || previous.value(1).toString()!=orderUid || previous.value(2).toString().trimmed().isEmpty())return fail(QStringLiteral("Previous archive part projection requires UID reconciliation"));
        QSqlQuery owner(database);
        if(!owner.prepare(QStringLiteral("SELECT 1 FROM work_order_parts WHERE part_uid=? AND (order_uid<>? OR id<>?) LIMIT 1")))return fail(owner.lastError().text());
        owner.addBindValue(previous.value(2));owner.addBindValue(orderUid);owner.addBindValue(previous.value(3));
        if(!owner.exec())return fail(owner.lastError().text());
        if(owner.next())return fail(QStringLiteral("Previous archive part UID belongs to another source"));
        if(owner.lastError().isValid())return fail(owner.lastError().text());
    }
    if(previous.lastError().isValid())return fail(previous.lastError().text());
    previous.finish();
    QSqlQuery up(r);
    if(!up.prepare(QStringLiteral(
        "INSERT INTO order_registry(order_number, order_uid, visible_order_number, order_year, storage_kind, storage_path, source_db, imported_at, last_seen_at, status, opened_at, closed_at, plate, plate_norm, vin, car, client, master_id, master_name_closed, works_sum, parts_sum) "
        "VALUES(?, ?, ?, ?, 'archive', ?, ?, COALESCE((SELECT imported_at FROM order_registry WHERE order_number=?), ''), datetime('now'), ?,?,?,?,?,?,?,?,?,?,?,?) "
        "ON CONFLICT(order_number) DO UPDATE SET "
        "storage_kind=excluded.storage_kind, storage_path=excluded.storage_path, source_db=excluded.source_db, "
        "last_seen_at=excluded.last_seen_at, status=excluded.status, opened_at=excluded.opened_at, closed_at=excluded.closed_at, "
        "plate=excluded.plate, plate_norm=excluded.plate_norm, vin=excluded.vin, car=excluded.car, client=excluded.client, "
        "master_id=excluded.master_id, master_name_closed=excluded.master_name_closed, works_sum=excluded.works_sum, parts_sum=excluded.parts_sum WHERE order_registry.order_uid=excluded.order_uid AND order_registry.visible_order_number=excluded.visible_order_number AND order_registry.order_year=excluded.order_year")))return fail(up.lastError().text());
    up.addBindValue(orderNumber);
    up.addBindValue(nonNullText(orderUid));
    up.addBindValue(visibleOrderNumber);
    up.addBindValue(orderYear);
    up.addBindValue(nonNullText(shardPath));
    up.addBindValue(nonNullText(shardPath));
    up.addBindValue(orderNumber);
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
    if (!up.exec()) {
        if (errorText) *errorText = up.lastError().text();
        return false;
    }


    if(up.numRowsAffected()!=1)return fail(QStringLiteral("Archive registry header was not published"));
    up.finish();
    if(!removePartRegistryFileRows(orderNumber,QString(),QString(),false))return fail(QStringLiteral("Archive previous parts were not removed"));
    for (const PartInfo& info : parts) {
        const QString entryKey = makePartRegistryEntryKey(QStringLiteral("archive"), shardPath, orderNumber, info.id, info.orderedAt, info.partNumber, info.name);
        if (!upsertPartRegistryFileRow(entryKey, orderNumber, info.id, QStringLiteral("archive"), shardPath, shardPath,
                                       info.orderedAt, info.deliveryDate, info.partNumber, info.name, info.note,
                                       plate, plateNorm, vin, car, client,false,orderUid,info.uid)) {
            if (errorText) *errorText = QStringLiteral("Failed to upsert archived part registry rows");
            return false;
        }
    }
    if(!registryTx.commit(QStringLiteral("Archive registry commit failed"),errorText))return false;
    ArchiveTransferJournal::crashPoint("route_committed");
    if (!ArchiveTransferJournal::advance(database, orderUid, "routed", errorText)) return false;
    ArchiveTransferJournal::crashPoint("routed");
    QString purgeError;
    if (!purgeVerifiedArchivedOrderGraph(database, orderNumber, &purgeError)) {
        if (errorText) *errorText = purgeError;
        return false;
    }

    ArchiveTransferJournal::crashPoint("after_purge_commit");
    if (!upsertRegistryMirrorRow(database, orderNumber, orderUid, visibleOrderNumber, orderYear,
                            QStringLiteral("archive"), nonNullText(shardPath), nonNullText(shardPath),
                            QStringLiteral(""), nonNullText(status), nonNullText(openedAt), nonNullText(closedAt),
                            nonNullText(plate), nonNullText(plateNorm), nonNullText(vin), nonNullText(car), nonNullText(client),
                            nonNullText(masterId), nonNullText(masterNameClosed), worksSum, partsSum)) {
        if (errorText) *errorText = QStringLiteral("Archive registry mirror update failed");
        return false;
    }

    return true;
}

bool pruneActiveClosedOrdersToArchive(QSqlDatabase database, QString* errorText)
{
    if (!ArchiveTransferJournal::ensure(database, errorText)) return false;
    QSqlQuery pending(database);
    if (!pending.exec("SELECT COUNT(*) FROM archive_transfer_journal_v1 j WHERE j.phase<>'complete' AND NOT EXISTS(SELECT 1 FROM main.work_orders o WHERE o.number=j.order_number AND o.order_uid=j.order_uid)")) {
        if(errorText)*errorText=pending.lastError().text();return false;
    }
    if (!pending.next() || pending.value(0).toInt()!=0) {
        if(errorText)*errorText=QStringLiteral("Pending archive source identity missing; reconciliation required");return false;
    }
    pending.finish();
    if (!pending.exec("SELECT j.order_number FROM archive_transfer_journal_v1 j JOIN main.work_orders o ON o.number=j.order_number AND o.order_uid=j.order_uid WHERE j.phase<>'complete' ORDER BY j.updated_at")) {
        if (errorText) *errorText=pending.lastError().text();
        return false;
    }
    QList<int> resume;
    while (pending.next()) resume << pending.value(0).toInt();
    if (pending.lastError().isValid()) {if(errorText)*errorText=pending.lastError().text();return false;}
    pending.finish();
    for (int number : resume) if (!OrderArchiveShardSqlSupport::archiveSingleActiveOrderToShard(database, number, errorText)) return false;

    const int kClosedOrderLimit = OrderStoragePolicyService::activeClosedOrderLimit();
    const QString candidatePredicate = OrderStoragePolicyService::rotationCandidatePredicate(QStringLiteral("o"), QStringLiteral("ws"));

    QSqlQuery q(database);
    if (!q.exec(QStringLiteral(
            "SELECT COUNT(*) FROM work_orders o "
            "LEFT JOIN workspace_order_state ws ON ws.order_number=o.number "
            "WHERE %1").arg(candidatePredicate)) || !q.next()) {
        if (errorText) *errorText = q.lastError().text();
        return false;
    }
    const int totalClosedOrders = q.value(0).toInt();
    if (totalClosedOrders <= kClosedOrderLimit) return true;

    const int excess = totalClosedOrders - kClosedOrderLimit;
    QSqlQuery pick(database);
    pick.prepare(QStringLiteral(
        "SELECT o.number FROM work_orders o "
        "LEFT JOIN workspace_order_state ws ON ws.order_number=o.number "
        "WHERE %1 "
        "ORDER BY COALESCE(NULLIF(TRIM(o.closed_at),''), o.opened_at) ASC, o.number ASC LIMIT ?").arg(candidatePredicate));
    pick.addBindValue(excess);
    if (!pick.exec()) {
        if (errorText) *errorText = pick.lastError().text();
        return false;
    }

    QList<int> candidates;
    while (pick.next()) candidates.push_back(pick.value(0).toInt());

    bool ok = true;
    QString firstError;
    for (int orderNumber : candidates) {
        QString localError;
        if (!OrderArchiveShardSqlSupport::archiveSingleActiveOrderToShard(database, orderNumber, &localError)) {
            ok = false;
            if (firstError.trimmed().isEmpty()) firstError = localError;
        }
    }
    if (!ok && errorText) *errorText = firstError;
    return ok;
}

bool purgeHydratedArchiveCopiesFromActive(QSqlDatabase database, QString* errorText)
{
    if (!ensureWorkspaceOrderStateSchema(database)) {
        if (errorText) *errorText = QStringLiteral("Workspace order state schema unavailable; cleanup not performed");
        return false;
    }

    QSqlQuery q(database);
    if (!q.exec(QStringLiteral("SELECT order_number FROM workspace_order_state WHERE origin='legacy_copy' ORDER BY modified_at ASC, order_number ASC"))) {
        if (errorText) *errorText = q.lastError().text();
        return false;
    }

    QList<int> orderNumbers;
    while (q.next()) orderNumbers.push_back(q.value(0).toInt());
    if (q.lastError().isValid()) {
        if (errorText) *errorText = q.lastError().text();
        return false;
    }
    q.finish();
    for (int orderNumber : orderNumbers) {
        QString localError;
        if (!purgeActiveOrderGraph(database, orderNumber, &localError)) {
            if (errorText) *errorText = localError;
            return false;
        }
    }
    return true;
}

} // namespace OrderArchiveShardSqlSupport
