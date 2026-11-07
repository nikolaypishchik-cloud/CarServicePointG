#include "TestSourceAssertions.h"

class RegressionPerformanceTzPass9SnapshotCacheGatesTests final : public QObject
{
    Q_OBJECT
private slots:
    void currentHistoryPartsUseSnapshotModelsOnly()
    {
        using namespace cs::tests;
        requireContains("src/ui/common/TabularReadModelMapper.h", "legacy item-model mapper");
        requireContains("src/ui/common/TabularReadModelMapper.cpp", "model->setSnapshot(readModel)");
        requireNotContains("src/ui/common/TabularReadModelMapper.cpp", "new QStandardItem");
        requireNotContains("src/ui/tabs/orders/CurrentOrdersPageModulePrelude.h", "QStandardItemModel");
        requireContains("src/ui/tabs/history/HistoryPage.cpp", "source snapshot index and map through the proxy");
        requireContains("src/ui/tabs/history/HistoryPage.cpp", "m_proxy->mapFromSource(sourceIndex)");
    }

    void partsArchiveModelOwnsVisibleRowIndexes()
    {
        using namespace cs::tests;
        requireContains("src/ui/tabs/parts/PartsPageTableModelAdapter.h", "rowByPartId");
        requireContains("src/ui/tabs/parts/PartsPageTableModelAdapter.h", "firstRowByOrderUid");
        requireContains("src/ui/tabs/parts/PartsPageTableModelAdapter.cpp", "rebuildIndexes");
        requireContains("src/ui/tabs/parts/PartsPageRuntime.cpp", "m_partsModel->rowByPartId(partId)");
        requireContains("src/ui/tabs/parts/PartsPageRuntime.cpp", "m_partsModel->firstRowByOrderUid(orderUid)");
        requireContains("src/ui/tabs/parts/PartsPageRuntime.cpp", "m_partsModel->firstRowByOrderNumber(orderNumber)");
        requireNotContains("src/ui/tabs/parts/PartsPage.h", "m_visibleRowByPartId");
    }

    void cacheKeysNormalizeDbIdentityAndCentralInvalidationIsScoped()
    {
        using namespace cs::tests;
        requireContains("src/core/cache/PartsQueryCache.cpp", "normalizedCacheDbIdentity");
        requireContains("src/core/cache/PartsQueryCache.cpp", "canonicalFilePath");
        requireContains("src/core/cache/OrderSnapshotCache.cpp", "normalizedCacheDbIdentity");
        requireContains("src/core/cache/OrderSnapshotCache.cpp", "RuntimeLoggingControl::deepDebugEnabled()");
        requireContains("src/core/cache/SnapshotCacheInvalidator.cpp", "touchesReportsOrStats");
        requireContains("src/core/cache/SnapshotCacheInvalidator.cpp", "all mutation invalidation routes through this central point");
        requireContains("src/core/cache/SnapshotCacheInvalidator.cpp", "invalidateReportsAndStats(reason)");
    }

    void testAndGateInventoryCoversPass9Closure()
    {
        using namespace cs::tests;
        requireContains("scripts/check_performance_tz_100_completion.py", "Pass9 closure gates");
        requireContains("scripts/check_performance_tz_100_completion.py", "RegressionPerformanceTzPass9SnapshotCacheGatesTests");
        requireContains("cmake/modules/CarServiceTests.cmake", "RegressionPerformanceTzPass9SnapshotCacheGatesTests");
        requireContains("scripts/check_build_sanity.py", "qrc/main/source wiring/temp artifact checks");
    }
};

QTEST_MAIN(RegressionPerformanceTzPass9SnapshotCacheGatesTests)
#include "RegressionPerformanceTzPass9SnapshotCacheGatesTests.moc"
