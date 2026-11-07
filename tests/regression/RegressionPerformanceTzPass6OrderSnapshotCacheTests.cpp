#include "TestSourceAssertions.h"

class RegressionPerformanceTzPass6OrderSnapshotCacheTests final : public QObject
{
    Q_OBJECT
private slots:
    void currentAndHistoryRefreshJobsUseCentralOrderSnapshotCache()
    {
        using namespace cs::tests;
        requireContains("src/core/cache/OrderSnapshotCache.h", "makeCurrentOrdersKey");
        requireContains("src/core/cache/OrderSnapshotCache.h", "makeHistoryKey");
        requireContains("src/core/services/orders/CurrentOrdersRefreshJob.cpp", "OrderSnapshotCache::tryGetCurrentOrders(cacheKey, &cached)");
        requireContains("src/core/services/orders/CurrentOrdersRefreshJob.cpp", "OrderSnapshotCache::storeCurrentOrders(cacheKey, {result.queryResult, result.readModel})");
        requireContains("src/core/services/HistoryRefreshJob.cpp", "OrderSnapshotCache::tryGetHistory(cacheKey, &cached)");
        requireContains("src/core/services/HistoryRefreshJob.cpp", "OrderSnapshotCache::storeHistory(cacheKey, {result.queryResult, result.readModel})");
    }

    void mutationInvalidatorClearsOrdersHistoryPartsAndReportsTogether()
    {
        using namespace cs::tests;
        requireContains("src/core/cache/SnapshotCacheInvalidator.cpp", "OrderSnapshotCache::invalidateAll(reason)");
        requireContains("src/core/cache/SnapshotCacheInvalidator.cpp", "families=current_orders,history,orders,parts");
        requireContains("src/core/cache/OrderSnapshotCache.cpp", "ORDER_SNAPSHOT_CACHE_REVISION_BUMP");
        requireContains("src/core/cache/OrderSnapshotCache.cpp", "orders|current|rev=%1|db=%2|filter=%3");
        requireContains("src/core/cache/OrderSnapshotCache.cpp", "orders|history|rev=%1|db=%2|from=%3|to=%4|filter=%5");
    }

    void runtimeJobTelemetryCarriesCacheKey()
    {
        using namespace cs::tests;
        requireContains("src/runtime/RuntimeJobDispatcher.h", "QString cacheKey;");
        requireContains("src/runtime/RuntimeJobDispatcher.cpp", "ticket.cacheKey = cacheKey.trimmed().left(512)");
        requireContains("src/runtime/RuntimeJobDispatcher.cpp", "cacheKey=");
        requireContains("src/ui/tabs/orders/CurrentOrdersPageActions.cpp", "current_orders|filter=%1");
        requireContains("src/ui/tabs/history/HistoryPage.cpp", "history|from=%1|to=%2|filter=%3");
    }
};

QTEST_MAIN(RegressionPerformanceTzPass6OrderSnapshotCacheTests)
#include "RegressionPerformanceTzPass6OrderSnapshotCacheTests.moc"
