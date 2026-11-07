// P2 architecture migration: implementation module converted from legacy .ipp implementation-fragment decomposition.
// Compiled as a separate P2 implementation module; include-order dependencies are isolated in the module prelude.

#include "infrastructure/db/sqlite/DbArchiveRoutingSupportModulePrelude.h"
#include "core/services/orders/LocalWorkshopBackend.h"
#include "infrastructure/db/sqlite/DbContext.h"
#include <QScopeGuard>

namespace {

int maxOrderNumberValue(QSqlDatabase database, const QString& tableName, const QString& columnName)
{
    if (!database.isValid() || (!database.isOpen() && !database.open())) return 0;
    if (tableName.trimmed().isEmpty() || columnName.trimmed().isEmpty()) return 0;

    QSqlQuery exists(database);
    exists.prepare(QStringLiteral("SELECT 1 FROM sqlite_master WHERE type IN ('table','view') AND name=? LIMIT 1"));
    exists.addBindValue(tableName.trimmed());
    if (!exists.exec() || !exists.next()) return 0;

    QSqlQuery q(database);
    const QString sql = QStringLiteral("SELECT COALESCE(MAX(COALESCE(%1,0)),0) FROM %2")
                            .arg(columnName.trimmed(), tableName.trimmed());
    if (q.exec(sql) && q.next()) return q.value(0).toInt();
    return 0;
}

bool readCanonicalWorkspaceHeader(QSqlDatabase database, int number, QSqlQuery& read, QString* errorText)
{
    auto fail=[&](const QString& message) { if (errorText) *errorText=message; return false; };
    if (!database.isValid() || !database.isOpen()) return fail(QStringLiteral("Workspace database is not open"));
    if (!read.prepare(QStringLiteral("SELECT number, COALESCE(order_uid,''), COALESCE(order_number,0), COALESCE(order_year,0), COALESCE(status,''), COALESCE(opened_at,''), COALESCE(closed_at,''), COALESCE(plate,''), COALESCE(plate_norm,''), COALESCE(vin,''), COALESCE(car,''), COALESCE(client,''), COALESCE(master_id,''), COALESCE(master_name_closed,''), COALESCE(works_sum,0), COALESCE(parts_sum,0) FROM work_orders WHERE number=?"))) return fail(read.lastError().text());
    read.addBindValue(number);
    if (!read.exec()) return fail(read.lastError().text());
    if (!read.next()) return fail(read.lastError().isValid() ? read.lastError().text() : QStringLiteral("Order %1 not found in workspace DB").arg(number));
    bool numberOk=false,yearOk=false;
    const int visible=read.value(2).toInt(&numberOk), year=read.value(3).toInt(&yearOk);
    if (read.value(1).toString().trimmed().isEmpty() || !numberOk || !yearOk || visible<=0 || year<=0 || year>9999
        || read.value(2).toDouble()!=visible || read.value(3).toDouble()!=year)
        return fail(QStringLiteral("Workspace order identity requires explicit migration"));
    return true;
}

} // namespace

int Database::nextOrderNumberForDate(const QDate& openedDate)
{
    Q_UNUSED(openedDate);
    return nextOrderInternalNumber();
}

int Database::nextOrderNumber()
{
    return nextOrderNumberForDate(QDate::currentDate());
}

int Database::nextOrderInternalNumber()
{
    QSqlDatabase database = db();
    int maxNumber = maxOrderNumberValue(database, QStringLiteral("work_orders"), QStringLiteral("number"));
    maxNumber = qMax(maxNumber, maxOrderNumberValue(database, QStringLiteral("order_registry"), QStringLiteral("order_number")));

    if (ensureRegistrySchemaFile()) {
        maxNumber = qMax(maxNumber, maxOrderNumberValue(QSqlDatabase::database(dbRegistryConnectionName(), false),
                                                        QStringLiteral("order_registry"),
                                                        QStringLiteral("order_number")));
    }

    return qMax(1, maxNumber + 1);
}

QString Database::resolveOrderUid(int orderNumber)
{
    if (orderNumber <= 0) return QString();
    QSqlDatabase database = db();
    QString uid = activeOrderUidForInternalNumber(database, orderNumber).trimmed();
    if (!uid.isEmpty()) return uid;
    uid = routerCacheOrderUidForInternalNumber(database, orderNumber).trimmed();
    if (!uid.isEmpty()) return uid;

    if (ensureLocalRegistryMirrorSchema(database)) {
        QSqlQuery q(database);
        q.prepare(QStringLiteral("SELECT COALESCE(order_uid,'') FROM order_registry WHERE order_number=? LIMIT 1"));
        q.addBindValue(orderNumber);
        if (q.exec() && q.next()) {
            uid = q.value(0).toString().trimmed();
            if (!uid.isEmpty()) return uid;
        }
    }
    if (ensureRegistrySchemaFile()) {
        QSqlQuery q(QSqlDatabase::database(dbRegistryConnectionName()));
        q.prepare(QStringLiteral("SELECT COALESCE(order_uid,'') FROM order_registry WHERE order_number=? LIMIT 1"));
        q.addBindValue(orderNumber);
        if (q.exec() && q.next()) return q.value(0).toString().trimmed();
    }
    return QString();
}

