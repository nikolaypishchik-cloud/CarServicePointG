#include "infrastructure/db/sqlite/DbWorkspaceOrderStateSupport.h"

#include "infrastructure/db/sqlite/DbArchiveInternals.h"
#include "infrastructure/db/sqlite/DbFacadeSupport.h"
#include "infrastructure/db/sqlite/Db.h"
#include "infrastructure/db/sqlite/DbContext.h"

#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>
#include <QFileInfo>
#include <QHash>
#include <QScopeGuard>
#include <QUuid>
#include "infrastructure/db/sqlite/DbThreadConnectionSupport.h"
#include "infrastructure/db/sqlite/ScopedConnectionManager.h"

bool workspaceOrderStateLookup(QSqlDatabase database, int orderNumber, QString* origin)
{
    if (!database.isValid() || !database.isOpen() || orderNumber <= 0) return false;
    QSqlQuery q(database);
    q.prepare(QStringLiteral("SELECT origin FROM workspace_order_state WHERE order_number=? LIMIT 1"));
    q.addBindValue(orderNumber);
    if (!q.exec() || !q.next()) return false;
    if (origin) *origin = q.value(0).toString();
    return true;
}

bool workspaceOrderStateLookupByUid(QSqlDatabase database, const QString& orderUid, QString* origin)
{
    const QString uid = orderUid.trimmed();
    if (!database.isValid() || !database.isOpen() || uid.isEmpty()) return false;
    QSqlQuery q(database);
    q.prepare(QStringLiteral("SELECT origin FROM workspace_order_state WHERE order_uid=? LIMIT 1"));
    q.addBindValue(uid);
    if (!q.exec() || !q.next()) return false;
    if (origin) *origin = q.value(0).toString();
    return true;
}

QString workspaceOrderSourceDbLookup(QSqlDatabase database, int orderNumber)
{
    if (!database.isValid() || !database.isOpen() || orderNumber <= 0) return QString();
    QSqlQuery q(database);
    q.prepare(QStringLiteral("SELECT source_db FROM workspace_order_state WHERE order_number=? LIMIT 1"));
    q.addBindValue(orderNumber);
    if (!q.exec() || !q.next()) return QString();
    return q.value(0).toString();
}

QString workspaceOrderSourceDbLookupByUid(QSqlDatabase database, const QString& orderUid)
{
    const QString uid = orderUid.trimmed();
    if (!database.isValid() || !database.isOpen() || uid.isEmpty()) return QString();
    QSqlQuery q(database);
    q.prepare(QStringLiteral("SELECT source_db FROM workspace_order_state WHERE order_uid=? LIMIT 1"));
    q.addBindValue(uid);
    if (!q.exec() || !q.next()) return QString();
    return q.value(0).toString();
}

bool routerCacheHasOrderEntry(QSqlDatabase database, int orderNumber)
{
    if (!database.isValid() || !database.isOpen() || orderNumber <= 0) return false;
    QSqlQuery q(database);
    q.prepare(QStringLiteral("SELECT 1 FROM router_cache_work_orders WHERE number=? LIMIT 1"));
    q.addBindValue(orderNumber);
    return q.exec() && q.next();
}

bool upsertWorkspaceOrderStateEntry(QSqlDatabase database,
                                    int orderNumber,
                                    const QString& origin,
                                    const QString& sourceDb,
                                    const QString& orderUid,
                                    const std::function<QString(int)>& fallbackOrderUidResolver,
                                    bool prepareSchema)
{
    if (orderNumber <= 0) return false;
    if (prepareSchema) {
        if (!ensureWorkspaceOrderStateSchema(database)) return false;
    } else if (!database.isValid() || !database.isOpen()) return false;
    QString resolvedUid = orderUid.trimmed();
    if (resolvedUid.isEmpty() && fallbackOrderUidResolver) {
        resolvedUid = fallbackOrderUidResolver(orderNumber).trimmed();
    }

    QSqlQuery q(database);
    q.prepare(
        "INSERT INTO workspace_order_state(order_number, order_uid, origin, source_db, hydrated_at, modified_at) "
        "VALUES(?, ?, ?, ?, CASE WHEN ?='legacy_copy' THEN datetime('now') ELSE COALESCE((SELECT hydrated_at FROM workspace_order_state WHERE order_number=?), '') END, datetime('now')) "
        "ON CONFLICT(order_number) DO UPDATE SET "
        "order_uid=CASE WHEN TRIM(COALESCE(excluded.order_uid,''))<>'' THEN excluded.order_uid ELSE workspace_order_state.order_uid END, "
        "origin=excluded.origin, source_db=excluded.source_db, "
        "hydrated_at=CASE WHEN COALESCE(excluded.hydrated_at,'')<>'' THEN excluded.hydrated_at ELSE workspace_order_state.hydrated_at END, "
        "modified_at=datetime('now')");
    q.addBindValue(orderNumber);
    q.addBindValue(nonNullText(resolvedUid));
    q.addBindValue(nonNullText(origin));
    q.addBindValue(nonNullText(sourceDb));
    q.addBindValue(nonNullText(origin));
    q.addBindValue(orderNumber);
    if (!q.exec() || q.numRowsAffected()!=1) {
        qWarning() << "Workspace order state upsert failed:" << q.lastError().text();
        return false;
    }
    return true;
}

