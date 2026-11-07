#include "infrastructure/db/sqlite/Db.h"
#include "infrastructure/db/sqlite/DbContext.h"
#include "infrastructure/db/sqlite/OrderDeletionPublication.h"
#include "infrastructure/db/sqlite/DbWorkspaceOrderStateSupport.h"
#include "infrastructure/db/sqlite/ScopedConnectionManager.h"
#include "infrastructure/db/sqlite/DbFacadeSupport.h"
#include "infrastructure/db/sqlite/DbArchiveInternals.h"
#include "infrastructure/db/sqlite/DbArchiveRouterCacheImportSupport.h"
#include "infrastructure/db/sqlite/LegacyCanonicalImportNormalization.h"
#include "infrastructure/db/sqlite/LegacyCanonicalAncillaryImportSupport.h"
#include "infrastructure/db/sqlite/DbSchemaAuditSupport.h"
#include "infrastructure/db/sqlite/DbSettingsCacheSupport.h"
#include "infrastructure/db/sqlite/DbThreadConnectionSupport.h"
#include "infrastructure/db/sqlite/RuntimeDbPreflightValidator.h"
#include "infrastructure/db/sqlite/OrderArchiveShardSqlSupport.h"
#include "core/services/LegacyMutationLogSupport.h"
#include "core/services/OrderIdentityService.h"
#include "core/services/OrderStoragePolicyService.h"
#include "core/services/legacyimport/RuntimeRecoveryCoordinator.h"
#include "core/services/OrderArchiveRotationService.h"
#include "core/services/OrderDetailIntegrityValidator.h"
#include "core/services/legacyimport/LegacyOrderChildMapper.h"
#include "infrastructure/security/EncryptedStorage.h"
#include "runtime/AppRuntime.h"
#include "core/diagnostics/RuntimeLoggingControl.h"

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
#include <QScopeGuard>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QTextStream>
#include <QUuid>
#include <QVariant>
#include <QVector>

#include <algorithm>
#include <limits>


void Database::close()
{
    const QStringList names{currentConnectionName(), dbRegistryConnectionName()};
    // Both handles belong to one profile. Do not partially tear it down while
    // an owned operation can still commit or roll back on either connection.
    for (const auto& connName : names) {
        if (!QSqlDatabase::contains(connName)) continue;
        if (!DbThreadConnectionSupport::isConnectionOwnedByCurrentThread(connName)
            || TransactionContext::hasActiveTransaction(QSqlDatabase::database(connName, false))) {
            qWarning() << "Profile close deferred for active/foreign connection" << connName;
            return;
        }
    }
    for (const auto& connName : names) {
        if (!QSqlDatabase::contains(connName)) {
            if (DbThreadConnectionSupport::isConnectionOwnedByCurrentThread(connName)) DbThreadConnectionSupport::unregisterConnectionName(connName);
            continue;
        }
        {
            QSqlDatabase handle = QSqlDatabase::database(connName, false);
            if (handle.isValid() && handle.isOpen()) handle.close();
            handle = QSqlDatabase();
        }
        SqliteConnectionUtils::safeCloseAndRemoveDatabase(connName);
        DbThreadConnectionSupport::unregisterConnectionName(connName);
    }
    if (currentConnectionName() == mainConnectionName()) DbSettingsCacheSupport::clear();
}