int Database::resolveOrderInternalNumberByUid(const QString& orderUid)
{
    const QString uid = orderUid.trimmed();
    if (uid.isEmpty()) return 0;
    QSqlDatabase database = db();
    int orderNumber = activeInternalNumberForOrderUid(database, uid);
    if (orderNumber > 0) return orderNumber;
    orderNumber = routerCacheInternalNumberForOrderUid(database, uid);
    if (orderNumber > 0) return orderNumber;
    if (ensureLocalRegistryMirrorSchema(database)) {
        QSqlQuery q(database);
        q.prepare(QStringLiteral("SELECT COALESCE(order_number,0) FROM order_registry WHERE order_uid=? LIMIT 1"));
        q.addBindValue(uid);
        if (q.exec() && q.next()) {
            orderNumber = q.value(0).toInt();
            if (orderNumber > 0) return orderNumber;
        }
    }
    if (ensureRegistrySchemaFile()) {
        QSqlQuery q(QSqlDatabase::database(dbRegistryConnectionName()));
        q.prepare(QStringLiteral("SELECT COALESCE(order_number,0) FROM order_registry WHERE order_uid=? LIMIT 1"));
        q.addBindValue(uid);
        if (q.exec() && q.next()) return q.value(0).toInt();
    }
    return 0;
}

bool Database::ensureOrderRuntimeStateForInteractiveOpen(int orderNumber, QString* errorText)
{
    if (orderNumber <= 0) return true;

    QSqlDatabase database = db();
    if (!database.isValid() || (!database.isOpen() && !database.open())) {
        if (errorText) *errorText = database.lastError().text();
        return false;
    }
    if (!ensureWorkspaceOrderStateSchema(database) || !ensureLocalRegistryMirrorSchema(database)) {
        if (errorText) *errorText = QStringLiteral("Workspace state schema is unavailable");
        return false;
    }

    QSqlQuery read(database);
    read.prepare(QStringLiteral("SELECT number, COALESCE(order_uid,''), COALESCE(order_number,number), COALESCE(NULLIF(order_year,0), CAST(strftime('%Y', (CASE WHEN substr(trim(COALESCE(opened_at,'')),3,1)='.' AND substr(trim(COALESCE(opened_at,'')),6,1)='.' THEN CASE WHEN substr(trim(COALESCE(opened_at,'')),7,4) GLOB '[0-9][0-9][0-9][0-9]' THEN substr(trim(COALESCE(opened_at,'')),7,4) ELSE '20'||substr(trim(COALESCE(opened_at,'')),7,2) END||'-'||substr(trim(COALESCE(opened_at,'')),4,2)||'-'||substr(trim(COALESCE(opened_at,'')),1,2) ELSE COALESCE(opened_at,'') END)) AS INTEGER), CAST(strftime('%Y','now') AS INTEGER)), COALESCE(status,''), COALESCE(opened_at,''), COALESCE(closed_at,''), COALESCE(plate,''), COALESCE(plate_norm,''), COALESCE(vin,''), COALESCE(car,''), COALESCE(client,''), COALESCE(master_id,''), COALESCE(master_name_closed,''), COALESCE(works_sum,0), COALESCE(parts_sum,0) FROM work_orders WHERE number=?"));
    read.addBindValue(orderNumber);
    if (!read.exec()) {
        if (errorText) *errorText = read.lastError().text();
        return false;
    }
    if (!read.next()) {
        if (errorText) *errorText = QStringLiteral("Order %1 not found in workspace DB").arg(orderNumber);
        return false;
    }

    QString existingOrigin;
    workspaceOrderStateLookup(database, orderNumber, &existingOrigin);
    const QString orderUid = nonNullVariantText(read.value(1));
    const int visibleOrderNumber = read.value(2).toInt();
    const int orderYear = read.value(3).toInt();
    const QString resolvedOrigin = (!existingOrigin.trimmed().isEmpty())
        ? existingOrigin
        : (routerCacheHasOrderEntry(database, orderNumber) ? QStringLiteral("legacy_copy") : QStringLiteral("native"));
    const QString resolvedSourceDb = (resolvedOrigin == QStringLiteral("legacy_copy"))
        ? nonNullText(workspaceOrderSourceDbLookup(database, orderNumber).trimmed().isEmpty()
                      ? (QFileInfo(AppRuntime::archiveDbFilePath()).exists() ? AppRuntime::archiveDbFilePath() : QStringLiteral(""))
                      : workspaceOrderSourceDbLookup(database, orderNumber))
        : QStringLiteral("");

    if (!upsertWorkspaceOrderState(database, orderNumber, resolvedOrigin, resolvedSourceDb, orderUid)) {
        if (errorText) *errorText = QStringLiteral("Failed to upsert workspace_order_state");
        return false;
    }

    const QString localStorageKind = (resolvedOrigin == QStringLiteral("legacy_copy")) ? QStringLiteral("archive") : QStringLiteral("active");
    const QString localStoragePath = (resolvedOrigin == QStringLiteral("legacy_copy")) ? nonNullText(resolvedSourceDb) : nonNullText(AppRuntime::activeDbFilePath());
    upsertRegistryMirrorRow(database,
                            orderNumber,
                            orderUid,
                            visibleOrderNumber,
                            orderYear,
                            localStorageKind,
                            localStoragePath,
                            resolvedSourceDb,
                            QStringLiteral(""),
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
                            read.value(14).toDouble(),
                            read.value(15).toDouble());
    return true;
}