void removeWorkspaceOrderStateEntry(QSqlDatabase database, int orderNumber)
{
    if (!ensureWorkspaceOrderStateSchema(database) || orderNumber <= 0) return;
    QSqlQuery q(database);
    q.prepare(QStringLiteral("DELETE FROM workspace_order_state WHERE order_number=?"));
    q.addBindValue(orderNumber);
    q.exec();
}

bool queueOrderRegistryPublication(QSqlDatabase database,int orderNumber,QString* errorText,bool deferPublication)
{
    auto fail=[&](const QString& text){if(errorText)*errorText=text;return false;};
    if(!database.isValid() || !database.isOpen() || orderNumber<=0) return fail(QStringLiteral("Registry intent requires an open order source"));
    QSqlQuery intent(database);
    if(!intent.prepare(QStringLiteral("UPDATE workspace_order_state SET registry_revision=registry_revision+1 WHERE order_number=? AND TRIM(order_uid)<>'' AND order_uid=(SELECT order_uid FROM work_orders WHERE number=?) AND typeof(registry_revision)='integer' AND typeof(published_registry_revision)='integer' AND published_registry_revision>=0 AND published_registry_revision<=registry_revision AND registry_revision<9223372036854775807"))) return fail(intent.lastError().text());
    intent.addBindValue(orderNumber);intent.addBindValue(orderNumber);
    if(!intent.exec()) return fail(intent.lastError().text());
    if(intent.numRowsAffected()!=1) return fail(QStringLiteral("Workspace registry intent was not recorded"));
    intent.finish();
    // An unregistered SQL/savepoint owner retains its durable pending revision;
    // startup can drain it without committing a caller's transaction early.
    if(deferPublication) TransactionContext::deferUntilCommit(database,[] {
        QString error;
        if(!Database::replayPendingOrderRegistryRefresh(&error)) qWarning()<<"Registry publication remains pending after child mutation"<<error;
    },QStringLiteral("workspace_registry_publication"));
    return true;
}


