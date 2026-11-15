#include "core/services/orders/CurrentOrdersRefreshJob.h"
#include "core/services/AccessControlService.h"

#include "core/readmodel/orders/CurrentOrdersViewModelBuilder.h"
#include "core/cache/OrderSnapshotCache.h"
#include "infrastructure/db/sqlite/Db.h"
#include <QSqlDriver>
#include <QSqlQuery>
#include "core/services/AppRuntimeService.h"
#include "runtime/AsyncThreadScope.h"
#include "runtime/AppRuntime.h"

namespace CurrentOrdersRefreshJob {

// Called on the page thread: SQLite data_version is comparable only on the same connection.
bool refreshWorkspaceRevision()
{
    const auto db = Database::db();
    auto* driver = db.driver();
    QSqlQuery version(db);
    const bool readable = db.isOpen() && driver
        && version.exec(QStringLiteral("PRAGMA main.data_version")) && version.next();
    if (readable) {
        const auto value = version.value(0).toLongLong();
        const char* property = "currentOrdersWorkspaceVersion";
        const QVariant previous = driver->property(property);
        if (previous.isValid() && previous.toLongLong() == value) return false;
        driver->setProperty(property, value);
    }
    OrderSnapshotCache::invalidateCurrentOrders(QStringLiteral("workspace_revision"));
    return true;
}

Result run(const QString& filter, const std::shared_ptr<std::atomic_bool>& cancelToken)
{
    auto cancelled = [&cancelToken]() { return AsyncThreadScope::isCancellationRequested(cancelToken); };

    Result result;
    if (cancelled()) {
        result.cancelled = true;
        AppRuntimeService::closeThreadConnection();
        return result;
    }

    const QString cacheKey = OrderSnapshotCache::makeCurrentOrdersKey(AppRuntime::activeDbFilePath(), filter)
        + QLatin1Char('|') + AccessControlService::currentScopeKey();
    OrderSnapshotCache::CurrentOrdersSnapshot cached;
    if (!AccessControlService::currentActorIsMaster() && OrderSnapshotCache::tryGetCurrentOrders(cacheKey, &cached)) {
        result.queryResult = cached.queryResult;
        result.readModel = cached.readModel;
        AppRuntimeService::closeThreadConnection();
        return result;
    }

    result.queryResult = CurrentOrdersQueryService::loadOpenOrders(filter);
    if (cancelled()) {
        result.cancelled = true;
        AppRuntimeService::closeThreadConnection();
        return result;
    }

    if (result.queryResult.hasSource) {
        result.readModel = CurrentOrdersViewModelBuilder::build(result.queryResult);
        OrderSnapshotCache::storeCurrentOrders(cacheKey, {result.queryResult, result.readModel});
    }

    if (cancelled()) {
        result.cancelled = true;
    }
    AppRuntimeService::closeThreadConnection();
    return result;
}

} // namespace CurrentOrdersRefreshJob