bool Database::ensureOrderAvailableInActiveByUid(const QString& orderUid, QString* errorText)
{
    const QString uid = orderUid.trimmed();
    if (uid.isEmpty()) return true;
    QSqlDatabase database = db();

    int orderNumber = activeInternalNumberForOrderUid(database, uid);
    if (orderNumber > 0) {
        return ensureOrderRuntimeStateForInteractiveOpen(orderNumber, errorText);
    }

    QString importError;
    QString archivedStoragePath;
    if (ensureLocalRegistryMirrorSchema(database)) {
        QSqlQuery regLocal(database);
        regLocal.prepare(QStringLiteral("SELECT COALESCE(order_number,0), COALESCE(storage_kind,''), COALESCE(storage_path,'') FROM order_registry WHERE order_uid=? LIMIT 1"));
        regLocal.addBindValue(uid);
        if (regLocal.exec() && regLocal.next()) {
            if (orderNumber <= 0) orderNumber = regLocal.value(0).toInt();
            if (regLocal.value(1).toString().trimmed() == QStringLiteral("archive")) {
                archivedStoragePath = regLocal.value(2).toString().trimmed();
            }
        }
    }
    if (orderNumber <= 0 || archivedStoragePath.trimmed().isEmpty()) {
        if (ensureRegistrySchemaFile()) {
            QSqlQuery regFile(QSqlDatabase::database(dbRegistryConnectionName()));
            regFile.prepare(QStringLiteral("SELECT COALESCE(order_number,0), COALESCE(storage_kind,''), COALESCE(storage_path,'') FROM order_registry WHERE order_uid=? LIMIT 1"));
            regFile.addBindValue(uid);
            if (regFile.exec() && regFile.next()) {
                if (orderNumber <= 0) orderNumber = regFile.value(0).toInt();
                if (archivedStoragePath.trimmed().isEmpty() && regFile.value(1).toString().trimmed() == QStringLiteral("archive")) {
                    archivedStoragePath = regFile.value(2).toString().trimmed();
                }
            }
        }
    }

    if (!archivedStoragePath.trimmed().isEmpty() && orderNumber > 0 && importSingleArchivedOrderToActiveFromPath(archivedStoragePath, orderNumber, &importError)) {
        upsertWorkspaceOrderState(database, orderNumber, QStringLiteral("legacy_copy"), archivedStoragePath, uid);
        return finalizeNewOrderWorkspaceState(orderNumber, errorText);
    }

    if (orderNumber <= 0) {
        orderNumber = routerCacheInternalNumberForOrderUid(database, uid);
    }

    if (orderNumber > 0 && QFileInfo::exists(AppRuntime::archiveDbFilePath())) {
        QString directLegacyImportError;
        if (importSingleLegacyOrderToActive(orderNumber, &directLegacyImportError)) {
            upsertWorkspaceOrderState(database, orderNumber, QStringLiteral("legacy_copy"), AppRuntime::archiveDbFilePath(), uid);
            return finalizeNewOrderWorkspaceState(orderNumber, errorText);
        }
        if (importError.trimmed().isEmpty()) importError = directLegacyImportError;
    }

    if (orderNumber > 0) {
        const bool ok = copyCachedOrderToActive(database, uid, orderNumber, errorText);
        if (!ok) return false;
        if (!workspaceOrderStateLookupByUid(database, uid, nullptr)) {
            upsertWorkspaceOrderState(database, orderNumber, QStringLiteral("legacy_copy"), AppRuntime::archiveDbFilePath(), uid);
        }
        return finalizeNewOrderWorkspaceState(orderNumber, errorText);
    }

    if (errorText) {
        *errorText = !importError.trimmed().isEmpty()
            ? importError
            : QStringLiteral("Order with uid %1 could not be found in active/archive routing").arg(uid);
    }
    return false;
}

bool Database::finalizeNewOrderWorkspaceStateByUid(const QString& orderUid, QString* errorText)
{
    const int orderNumber = resolveOrderInternalNumberByUid(orderUid);
    if (orderNumber <= 0) {
        if (errorText) *errorText = QStringLiteral("Order uid not resolved: %1").arg(orderUid);
        return false;
    }
    return finalizeNewOrderWorkspaceState(orderNumber, errorText);
}

bool Database::refreshOrderRegistryByUid(const QString& orderUid, QString* errorText)
{
    const int orderNumber = resolveOrderInternalNumberByUid(orderUid);
    if (orderNumber <= 0) {
        if (errorText) *errorText = QStringLiteral("Order uid not resolved: %1").arg(orderUid);
        return false;
    }
    return refreshOrderRegistry(orderNumber, errorText);
}

bool Database::removeOrderFromRegistryByUid(const QString& orderUid, QString* errorText)
{
    const QString uid = orderUid.trimmed();
    if (uid.isEmpty()) return true;
    const int orderNumber = resolveOrderInternalNumberByUid(uid);
    if (orderNumber > 0) return removeOrderFromRegistry(orderNumber, errorText);
    if (ensureRegistrySchemaFile()) {
        QSqlQuery q(QSqlDatabase::database(dbRegistryConnectionName()));
        q.prepare(QStringLiteral("DELETE FROM order_registry WHERE order_uid=?"));
        q.addBindValue(uid);
        if (!q.exec()) {
            if (errorText) *errorText = q.lastError().text();
            return false;
        }
    }
    QSqlDatabase database = db();
    if (ensureLocalRegistryMirrorSchema(database)) {
        QSqlQuery q(database);
        q.prepare(QStringLiteral("DELETE FROM order_registry WHERE order_uid=?"));
        q.addBindValue(uid);
        q.exec();
    }
    if (ensureWorkspaceOrderStateSchema(database)) {
        QSqlQuery q(database);
        q.prepare(QStringLiteral("DELETE FROM workspace_order_state WHERE order_uid=?"));
        q.addBindValue(uid);
        q.exec();
    }
    return true;
}