bool Database::runStartupSchemaCheck(QString* errorText)
{
    if (!ensureSchema()) {
        if (errorText) *errorText = QStringLiteral("Schema check failed");
        return false;
    }

    QSqlDatabase d = db();
    if (!d.isValid() || (!d.isOpen() && !d.open())) {
        if (errorText) *errorText = d.lastError().text();
        return false;
    }
    if (!ensureRouterCacheSchema(d) || !rebuildRouterViews(d)) {
        if (errorText) *errorText = QStringLiteral("Router schema/view rebuild failed");
        return false;
    }

    QString detailError;
    if (!OrderDetailIntegrityValidator::ensureCanonicalDetailSchema(d, &detailError)
        || !OrderDetailIntegrityValidator::repairCanonicalOrderUids(d, &detailError)
        || !OrderDetailIntegrityValidator::validateAfterImport(d, &detailError)) {
        if (errorText) *errorText = detailError.trimmed().isEmpty() ? QStringLiteral("Order detail integrity validation failed") : detailError;
        return false;
    }

    QString auditError;
    if (!ensureCatalogImportSchema(&auditError)) {
        if (errorText) *errorText = auditError.trimmed().isEmpty() ? QStringLiteral("Catalog schema prepare failed") : auditError;
        return false;
    }

    // ensureCatalogImportSchema() may recurse into ensureSchema(); keep the router
    // derived views freshly rebuilt right before the audit/preflight boundary.
    if (!ensureRouterCacheSchema(d) || !rebuildRouterViews(d)) {
        if (errorText) *errorText = QStringLiteral("Router schema/view rebuild failed after catalog schema prepare");
        return false;
    }

    auditError.clear();
    if (!DbSchemaAuditSupport::runActiveSchemaAudit(d, &auditError)) {
        if (errorText) *errorText = auditError.trimmed().isEmpty() ? QStringLiteral("Schema contract audit failed") : auditError;
        return false;
    }

    auditError.clear();
    if (!RuntimeDbPreflightValidator::validateFile(AppRuntime::activeDbFilePath(), RuntimeDbPreflightValidator::DbRole::Workspace, &auditError)) {
        if (errorText) *errorText = auditError.trimmed().isEmpty() ? QStringLiteral("Workspace DB preflight failed") : auditError;
        return false;
    }
    return true;
}

bool Database::runStartupMigrationCheck(QString* errorText)
{
    const QString connName = currentConnectionName();
    QSqlDatabase d = QSqlDatabase::contains(connName) ? QSqlDatabase::database(connName, false) : QSqlDatabase();
    if (!d.isValid() || (!d.isOpen() && !d.open())) {
        if (errorText) *errorText = d.lastError().text();
        return false;
    }

    QString rewriteError;
    if (!rewriteLegacyToCanonicalWorkspaceIfNeeded(d, &rewriteError)) {
        if (errorText) *errorText = rewriteError.trimmed().isEmpty() ? QStringLiteral("Canonical migration failed") : rewriteError;
        return false;
    }

    if (!ensureSchema()) {
        if (errorText) *errorText = QStringLiteral("Schema re-check failed after migration");
        return false;
    }
    if (!ensureRouterCacheSchema(d) || !rebuildRouterViews(d)) {
        if (errorText) *errorText = QStringLiteral("Router schema/view rebuild failed after migration");
        return false;
    }

    QString detailError;
    if (!OrderDetailIntegrityValidator::ensureCanonicalDetailSchema(d, &detailError)
        || !OrderDetailIntegrityValidator::repairCanonicalOrderUids(d, &detailError)
        || !OrderDetailIntegrityValidator::validateAfterImport(d, &detailError)) {
        if (errorText) *errorText = detailError.trimmed().isEmpty() ? QStringLiteral("Order detail integrity validation failed after migration") : detailError;
        return false;
    }

    QString auditError;
    if (!DbSchemaAuditSupport::runActiveSchemaAudit(d, &auditError)) {
        if (errorText) *errorText = auditError.trimmed().isEmpty() ? QStringLiteral("Schema contract audit failed after migration") : auditError;
        return false;
    }
    auditError.clear();
    if (!RuntimeDbPreflightValidator::validateFile(AppRuntime::activeDbFilePath(), RuntimeDbPreflightValidator::DbRole::Workspace, &auditError)) {
        if (errorText) *errorText = auditError.trimmed().isEmpty() ? QStringLiteral("Workspace DB preflight failed after migration") : auditError;
        return false;
    }
    return true;
}

bool Database::runStartupArchiveMaintenance(QString* errorText)
{
    QString pruneError;
    if (!OrderArchiveRotationService::enforcePolicy(&pruneError)) {
        if (errorText) *errorText = pruneError.trimmed().isEmpty() ? QStringLiteral("Archive rollover maintenance failed") : pruneError;
        return false;
    }
    return true;
}

