#include "infrastructure/db/sqlite/MigrationDestinationBaseline.h"
#include "core/services/legacyimport/TargetConsistencyGate.h"

#include "runtime/AppRuntime.h"
#include "core/services/legacyimport/MigrationPreflightValidator.h"
#include "infrastructure/db/sqlite/Db.h"
#include "infrastructure/db/sqlite/ScopedConnectionManager.h"

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStringList>

namespace {

int sourceEntityCount(const LegacyImportAuditReport& report, const QString& sourceTable)
{
    for (const LegacyImportEntityAudit& entity : report.entities) {
        if (entity.sourceTable == sourceTable) return qMax(0, entity.rowCount);
    }
    return 0;
}

int scalarCount(QSqlDatabase db, const QString& sql)
{
    QSqlQuery q(db);
    if (!q.exec(sql) || !q.next()) return -1;
    return q.value(0).toInt();
}

bool tableOrViewExists(QSqlDatabase db,
                       const QString& name,
                       const QString& typePredicate = QStringLiteral("type IN ('table','view')"))
{
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT 1 FROM sqlite_master WHERE %1 AND name=? LIMIT 1").arg(typePredicate));
    q.addBindValue(name);
    return q.exec() && q.next();
}

} // namespace

bool TargetConsistencyGate::validateCurrentRuntime(const LegacyImportAuditReport& report,
                                                   QString* errorText)
{
    QString runtimePreflightError;
    if (!MigrationPreflightValidator::validateRuntimeTargets(&runtimePreflightError)) {
        if (errorText) *errorText = runtimePreflightError;
        return false;
    }

    QString refreshError;
    if (!Database::runStartupRegistryRefresh(&refreshError)) {
        if (errorText) *errorText = refreshError.trimmed().isEmpty()
            ? QStringLiteral("Startup registry refresh failed before target consistency gate")
            : refreshError;
        return false;
    }

    QString connectionError;
    ScopedSqliteConnection workspaceConn(AppRuntime::activeDbFilePath(), QStringLiteral("target_consistency_gate"), &connectionError);
    if (!workspaceConn.isOpen()) {
        if (errorText) *errorText = connectionError.trimmed().isEmpty()
            ? QStringLiteral("Workspace DB is not open for target consistency gate")
            : connectionError;
        return false;
    }
    const QSqlDatabase workspaceDb = workspaceConn.db();

    QStringList failures;
    const auto expected = [&](const QString& entity) -> qint64 {
        const auto retained = MigrationDestinationBaseline::before(workspaceDb,entity);
        if (retained < 0) failures << QStringLiteral("Missing destination baseline for %1").arg(entity);
        return retained + sourceEntityCount(report,entity);
    };
    const auto expectedOrders=expected("work_orders");
    const auto expectedServices=expected("work_order_services");
    const auto expectedParts=expected("work_order_parts");

    const int tgtOrders = scalarCount(workspaceDb, QStringLiteral("SELECT COUNT(*) FROM work_orders"));
    const int tgtServices = scalarCount(workspaceDb, QStringLiteral("SELECT COUNT(*) FROM work_order_services"));
    const int tgtParts = scalarCount(workspaceDb, QStringLiteral("SELECT COUNT(*) FROM work_order_parts"));
    const int stateOrders = tableOrViewExists(workspaceDb, QStringLiteral("workspace_order_state"), QStringLiteral("type='table'"))
        ? scalarCount(workspaceDb, QStringLiteral("SELECT COUNT(*) FROM workspace_order_state")) : -1;
    const int regOrders = tableOrViewExists(workspaceDb, QStringLiteral("order_registry"), QStringLiteral("type='table'"))
        ? scalarCount(workspaceDb, QStringLiteral("SELECT COUNT(DISTINCT order_uid) FROM order_registry")) : -1;
    const int routerOpen = tableOrViewExists(workspaceDb, QStringLiteral("router_open_order_locator"), QStringLiteral("type='view'"))
        ? scalarCount(workspaceDb, QStringLiteral("SELECT COUNT(*) FROM router_open_order_locator")) : -1;
    const int routerClosed = tableOrViewExists(workspaceDb, QStringLiteral("router_closed_order_locator"), QStringLiteral("type='view'"))
        ? scalarCount(workspaceDb, QStringLiteral("SELECT COUNT(*) FROM router_closed_order_locator")) : -1;
    const int routerOrders = tableOrViewExists(workspaceDb, QStringLiteral("router_cache_work_orders"), QStringLiteral("type='table'"))
        ? scalarCount(workspaceDb, QStringLiteral("SELECT COUNT(*) FROM router_cache_work_orders")) : -1;
    const int routerServices = tableOrViewExists(workspaceDb, QStringLiteral("router_cache_work_order_services"), QStringLiteral("type='table'"))
        ? scalarCount(workspaceDb, QStringLiteral("SELECT COUNT(*) FROM router_cache_work_order_services")) : -1;
    const int routerParts = tableOrViewExists(workspaceDb, QStringLiteral("router_cache_work_order_parts"), QStringLiteral("type='table'"))
        ? scalarCount(workspaceDb, QStringLiteral("SELECT COUNT(*) FROM router_cache_work_order_parts")) : -1;

    if (tgtOrders != expectedOrders) failures << QStringLiteral("work_orders expected=%1 target=%2").arg(expectedOrders).arg(tgtOrders);
    if (tgtServices != expectedServices) failures << QStringLiteral("work_order_services expected=%1 target=%2").arg(expectedServices).arg(tgtServices);
    if (tgtParts != expectedParts) failures << QStringLiteral("work_order_parts expected=%1 target=%2").arg(expectedParts).arg(tgtParts);
    if (regOrders != expectedOrders) failures << QStringLiteral("order_registry expected=%1 target=%2").arg(expectedOrders).arg(regOrders);
    if (stateOrders != expectedOrders) failures << QStringLiteral("workspace_order_state expected=%1 target=%2").arg(expectedOrders).arg(stateOrders);
    if (routerOrders != expectedOrders) failures << QStringLiteral("router_cache_work_orders expected=%1 target=%2").arg(expectedOrders).arg(routerOrders);
    if (routerServices != expectedServices) failures << QStringLiteral("router_cache_work_order_services expected=%1 target=%2").arg(expectedServices).arg(routerServices);
    if (routerParts != expectedParts) failures << QStringLiteral("router_cache_work_order_parts expected=%1 target=%2").arg(expectedParts).arg(routerParts);
    if (routerOpen < 0) failures << QStringLiteral("missing router_open_order_locator");
    if (routerClosed < 0) failures << QStringLiteral("missing router_closed_order_locator");

    if (!failures.isEmpty()) {
        if (errorText) *errorText = QStringLiteral("Target consistency gate failed: %1").arg(failures.join(QStringLiteral(" | ")));
        return false;
    }
    return true;
}