bool Database::ensureOrderReadyForInteractiveOpen(int orderNumber, QString* errorText)
{
    if (orderNumber <= 0) return true;
    if (!ensureConnectionAlive(errorText)) return false;

    QSqlQuery chk(db());
    chk.prepare(QStringLiteral("SELECT 1 FROM work_orders WHERE number=? LIMIT 1"));
    chk.addBindValue(orderNumber);
    if (chk.exec() && chk.next()) {
        return ensureOrderRuntimeStateForInteractiveOpen(orderNumber, errorText);
    }

    return ensureOrderAvailableInActive(orderNumber, errorText);
}

bool Database::ensureOrderAvailableInActive(int orderNumber, QString* errorText)
{
    if (orderNumber <= 0) return true;
    QSqlDatabase database = db();
    const QString knownUid = resolveOrderUid(orderNumber);
    if (!knownUid.trimmed().isEmpty()) {
        return ensureOrderAvailableInActiveByUid(knownUid, errorText);
    }

    QSqlQuery chk(database);
    chk.prepare("SELECT 1 FROM work_orders WHERE number=? LIMIT 1");
    chk.addBindValue(orderNumber);
    const bool activeOrder = chk.exec() && chk.next();
    if (activeOrder) {
        return ensureOrderRuntimeStateForInteractiveOpen(orderNumber, errorText);
    }

    QString importError;
    QString archivedStoragePath;
    {
        QSqlQuery regLocal(database);
        regLocal.prepare(QStringLiteral("SELECT COALESCE(storage_kind,''), COALESCE(storage_path,'') FROM order_registry WHERE order_number=? LIMIT 1"));
        regLocal.addBindValue(orderNumber);
        if (regLocal.exec() && regLocal.next() && regLocal.value(0).toString().trimmed() == QStringLiteral("archive")) {
            archivedStoragePath = regLocal.value(1).toString().trimmed();
        }
    }
    if (archivedStoragePath.trimmed().isEmpty() && ensureRegistrySchemaFile()) {
        QSqlQuery regFile(QSqlDatabase::database(dbRegistryConnectionName()));
        regFile.prepare(QStringLiteral("SELECT COALESCE(storage_kind,''), COALESCE(storage_path,'') FROM order_registry WHERE order_number=? LIMIT 1"));
        regFile.addBindValue(orderNumber);
        if (regFile.exec() && regFile.next() && regFile.value(0).toString().trimmed() == QStringLiteral("archive")) {
            archivedStoragePath = regFile.value(1).toString().trimmed();
        }
    }

    if (!archivedStoragePath.trimmed().isEmpty() && importSingleArchivedOrderToActiveFromPath(archivedStoragePath, orderNumber, &importError)) {
        upsertWorkspaceOrderState(database, orderNumber, QStringLiteral("legacy_copy"), archivedStoragePath);
        return finalizeNewOrderWorkspaceState(orderNumber, errorText);
    }

    if (importSingleLegacyOrderToActive(orderNumber, &importError)) {
        upsertWorkspaceOrderState(database, orderNumber, QStringLiteral("legacy_copy"), AppRuntime::archiveDbFilePath());
        return finalizeNewOrderWorkspaceState(orderNumber, errorText);
    }

    if (routerCacheHasOrderEntry(database, orderNumber)) {
        const bool ok = copyCachedOrderToActive(database, knownUid, orderNumber, errorText);
        if (!ok) return false;
        if (!workspaceOrderStateLookup(database, orderNumber, nullptr)) {
            upsertWorkspaceOrderState(database, orderNumber, QStringLiteral("legacy_copy"), AppRuntime::archiveDbFilePath());
        }
        return finalizeNewOrderWorkspaceState(orderNumber, errorText);
    }

    if (createDraftOrderShellInActive(database, orderNumber, errorText)) {
        return finalizeNewOrderWorkspaceState(orderNumber, errorText);
    }

    if (errorText) {
        *errorText = !importError.trimmed().isEmpty()
            ? importError
            : QStringLiteral("Order %1 could not be created in workspace DB").arg(orderNumber);
    }
    return false;
}