bool validateStartupRegistryIdentity(QSqlDatabase database,const QString& registryPath,QString* errorText)
{
    auto fail=[&](const QString& detail) {
        if(errorText)*errorText=QStringLiteral("Registry identity requires reconciliation: %1").arg(detail);
        return false;
    };
    if(!database.isValid() || !database.isOpen()) return fail(QStringLiteral("workspace unavailable"));
    if(TransactionContext::hasActiveTransaction(database)) return fail(QStringLiteral("workspace transaction is active"));
    struct Identity { qint64 internal; QString uid; qint64 visible; qint64 year; };
    QHash<qint64,Identity> recorded;
    QHash<QString,qint64> uidOwners,annualOwners;
    QSet<qint64> canonical;
    auto remember=[&](const Identity& id,const QString& source) {
        if(id.internal<=0 || id.internal>2147483647 || id.uid.trimmed().isEmpty()
            || id.visible<=0 || id.visible>2147483647 || id.year<=0 || id.year>9999)
            return fail(source+QStringLiteral(" has invalid identity"));
        const auto annual=QString::number(id.year)+QLatin1Char('/')+QString::number(id.visible);
        if(recorded.contains(id.internal)) {
            const auto previous=recorded.value(id.internal);
            if(previous.uid!=id.uid || previous.visible!=id.visible || previous.year!=id.year)
                return fail(source+QStringLiteral(" conflicts with recorded internal identity"));
        }
        if((uidOwners.contains(id.uid) && uidOwners.value(id.uid)!=id.internal)
            || (annualOwners.contains(annual) && annualOwners.value(annual)!=id.internal))
            return fail(source+QStringLiteral(" conflicts with UID or annual number owner"));
        recorded.insert(id.internal,id);uidOwners.insert(id.uid,id.internal);annualOwners.insert(annual,id.internal);
        return true;
    };
    auto scan=[&](QSqlDatabase source,const QString& table,const QString& internal,const QString& visible,bool headers) {
        QSqlQuery count(source);
        if(!count.exec("SELECT count(*) FROM "+table) || !count.next())return fail(count.lastError().text());
        if(count.value(0).toLongLong()==0)return true;
        count.finish();
        QSqlQuery rows(source);
        const auto sql=QStringLiteral("SELECT %1,order_uid,%2,order_year,typeof(%1),typeof(%2),typeof(order_year)%3 FROM %4")
            .arg(internal,visible,headers?QString():QStringLiteral(",storage_kind"),table);
        if(!rows.exec(sql))return fail(rows.lastError().text());
        while(rows.next()) {
            for(int i=4;i<7;++i)if(rows.value(i).toString()!=QStringLiteral("integer"))return fail(table+QStringLiteral(" has noninteger identity"));
            const Identity id{rows.value(0).toLongLong(),rows.value(1).toString(),rows.value(2).toLongLong(),rows.value(3).toLongLong()};
            if(headers) {
                if(canonical.contains(id.internal) || uidOwners.contains(id.uid))return fail(QStringLiteral("duplicate canonical identity"));
                canonical.insert(id.internal);
            } else {
                const auto kind=rows.value(7).toString();
                if(kind!=QStringLiteral("active") && kind!=QStringLiteral("archive"))return fail(table+QStringLiteral(" has unknown storage kind"));
                if(kind==QStringLiteral("active") && !canonical.contains(id.internal))return fail(table+QStringLiteral(" has active locator without canonical header"));
            }
            if(!remember(id,table))return false;
        }
        return !rows.lastError().isValid() || fail(rows.lastError().text());
    };
    if(!scan(database,QStringLiteral("work_orders"),QStringLiteral("number"),QStringLiteral("order_number"),true))return false;
    if(tableExistsInDb(database,QStringLiteral("workspace_order_state"))) {
        QSqlQuery state(database);
        if(!state.exec("SELECT order_number,order_uid,typeof(order_number) FROM workspace_order_state"))return fail(state.lastError().text());
        while(state.next()) {
            const auto number=state.value(0).toLongLong();
            if(state.value(2).toString()!=QStringLiteral("integer") || !canonical.contains(number) || recorded.value(number).uid!=state.value(1).toString())
                return fail(QStringLiteral("workspace state UID differs from canonical header"));
        }
        if(state.lastError().isValid())return fail(state.lastError().text());
    }
    if(tableExistsInDb(database,QStringLiteral("order_registry"))
        && !scan(database,QStringLiteral("order_registry"),QStringLiteral("order_number"),QStringLiteral("visible_order_number"),false))return false;
    if(!QFileInfo::exists(registryPath))return true;
    // Open only the explicitly selected profile, with no schema preparation or writes.
    const auto name=QStringLiteral("registry_identity_%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    const auto cleanup=qScopeGuard([&]{SqliteConnectionUtils::safeCloseAndRemoveDatabase(name);});
    auto registry=QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),name);
    registry.setDatabaseName(registryPath);registry.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
    DbThreadConnectionSupport::registerConnectionName(name,registryPath);
    if(!registry.open())return fail(registry.lastError().text());
    if(!tableExistsInDb(registry,QStringLiteral("order_registry"))) {
        QSqlQuery tables(registry);
        if(!tables.exec("SELECT count(*) FROM sqlite_master WHERE type='table'") || !tables.next())return fail(tables.lastError().text());
        return tables.value(0).toInt()==0 || fail(QStringLiteral("registry identity table is absent"));
    }
    return scan(registry,QStringLiteral("order_registry"),QStringLiteral("order_number"),QStringLiteral("visible_order_number"),false);
}


bool validateArchiveShardIdentity(QSqlDatabase database,QString* errorText)
{
    auto fail=[&](const QString& text){if(errorText)*errorText=QStringLiteral("Archive identity requires reconciliation: %1").arg(text);return false;};
    if(tableExistsInDb(database,QStringLiteral("work_orders"))
        && !validateStartupRegistryIdentity(database,QString(),errorText))return false;
    for(const auto& table:QStringList{QStringLiteral("work_order_services"),QStringLiteral("work_order_parts")}) {
        if(!tableExistsInDb(database,table))continue;
        QSqlQuery count(database);
        if(!count.exec("SELECT COUNT(*) FROM "+table) || !count.next())return fail(count.lastError().text());
        if(count.value(0).toLongLong()==0)continue;
        count.finish();
        QSqlQuery invalid(database);
        if(!invalid.exec("SELECT 1 FROM "+table+" c LEFT JOIN work_orders o ON o.number=c.order_number WHERE o.number IS NULL OR TRIM(COALESCE(c.order_uid,''))='' OR c.order_uid<>o.order_uid LIMIT 1"))return fail(invalid.lastError().text());
        if(invalid.next())return fail(table+QStringLiteral(" parent UID conflicts with numeric owner"));
        if(invalid.lastError().isValid())return fail(invalid.lastError().text());
        invalid.finish();
        if(table==QStringLiteral("work_order_parts")) {
            if(!invalid.exec("SELECT part_uid FROM work_order_parts GROUP BY part_uid HAVING TRIM(COALESCE(part_uid,''))='' OR COUNT(*)<>1 LIMIT 1"))return fail(invalid.lastError().text());
            if(invalid.next())return fail(QStringLiteral("part UID missing or duplicated"));
            if(invalid.lastError().isValid())return fail(invalid.lastError().text());
        }
    }
    return true;
}
