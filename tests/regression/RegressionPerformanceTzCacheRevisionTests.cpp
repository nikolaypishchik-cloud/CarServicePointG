#include "TestSourceAssertions.h"

class RegressionPerformanceTzCacheRevisionTests final : public QObject
{
    Q_OBJECT
private slots:
    void partsCacheKeysIncludeRevisionAndFilters()
    {
        using namespace cs::tests;
        requireContains("src/core/cache/PartsQueryCache.h", "quint64 currentRevision();");
        requireContains("src/core/cache/PartsQueryCache.cpp", "std::atomic<quint64> g_cacheRevision");
        requireContains("src/core/cache/PartsQueryCache.cpp", "parts|rows|rev=%1|db=%2|filter=%3|from=%4|to=%5");
        requireContains("src/core/cache/PartsQueryCache.cpp", "parts|date-bounds|rev=%1|db=%2");
        requireContains("src/core/cache/PartsQueryCache.cpp", "bumpRevision(reason.trimmed().isEmpty() ? QStringLiteral(\"parts\") : reason)");
    }

    void reportsAndStatsCacheKeysIncludeRevisionAndScope()
    {
        using namespace cs::tests;
        requireContains("src/core/cache/ReportsSnapshotCache.h", "quint64 currentRevision();");
        requireContains("src/core/cache/ReportsSnapshotCache.cpp", "std::atomic<quint64> g_cacheRevision");
        requireContains("src/core/cache/ReportsSnapshotCache.cpp", "reports|rev=%1|family=%2|db=%3|from=%4|to=%5|scope=%6");
        requireContains("src/core/cache/ReportsSnapshotCache.cpp", "stats|rev=%1|db=%2|from=%3|to=%4");
        requireContains("src/core/cache/ReportsSnapshotCache.cpp", "bumpRevision(QStringLiteral(\"reports\"))");
        requireContains("src/core/cache/ReportsSnapshotCache.cpp", "bumpRevision(QStringLiteral(\"stats\"))");
    }

    void mutationInvalidatorStillRoutesThroughCentralPoint()
    {
        using namespace cs::tests;
        requireContains("src/core/cache/SnapshotCacheInvalidator.cpp", "PartsQueryCache::invalidate(reason)");
        requireContains("src/core/cache/SnapshotCacheInvalidator.cpp", "ReportsSnapshotCache::invalidateReports()");
        requireContains("src/core/cache/SnapshotCacheInvalidator.cpp", "ReportsSnapshotCache::invalidateStats()");
        requireContains("src/core/services/MutationPostCommitService.cpp", "SnapshotCacheInvalidator::invalidateOrdersAndParts");
    }
};

QTEST_MAIN(RegressionPerformanceTzCacheRevisionTests)
#include "RegressionPerformanceTzCacheRevisionTests.moc"