bool Database::finalizeOrderWorkspaceInTransaction(QSqlDatabase database, int orderNumber, QString* errorText)
{
    const QString archiveOrigin = QStringLiteral("legacy_copy");
    auto fail = [&](const QString& message) { if (errorText) *errorText = message; return false; };
    if (!TransactionContext::hasActiveTransaction(database))
        return fail(QStringLiteral("Workspace finalization requires an owned transaction"));
    if (orderNumber <= 0) return true;

    // Schema/identity migration belongs to bootstrap. Finalization must only
    // validate prepared canonical data; schema helpers can rewrite legacy values.
    QSqlQuery read(database);
    if (!readCanonicalWorkspaceHeader(database,orderNumber,read,errorText)) return false;
    const QString orderUid=read.value(1).toString();
    const int visibleOrderNumber=read.value(2).toInt(),orderYear=read.value(3).toInt();

    QSqlQuery state(database);
    if (!state.prepare(QStringLiteral("SELECT COALESCE(order_uid,''),COALESCE(origin,''),COALESCE(source_db,'') FROM workspace_order_state WHERE order_number=?")))
        return fail(state.lastError().text());
    state.addBindValue(orderNumber);
    if (!state.exec()) return fail(state.lastError().text());
    const bool hasState = state.next();
    if (state.lastError().isValid()) return fail(state.lastError().text());
    QString resolvedOrigin, resolvedSourceDb;
    if (hasState) {
        if (state.value(0).toString()!=orderUid)
            return fail(QStringLiteral("Workspace state UID conflicts with the order identity"));
        resolvedOrigin=state.value(1).toString(); resolvedSourceDb=state.value(2).toString();
        if (resolvedOrigin!=QStringLiteral("native") && resolvedOrigin!=archiveOrigin)
            return fail(QStringLiteral("Workspace origin requires explicit migration"));
    }
    state.finish();

    QSqlQuery registry(database);
    if (!registry.prepare(QStringLiteral("SELECT COALESCE(order_uid,''),COALESCE(visible_order_number,0),COALESCE(order_year,0),COALESCE(storage_kind,''),COALESCE(source_db,''),COALESCE(storage_path,'') FROM order_registry WHERE order_number=?")))
        return fail(registry.lastError().text());
    registry.addBindValue(orderNumber);
    if (!registry.exec()) return fail(registry.lastError().text());
    const bool hasRegistry=registry.next();
    if (registry.lastError().isValid()) return fail(registry.lastError().text());
    if (hasRegistry && (registry.value(0).toString()!=orderUid || registry.value(1).toInt()!=visibleOrderNumber || registry.value(2).toInt()!=orderYear))
        return fail(QStringLiteral("Local registry identity conflicts with the workspace order"));
    const QString recordedStorage=hasRegistry?registry.value(3).toString():QString();
    const QString recordedSource=hasRegistry?registry.value(4).toString():QString();
    const QString recordedPath=hasRegistry?registry.value(5).toString():QString();
    registry.finish();

    if (!hasState && hasRegistry) {
        // A matching active mirror remains native even when also cached.
        // Cache membership alone does not establish archive provenance.
        if(recordedStorage==QStringLiteral("active"))resolvedOrigin=QStringLiteral("native");
        else if(recordedStorage==QStringLiteral("archive")) {
            resolvedOrigin=archiveOrigin;
            resolvedSourceDb=recordedSource.isEmpty()?recordedPath:recordedSource;
            if(resolvedSourceDb.trimmed().isEmpty())return fail(QStringLiteral("Archived workspace source requires reconciliation"));
        } else return fail(QStringLiteral("Recorded workspace storage requires reconciliation"));
    }
    if (!hasState && !hasRegistry) {
        // An internal integer can name a different historical/cache order.
        // Only the immutable UID can establish that this is its hydrated copy.
        QSqlQuery cache(database);
        if (!cache.prepare(QStringLiteral("SELECT COALESCE(order_uid,''),COALESCE(order_number,0),COALESCE(order_year,0) FROM router_cache_work_orders WHERE number=?")))
            return fail(cache.lastError().text());
        cache.addBindValue(orderNumber);
        if (!cache.exec()) return fail(cache.lastError().text());
        const bool hasCache=cache.next();
        if (cache.lastError().isValid()) return fail(cache.lastError().text());
        if (hasCache && cache.value(0).toString().trimmed().isEmpty())
            return fail(QStringLiteral("Cached order identity requires explicit migration"));
        const bool sameOrder=hasCache && cache.value(0).toString()==orderUid;
        if (sameOrder && (cache.value(1).toInt()!=visibleOrderNumber || cache.value(2).toInt()!=orderYear))
            return fail(QStringLiteral("Cached order identity conflicts with the workspace order"));
        resolvedOrigin=sameOrder ? archiveOrigin : QStringLiteral("native");
    }
    if (resolvedOrigin==archiveOrigin && resolvedSourceDb.trimmed().isEmpty())
        resolvedSourceDb=QFileInfo(AppRuntime::archiveDbFilePath()).exists() ? AppRuntime::archiveDbFilePath() : QStringLiteral("");
    if (resolvedOrigin==QStringLiteral("native")) resolvedSourceDb=QStringLiteral("");

    if (!upsertWorkspaceOrderStateEntry(database, orderNumber, resolvedOrigin, resolvedSourceDb, orderUid, {}, false)) {
        if (errorText) *errorText = QStringLiteral("Failed to upsert workspace_order_state");
        return false;
    }
    const QString localStorageKind = (resolvedOrigin == archiveOrigin) ? QStringLiteral("archive") : QStringLiteral("active");
    const QString localStoragePath = (resolvedOrigin == archiveOrigin) ? nonNullText(resolvedSourceDb) : nonNullText(AppRuntime::activeDbFilePath());
    if (!upsertRegistryMirrorRow(database,
                                 orderNumber,
                                 orderUid,
                                 visibleOrderNumber,
                                 orderYear,
                                 localStorageKind,
                                 localStoragePath,
                                 nonNullText(resolvedSourceDb),
                                 QStringLiteral(""),
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
                                 read.value(14).toDouble(),
                                 read.value(15).toDouble(), false)) {
        if (errorText) *errorText = QStringLiteral("Failed to upsert local order_registry mirror");
        return false;
    }
    return queueOrderRegistryPublication(database,orderNumber,errorText,false);
}

bool Database::finalizeNewOrderWorkspaceState(int orderNumber, QString* errorText)
{
    if (orderNumber <= 0) return true;
    QSqlDatabase database = db();
    MutationExecutor::Request request;
    request.sourceModule = QStringLiteral("DbArchiveRoutingSupport.finalizeNewOrderWorkspaceState");
    request.entityType = QStringLiteral("workspace_order_state");
    request.entityId = QString::number(orderNumber);
    request.operation = QStringLiteral("finalize_workspace_state");
    request.changedTables = {QStringLiteral("workspace_order_state"), QStringLiteral("order_registry")};
    request.beginErrorFallback = QStringLiteral("Failed to start finalize workspace transaction");
    request.commitErrorFallback = QStringLiteral("Failed to commit finalize workspace transaction");

    const auto tx = MutationExecutor::execute(database, request, [&](QSqlDatabase& txDb, QString* txError) {
        return finalizeOrderWorkspaceInTransaction(txDb, orderNumber, txError);
    });
    if (!tx.ok) { if (errorText) *errorText = tx.errorText; return false; }
    publishFinalizedOrderWorkspace(orderNumber);
    return true;
}