bool Database::archiveSingleActiveOrderToArchive(int orderNumber, QString* errorText)
{
    QSqlDatabase d = db();
    if (!d.isValid() || (!d.isOpen() && !d.open())) {
        if (errorText) *errorText = d.lastError().text();
        return false;
    }
    return archiveSingleActiveOrderToShard(d, orderNumber, errorText);
}

bool Database::rotateActiveClosedOrders(QString* errorText)
{
    QSqlDatabase d = db();
    if (!d.isValid() || (!d.isOpen() && !d.open())) {
        if (errorText) *errorText = d.lastError().text();
        return false;
    }
    return OrderArchiveShardSqlSupport::pruneActiveClosedOrdersToArchive(d, errorText);
}

bool Database::cleanupHydratedArchiveCopies(QString* errorText)
{
    QSqlDatabase d = db();
    if (!d.isValid() || (!d.isOpen() && !d.open())) {
        if (errorText) *errorText = d.lastError().text();
        return false;
    }
    return purgeHydratedArchiveCopiesFromActive(d, errorText);
}

bool Database::runStartupRegistryRefresh(QString* errorText)
{
    QSqlDatabase d = db();
    if (!d.isValid() || (!d.isOpen() && !d.open())) {
        if (errorText) *errorText = d.lastError().text();
        return false;
    }

    QString preflightError;
    if (!RuntimeDbPreflightValidator::validateFile(AppRuntime::activeDbFilePath(), RuntimeDbPreflightValidator::DbRole::Workspace, &preflightError)) {
        if (errorText) *errorText = QStringLiteral("Active workspace preflight failed before registry refresh: %1").arg(preflightError);
        return false;
    }

    // A retired header can still have its exact former locator after a process interruption.
    if (!OrderDeletionPublication::replay(d,errorText)) return false;
    if (!validateStartupRegistryIdentity(d, AppRuntime::registryDbFilePath(), errorText)) return false;

    // Pending canonical commits take precedence over cached stamps and bulk mirrors.
    if (!replayPendingOrderRegistryRefresh(errorText)) return false;

    // Bootstrap the registry file first; a fresh import may not have created
    // it yet, but the refresh path is exactly where it should be materialized.
    if (!ensurePartRegistrySchemaFile()) {
        if (errorText) *errorText = QStringLiteral("Registry schema is unavailable");
        return false;
    }
    {
        QSqlQuery recovery(QSqlDatabase::database(dbRegistryConnectionName(),false));
        if(!recovery.exec("SELECT value FROM registry_meta WHERE key='registry_rebuild_pending'")){if(errorText)*errorText=recovery.lastError().text();return false;}
        const bool pending=recovery.next() && recovery.value(0).toString()==QStringLiteral("1");
        if(recovery.lastError().isValid()){if(errorText)*errorText=recovery.lastError().text();return false;}
        recovery.finish();
        if(pending && !rebuildRegistryDerivedState(d,errorText))return false;
    }
    if (!RuntimeDbPreflightValidator::validateFile(AppRuntime::registryDbFilePath(), RuntimeDbPreflightValidator::DbRole::Registry, &preflightError)) {
        if (errorText) *errorText = QStringLiteral("Registry preflight failed before registry refresh: %1").arg(preflightError);
        return false;
    }

    const auto fileStamp = [](const QString& path) -> QString {
        const QFileInfo fi(path);
        if (!fi.exists()) return QStringLiteral("missing");
        return QStringLiteral("%1|%2|%3")
            .arg(fi.absoluteFilePath())
            .arg(fi.size())
            .arg(fi.lastModified().toUTC().toString(Qt::ISODateWithMs));
    };
    auto registryMetaValue = [](QSqlDatabase registryDb, const QString& key) -> QString {
        QSqlQuery q(registryDb);
        q.prepare(QStringLiteral("SELECT value FROM registry_meta WHERE key=?"));
        q.addBindValue(key);
        if (!q.exec() || !q.next()) return QString();
        return q.value(0).toString();
    };
    auto upsertRegistryMetaValue = [&](QSqlDatabase registryDb, const QString& key, const QString& value) {
        QSqlQuery q(registryDb);
        if (!q.prepare(QStringLiteral("INSERT INTO registry_meta(key, value) VALUES(?, ?) ON CONFLICT(key) DO UPDATE SET value=excluded.value"))) {
            if (errorText) *errorText=q.lastError().text();
            return false;
        }
        q.addBindValue(key);
        q.addBindValue(value);
        if (!q.exec() || q.numRowsAffected()!=1) {
            if (errorText) *errorText=q.lastError().isValid()?q.lastError().text():QStringLiteral("Registry readiness stamp was not recorded");
            return false;
        }
        return true;
    };

    QSqlDatabase registryDb = QSqlDatabase::database(dbRegistryConnectionName());
    QSqlQuery pendingBulk(registryDb);
    if (!pendingBulk.exec(QStringLiteral("SELECT value FROM registry_meta WHERE key='runtime_bulk_pending'"))) {
        if (errorText) *errorText=pendingBulk.lastError().text();
        return false;
    }
    const bool bulkPending=pendingBulk.next() && pendingBulk.value(0).toString()==QStringLiteral("1");
    if (pendingBulk.lastError().isValid()) {if(errorText)*errorText=pendingBulk.lastError().text();return false;}
    pendingBulk.finish();
    const QString activeStamp = fileStamp(AppRuntime::activeDbFilePath());
    const QString archiveStamp = fileStamp(AppRuntime::archiveDbFilePath());
    const QString cachedActiveStamp = registryMetaValue(registryDb, QStringLiteral("runtime_active_stamp"));
    const QString cachedArchiveStamp = registryMetaValue(registryDb, QStringLiteral("runtime_archive_stamp"));
    const bool mirrorWarm = tableExistsInDb(d, QStringLiteral("order_registry")) && tableExistsInDb(d, QStringLiteral("router_cache_work_orders"));
    const bool registryWarm = !registryMetaValue(registryDb, QStringLiteral("active_path")).trimmed().isEmpty();
    const auto scalarLocalCount = [&](const QString& sql) -> int {
        QSqlQuery q(d);
        if (!q.exec(sql) || !q.next()) return 0;
        return q.value(0).toInt();
    };
    const bool routerViewsExist = tableExistsInDb(d, QStringLiteral("router_open_order_locator"), QStringLiteral("type='view'"))
        && tableExistsInDb(d, QStringLiteral("router_closed_order_locator"), QStringLiteral("type='view'"));
    const int warmWorkOrders = tableExistsInDb(d, QStringLiteral("work_orders"))
        ? scalarLocalCount(QStringLiteral("SELECT COUNT(*) FROM work_orders")) : 0;
    const int warmWorkServices = tableExistsInDb(d, QStringLiteral("work_order_services"))
        ? scalarLocalCount(QStringLiteral("SELECT COUNT(*) FROM work_order_services")) : 0;
    const int warmWorkParts = tableExistsInDb(d, QStringLiteral("work_order_parts"))
        ? scalarLocalCount(QStringLiteral("SELECT COUNT(*) FROM work_order_parts")) : 0;
    const int warmRegistry = tableExistsInDb(d, QStringLiteral("order_registry"))
        ? scalarLocalCount(QStringLiteral("SELECT COUNT(DISTINCT order_uid) FROM order_registry")) : 0;
    const int warmWorkspaceState = tableExistsInDb(d, QStringLiteral("workspace_order_state"))
        ? scalarLocalCount(QStringLiteral("SELECT COUNT(*) FROM workspace_order_state")) : 0;
    const int warmRouterOrders = tableExistsInDb(d, QStringLiteral("router_cache_work_orders"))
        ? scalarLocalCount(QStringLiteral("SELECT COUNT(*) FROM router_cache_work_orders")) : 0;
    const int warmRouterServices = tableExistsInDb(d, QStringLiteral("router_cache_work_order_services"))
        ? scalarLocalCount(QStringLiteral("SELECT COUNT(*) FROM router_cache_work_order_services")) : 0;
    const int warmRouterParts = tableExistsInDb(d, QStringLiteral("router_cache_work_order_parts"))
        ? scalarLocalCount(QStringLiteral("SELECT COUNT(*) FROM router_cache_work_order_parts")) : 0;
    const bool fastPathHealthy = warmWorkOrders <= 0
        || (warmRegistry >= warmWorkOrders
            && warmWorkspaceState >= warmWorkOrders
            && warmRouterOrders >= warmWorkOrders
            && warmRouterServices >= warmWorkServices
            && warmRouterParts >= warmWorkParts
            && routerViewsExist);
    if (!bulkPending && mirrorWarm && registryWarm && cachedActiveStamp == activeStamp && cachedArchiveStamp == archiveStamp && fastPathHealthy && routerCacheContentReady(d)) {
        if (RuntimeLoggingControl::deepDebugEnabled()) qInfo() << "Registry refresh fast-path: stamps unchanged, heavy sync skipped";
        QSqlQuery viewDefinition(d);
        if (viewDefinition.exec("SELECT sql FROM sqlite_master WHERE name='router_closed_order_locator'")
            && viewDefinition.next() && !viewDefinition.value(0).toString().contains(QStringLiteral("'paid'"))) {
            return rebuildRouterViews(d);
        }
        return true;
    }
    if (mirrorWarm && registryWarm && cachedActiveStamp == activeStamp && cachedArchiveStamp == archiveStamp && !fastPathHealthy) {
        qWarning() << "Registry refresh fast-path bypassed due to sparse or incomplete derived state"
                   << "work" << warmWorkOrders << warmWorkServices << warmWorkParts
                   << "registry" << warmRegistry
                   << "workspace_state" << warmWorkspaceState
                   << "cache" << warmRouterOrders << warmRouterServices << warmRouterParts
                   << "views" << routerViewsExist;
    }

    QString registryProblem;
    QString registryAuditError;
    if (!DbSchemaAuditSupport::runRegistrySchemaAudit(registryDb, &registryAuditError)) {
        registryProblem = registryAuditError.trimmed().isEmpty() ? QStringLiteral("Registry contract audit failed") : registryAuditError;
    }

    if (registryProblem.trimmed().isEmpty()) {
        QString syncError;
        if (!syncLocalRegistryMirrorFromRegistryFile(d, &syncError)) {
            registryProblem = syncError.trimmed().isEmpty() ? QStringLiteral("Local registry mirror sync failed") : syncError;
        }
    }

    if (!registryProblem.trimmed().isEmpty()) {
        qWarning() << "Registry refresh fallback rebuild triggered:" << registryProblem;
        QString rebuildError;
        if (!rebuildRegistryDerivedState(d, &rebuildError)) {
            if (errorText) *errorText = rebuildError.trimmed().isEmpty() ? registryProblem : rebuildError;
            return false;
        }
    }

    QString routerCacheError;
    if (!refreshArchiveIntoRouterCache(d)) {
        if (errorText) *errorText = routerCacheError.trimmed().isEmpty() ? QStringLiteral("Archive router cache refresh failed") : routerCacheError;
        return false;
    }

    if (!rebuildRouterViews(d)) {
        if (errorText) *errorText = QStringLiteral("Router view refresh failed");
        return false;
    }
    if (!upsertActiveOrdersIntoRegistry(errorText)) return false;
    if (!rebuildRouterViews(d)) {
        if (errorText) *errorText = QStringLiteral("Router view refresh failed after registry sync");
        return false;
    }

    const auto scalarRegistryCount = [&](const QString& sql) -> int {
        QSqlQuery q(d);
        if (!q.exec(sql) || !q.next()) return 0;
        return q.value(0).toInt();
    };
    const int workOrdersCount = tableExistsInDb(d, QStringLiteral("work_orders"))
        ? scalarRegistryCount(QStringLiteral("SELECT COUNT(*) FROM work_orders")) : 0;
    const int workServicesCount = tableExistsInDb(d, QStringLiteral("work_order_services"))
        ? scalarRegistryCount(QStringLiteral("SELECT COUNT(*) FROM work_order_services")) : 0;
    const int workPartsCount = tableExistsInDb(d, QStringLiteral("work_order_parts"))
        ? scalarRegistryCount(QStringLiteral("SELECT COUNT(*) FROM work_order_parts")) : 0;
    const int registryCount = tableExistsInDb(d, QStringLiteral("order_registry"))
        ? scalarRegistryCount(QStringLiteral("SELECT COUNT(*) FROM order_registry")) : 0;
    int routerCacheCount = tableExistsInDb(d, QStringLiteral("router_cache_work_orders"))
        ? scalarRegistryCount(QStringLiteral("SELECT COUNT(*) FROM router_cache_work_orders")) : 0;
    int routerServicesCount = tableExistsInDb(d, QStringLiteral("router_cache_work_order_services"))
        ? scalarRegistryCount(QStringLiteral("SELECT COUNT(*) FROM router_cache_work_order_services")) : 0;
    int routerPartsCount = tableExistsInDb(d, QStringLiteral("router_cache_work_order_parts"))
        ? scalarRegistryCount(QStringLiteral("SELECT COUNT(*) FROM router_cache_work_order_parts")) : 0;

    const bool routerUnderfilled = workOrdersCount > 0 && (routerCacheCount < workOrdersCount || routerServicesCount < workServicesCount || routerPartsCount < workPartsCount);
    if (routerUnderfilled) {
        qWarning() << "Registry refresh detected sparse router cache; forcing active-workspace rebuild"
                   << "active" << workOrdersCount << workServicesCount << workPartsCount
                   << "cache" << routerCacheCount << routerServicesCount << routerPartsCount;
        QString forceError;
        if (!forceRebuildRouterCacheFromActiveWorkspace(d, &forceError)) {
            if (errorText) *errorText = forceError.trimmed().isEmpty() ? QStringLiteral("Forced router cache rebuild from active workspace failed") : forceError;
            return false;
        }
        if (!upsertActiveOrdersIntoRegistry(errorText)) return false;
        if (!rebuildRouterViews(d)) {
            if (errorText) *errorText = QStringLiteral("Router view refresh failed after forced active rebuild");
            return false;
        }
        routerCacheCount = tableExistsInDb(d, QStringLiteral("router_cache_work_orders"))
            ? scalarRegistryCount(QStringLiteral("SELECT COUNT(*) FROM router_cache_work_orders")) : 0;
        routerServicesCount = tableExistsInDb(d, QStringLiteral("router_cache_work_order_services"))
            ? scalarRegistryCount(QStringLiteral("SELECT COUNT(*) FROM router_cache_work_order_services")) : 0;
        routerPartsCount = tableExistsInDb(d, QStringLiteral("router_cache_work_order_parts"))
            ? scalarRegistryCount(QStringLiteral("SELECT COUNT(*) FROM router_cache_work_order_parts")) : 0;
    }

    if (workOrdersCount > 0 && (registryCount <= 0 || routerCacheCount <= 0 || routerServicesCount < workServicesCount || routerPartsCount < workPartsCount)) {
        if (errorText) {
            *errorText = QStringLiteral("Registry refresh consistency gate failed: work_orders=%1 order_registry=%2 router_cache_work_orders=%3 router_cache_work_order_services=%4 router_cache_work_order_parts=%5")
                .arg(workOrdersCount)
                .arg(registryCount)
                .arg(routerCacheCount)
                .arg(routerServicesCount)
                .arg(routerPartsCount);
        }
        return false;
    }

    TransactionContext stamps(registryDb,QStringLiteral("RegistryReadinessStamps"));
    if (!stamps.begin(QStringLiteral("Cannot start registry readiness stamps"),errorText)) return false;
    const auto stampsRollback=qScopeGuard([&]{if(stamps.isActive()){QString ignored;stamps.rollback(QString(),&ignored);}});
    if (!upsertRegistryMetaValue(registryDb, QStringLiteral("runtime_active_stamp"), activeStamp)
        || !upsertRegistryMetaValue(registryDb, QStringLiteral("runtime_archive_stamp"), archiveStamp)
        || !upsertRegistryMetaValue(registryDb, QStringLiteral("runtime_last_refresh_utc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs))) return false;
    return stamps.commit(QStringLiteral("Cannot commit registry readiness stamps"),errorText);
}