void Database::publishFinalizedOrderWorkspace(int orderNumber)
{
    if (orderNumber <= 0) return;
    QString registryError;
    if (!refreshOrderRegistry(orderNumber, &registryError)) {
        qWarning() << "Registry refresh skipped for order" << orderNumber << registryError;
    }
    QString origin;
    if (workspaceOrderStateLookup(db(), orderNumber, &origin) && origin != QStringLiteral("legacy_copy")) {
        QString pruneError;
        if (!OrderArchiveRotationService::enforcePolicy(&pruneError) && !pruneError.trimmed().isEmpty()) {
            qWarning() << "Archive pruning skipped/failed:" << pruneError;
        }
    }
}

bool Database::replayPendingOrderRegistryRefresh(QString* errorText)
{
    QSqlDatabase source=db(errorText);
    if (!source.isValid() || !source.isOpen()) return false;
    if (TransactionContext::hasActiveTransaction(source)) {
        if (errorText) *errorText=QStringLiteral("Registry replay must follow local COMMIT");
        return false;
    }
    QSqlQuery pending(source);
    if (!pending.exec(QStringLiteral("SELECT order_number,order_uid,registry_revision,published_registry_revision,typeof(registry_revision),typeof(published_registry_revision) FROM workspace_order_state WHERE registry_revision<>published_registry_revision OR registry_revision<0 OR published_registry_revision<0 OR typeof(registry_revision)<>'integer' OR typeof(published_registry_revision)<>'integer' ORDER BY order_number"))) {
        if (errorText) *errorText=pending.lastError().text(); return false;
    }
    QList<int> numbers;
    while (pending.next()) {
        if (pending.value(0).toInt()<=0 || pending.value(1).toString().trimmed().isEmpty()
            || pending.value(4).toString()!=QStringLiteral("integer") || pending.value(5).toString()!=QStringLiteral("integer")
            || pending.value(3).toLongLong()<0 || pending.value(2).toLongLong()<pending.value(3).toLongLong()) {
            if (errorText) *errorText=QStringLiteral("Pending registry intent requires reconciliation"); return false;
        }
        numbers.append(pending.value(0).toInt());
    }
    if (pending.lastError().isValid()) { if (errorText) *errorText=pending.lastError().text(); return false; }
    pending.finish();
    for (int number:numbers) if (!refreshOrderRegistry(number,errorText)) return false;
    return true;
}

bool publishCommittedOrderRegistryProjection(QSqlDatabase source, int orderNumber, QString* errorText)
{
    auto fail=[&](const QString& message) {if(errorText)*errorText=message;return false;};
    auto r=QSqlDatabase::database(dbRegistryConnectionName(),false);
    if(!source.isValid() || !source.isOpen() || !r.isValid() || !r.isOpen()
       || !TransactionContext::hasActiveTransaction(source) || !TransactionContext::hasActiveTransaction(r))
        return fail(QStringLiteral("Registry projection requires owned source and target transactions"));
    QSqlQuery read(source);
    if (!readCanonicalWorkspaceHeader(source,orderNumber,read,errorText)) return false;
    const QString uid=read.value(1).toString();
    const int visible=read.value(2).toInt(),year=read.value(3).toInt();
    QSqlQuery state(source);
    if (!state.prepare(QStringLiteral("SELECT COALESCE(order_uid,''),COALESCE(origin,''),COALESCE(source_db,''),registry_revision,published_registry_revision,typeof(registry_revision),typeof(published_registry_revision) FROM workspace_order_state WHERE order_number=?"))) return fail(state.lastError().text());
    state.addBindValue(orderNumber);
    if (!state.exec()) return fail(state.lastError().text());
    if (!state.next() || state.value(0).toString()!=uid) return fail(QStringLiteral("Committed workspace state requires identity reconciliation"));
    const qlonglong revision=state.value(3).toLongLong(), published=state.value(4).toLongLong();
    if (state.value(5).toString()!=QStringLiteral("integer") || state.value(6).toString()!=QStringLiteral("integer") || published<0 || revision<published)
        return fail(QStringLiteral("Workspace registry revision requires reconciliation"));
    const QString origin=state.value(1).toString();
    const bool hydratedArchive=origin==QStringLiteral("legacy_copy");
    if (!hydratedArchive && origin!=QStringLiteral("native")) return fail(QStringLiteral("Committed workspace origin is invalid"));
    const QString sourceDb=hydratedArchive ? state.value(2).toString() : QStringLiteral("");
    if (hydratedArchive && sourceDb.trimmed().isEmpty()) return fail(QStringLiteral("Archived workspace source path requires reconciliation"));
    const QString storageKind=hydratedArchive ? QStringLiteral("archive") : QStringLiteral("active");
    const QString canonicalArchivePath=hydratedArchive ? QFileInfo(sourceDb).canonicalFilePath() : QString();
    const QString storagePath=hydratedArchive ? (canonicalArchivePath.isEmpty()?sourceDb:canonicalArchivePath) : AppRuntime::activeDbFilePath();
    state.finish();
    QSqlQuery mirrorState(source);
    if (!mirrorState.prepare(QStringLiteral("SELECT order_uid,visible_order_number,order_year FROM order_registry WHERE order_number=?"))) return fail(mirrorState.lastError().text());
    mirrorState.addBindValue(orderNumber);
    if (!mirrorState.exec()) return fail(mirrorState.lastError().text());
    if (!mirrorState.next() || mirrorState.value(0).toString()!=uid || mirrorState.value(1).toDouble()!=visible || mirrorState.value(2).toDouble()!=year)
        return fail(QStringLiteral("Committed local registry identity requires reconciliation"));
    mirrorState.finish();
    QSqlQuery existing(r);
    if (!existing.prepare(QStringLiteral("SELECT order_number,order_uid,visible_order_number,order_year FROM order_registry WHERE order_number=? OR order_uid=? OR (order_year=? AND visible_order_number=?)"))) return fail(existing.lastError().text());
    existing.addBindValue(orderNumber);existing.addBindValue(uid);existing.addBindValue(year);existing.addBindValue(visible);
    if (!existing.exec()) return fail(existing.lastError().text());
    while (existing.next()) {
        if (existing.value(0).toLongLong()!=orderNumber || existing.value(1).toString()!=uid || existing.value(2).toDouble()!=visible || existing.value(3).toDouble()!=year)
            return fail(QStringLiteral("External registry identity conflicts with the committed order"));
    }
    if (existing.lastError().isValid()) return fail(existing.lastError().text());
    existing.finish();
    QSqlQuery up(r);
    if (!up.prepare(QStringLiteral(
        "INSERT INTO order_registry(order_number, order_uid, visible_order_number, order_year, storage_kind, storage_path, source_db, imported_at, last_seen_at, status, opened_at, closed_at, plate, plate_norm, vin, car, client, master_id, master_name_closed, works_sum, parts_sum) "
        "VALUES(?, ?, ?, ?, ?, ?, ?, COALESCE((SELECT imported_at FROM order_registry WHERE order_number=?), ''), datetime('now'), ?,?,?,?,?,?,?,?,?,?,?,?) "
        "ON CONFLICT(order_number) DO UPDATE SET "
        "storage_kind=excluded.storage_kind, storage_path=excluded.storage_path, source_db=excluded.source_db, "
        "last_seen_at=excluded.last_seen_at, status=excluded.status, opened_at=excluded.opened_at, closed_at=excluded.closed_at, "
        "plate=excluded.plate, plate_norm=excluded.plate_norm, vin=excluded.vin, car=excluded.car, client=excluded.client, "
        "master_id=excluded.master_id, master_name_closed=excluded.master_name_closed, works_sum=excluded.works_sum, parts_sum=excluded.parts_sum WHERE order_registry.order_uid=excluded.order_uid AND order_registry.visible_order_number=excluded.visible_order_number AND order_registry.order_year=excluded.order_year"))) return fail(up.lastError().text());
    up.addBindValue(orderNumber);
    up.addBindValue(nonNullVariantText(read.value(1)));
    up.addBindValue(read.value(2).toInt());
    up.addBindValue(read.value(3).toInt());
    up.addBindValue(nonNullText(storageKind));
    up.addBindValue(nonNullText(storagePath));
    up.addBindValue(nonNullText(sourceDb));
    up.addBindValue(orderNumber);
    for (int i = 4; i <= 13; ++i) up.addBindValue(nonNullVariantText(read.value(i)));
    up.addBindValue(read.value(14).toDouble());
    up.addBindValue(read.value(15).toDouble());
    if (!up.exec()) return fail(up.lastError().text());
    if (up.numRowsAffected()!=1) return fail(QStringLiteral("External registry publication did not write the expected row"));
    up.finish();
    if (!syncPartRegistryFromLiveOrder(source, orderNumber, storageKind, storagePath, sourceDb)) {
        return fail(QStringLiteral("Failed to refresh order parts registry"));
    }
    return true;
}

bool Database::refreshOrderRegistry(int orderNumber, QString* errorText)
{
    if (orderNumber<=0) return true;
    auto fail=[&](const QString& message) { if (errorText) *errorText=message; return false; };
    QSqlDatabase source=db(errorText);
    if (!source.isValid() || !source.isOpen()) return false;
    if (TransactionContext::hasActiveTransaction(source))
        return fail(QStringLiteral("External registry publication must follow local COMMIT"));
    TransactionContext sourceTx(source,QStringLiteral("RegistrySourceSnapshot"));
    if (!sourceTx.begin(QStringLiteral("Cannot start registry source snapshot"),errorText)) return false;
    auto sourceRollback=qScopeGuard([&] { if (sourceTx.isActive()) {QString ignored;sourceTx.rollback(QString(),&ignored);} });
    QSqlQuery read(source);
    if (!readCanonicalWorkspaceHeader(source,orderNumber,read,errorText)) return false;
    const QString uid=read.value(1).toString();
    const int visible=read.value(2).toInt(),year=read.value(3).toInt();
    QSqlQuery state(source);
    if (!state.prepare(QStringLiteral("SELECT COALESCE(order_uid,''),COALESCE(origin,''),COALESCE(source_db,''),registry_revision,published_registry_revision,typeof(registry_revision),typeof(published_registry_revision) FROM workspace_order_state WHERE order_number=?"))) return fail(state.lastError().text());
    state.addBindValue(orderNumber);
    if (!state.exec()) return fail(state.lastError().text());
    if (!state.next() || state.value(0).toString()!=uid) return fail(QStringLiteral("Committed workspace state requires identity reconciliation"));
    const qlonglong revision=state.value(3).toLongLong(), published=state.value(4).toLongLong();
    if (state.value(5).toString()!=QStringLiteral("integer") || state.value(6).toString()!=QStringLiteral("integer") || published<0 || revision<published)
        return fail(QStringLiteral("Workspace registry revision requires reconciliation"));
    const QString origin=state.value(1).toString();
    const bool hydratedArchive=origin==QStringLiteral("legacy_copy");
    if (!hydratedArchive && origin!=QStringLiteral("native")) return fail(QStringLiteral("Committed workspace origin is invalid"));
    const QString sourceDb=hydratedArchive ? state.value(2).toString() : QStringLiteral("");
    if (hydratedArchive && sourceDb.trimmed().isEmpty()) return fail(QStringLiteral("Archived workspace source path requires reconciliation"));
    const QString storageKind=hydratedArchive ? QStringLiteral("archive") : QStringLiteral("active");
    const QString canonicalArchivePath=hydratedArchive ? QFileInfo(sourceDb).canonicalFilePath() : QString();
    const QString storagePath=hydratedArchive ? (canonicalArchivePath.isEmpty()?sourceDb:canonicalArchivePath) : AppRuntime::activeDbFilePath();
    state.finish();
    QSqlQuery mirrorState(source);
    if (!mirrorState.prepare(QStringLiteral("SELECT order_uid,visible_order_number,order_year FROM order_registry WHERE order_number=?"))) return fail(mirrorState.lastError().text());
    mirrorState.addBindValue(orderNumber);
    if (!mirrorState.exec()) return fail(mirrorState.lastError().text());
    if (!mirrorState.next() || mirrorState.value(0).toString()!=uid || mirrorState.value(1).toDouble()!=visible || mirrorState.value(2).toDouble()!=year)
        return fail(QStringLiteral("Committed local registry identity requires reconciliation"));
    mirrorState.finish();
    if (!ensureRegistrySchemaFile()) return fail(QStringLiteral("Registry DB/schema is unavailable"));
    QSqlDatabase r=QSqlDatabase::database(dbRegistryConnectionName(),false);
    TransactionContext registryTx(r,QStringLiteral("ExternalRegistryRefresh"));
    if (!registryTx.begin(QStringLiteral("Cannot start external registry refresh"),errorText)) return false;
    auto registryRollback=qScopeGuard([&] { if (registryTx.isActive()) {QString ignored;registryTx.rollback(QString(),&ignored);} });
    if (!publishCommittedOrderRegistryProjection(source,orderNumber,errorText)) return false;
    if (!upsertRegistryMirrorRow(source, orderNumber, nonNullVariantText(read.value(1)), read.value(2).toInt(), read.value(3).toInt(),
                            storageKind, nonNullText(storagePath), nonNullText(sourceDb),
                            QStringLiteral(""), nonNullVariantText(read.value(4)), nonNullVariantText(read.value(5)), nonNullVariantText(read.value(6)),
                            nonNullVariantText(read.value(7)), nonNullVariantText(read.value(8)), nonNullVariantText(read.value(9)),
                            nonNullVariantText(read.value(10)), nonNullVariantText(read.value(11)), nonNullVariantText(read.value(12)),
                            nonNullVariantText(read.value(13)), read.value(14).toDouble(), read.value(15).toDouble(), false)) {
        return fail(QStringLiteral("Failed to refresh prepared local registry mirror"));
    }
    QSqlQuery acknowledge(source);
    if (!acknowledge.prepare(QStringLiteral("UPDATE workspace_order_state SET published_registry_revision=? WHERE order_number=? AND order_uid=? AND registry_revision=?"))) return fail(acknowledge.lastError().text());
    acknowledge.addBindValue(revision);acknowledge.addBindValue(orderNumber);acknowledge.addBindValue(uid);acknowledge.addBindValue(revision);
    if (!acknowledge.exec()) return fail(acknowledge.lastError().text());
    if (acknowledge.numRowsAffected()!=1) return fail(QStringLiteral("Registry publication acknowledgement was not recorded"));
    acknowledge.finish();
    if (!registryTx.commit(QStringLiteral("External registry commit failed"),errorText)) return false;
    if (!sourceTx.commit(QStringLiteral("Local registry mirror commit failed"),errorText)) return false;
    return true;
}

bool Database::hardDeleteOrder(int orderNumber, QString* errorText)
{
    if (orderNumber <= 0) return true;
    const QString beforeJson = LegacyMutationLogSupport::snapshotOrder(db(), orderNumber);
    const QString orderUid = LegacyMutationLogSupport::orderUidForNumber(db(), orderNumber);

    const auto blocked=LocalWorkshopBackend::deletionBlockReason(db(),orderUid);
    if(!blocked.isEmpty()){if(errorText)*errorText=blocked;return false;}

    if (!purgeActiveOrderGraph(db(), orderNumber, errorText)) return false;
    if (!removeOrderFromRegistry(orderNumber, errorText)) return false;

    QString logError;
    LegacyMutationLogSupport::logMutation(db(), QStringLiteral("order"), QStringLiteral("delete"), orderNumber, orderUid, QString::number(orderNumber), beforeJson, QStringLiteral("{}"), &logError);
    if (!logError.trimmed().isEmpty()) qWarning() << "Order delete mutation log failed:" << logError;
    return true;
}

bool Database::removeOrderFromRegistry(int orderNumber, QString* errorText)
{
    if (orderNumber <= 0) return true;
    if (!ensureRegistrySchemaFile()) return true;
    QSqlQuery q(QSqlDatabase::database(dbRegistryConnectionName()));
    q.prepare("DELETE FROM order_registry WHERE order_number=?");
    q.addBindValue(orderNumber);
    if (!q.exec()) {
        if (errorText) *errorText = q.lastError().text();
        return false;
    }
    removePartRegistryFileRows(orderNumber);
    removeRegistryMirrorRow(db(), orderNumber);
    removeWorkspaceOrderStateEntry(db(), orderNumber);
    return true;
}
