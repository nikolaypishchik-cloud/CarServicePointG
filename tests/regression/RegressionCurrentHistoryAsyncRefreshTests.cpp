#include <QtTest/QtTest>
#include "../TestSourceAssertions.h"

class RegressionCurrentHistoryAsyncRefreshTests final : public QObject {
    Q_OBJECT
private slots:
    void currentOrdersRefreshRunsThroughWorkerWithCancellation();
    void historyRefreshRunsThroughWorkerWithDebouncedDateChanges();
    void runtimeDispatcherAndUiBlockingGuardsAreWired();
    void largeCurrentAndHistoryTablesUseSnapshotTableModel();
    void partsDebtBannerUsesPreparedMapAndDebounce();
    void centralizedCacheInvalidationIsWired();
};

void RegressionCurrentHistoryAsyncRefreshTests::currentOrdersRefreshRunsThroughWorkerWithCancellation()
{
    const QString source = cs::tests::readSourceFile("src/ui/tabs/orders/CurrentOrdersPageActions.cpp");
    const QString header = cs::tests::readSourceFile("src/ui/tabs/orders/CurrentOrdersPage.h");

    QVERIFY2(source.contains(QStringLiteral("AsyncThreadScope::startWorker")),
             "CurrentOrdersPage::refresh must run heavy loadOpenOrders work through the async worker helper.");
    QVERIFY2(source.contains(QStringLiteral("CurrentOrdersRefreshJob::run(filter, cancelToken)")),
             "CurrentOrders worker must delegate heavy query/view-model creation to the core refresh job.");
    QVERIFY2(source.contains(QStringLiteral("asyncHandle.shouldApply"))
                 && source.contains(QStringLiteral("m_refreshGeneration"))
                 && source.contains(QStringLiteral("m_refreshQueued = true")),
             "CurrentOrders async refresh must ignore stale results and queue the latest refresh only.");
    QVERIFY2(header.contains(QStringLiteral("std::shared_ptr<std::atomic_bool> m_refreshCancelToken"))
                 && header.contains(QStringLiteral("QPointer<QThread> m_refreshWorker"))
                 && header.contains(QStringLiteral("closeEvent(QCloseEvent* event) override")),
             "CurrentOrdersPage must own cancel token/worker and cancel safely on close/destruction.");
}

void RegressionCurrentHistoryAsyncRefreshTests::historyRefreshRunsThroughWorkerWithDebouncedDateChanges()
{
    const QString source = cs::tests::readSourceFile("src/ui/tabs/history/HistoryPage.cpp");
    const QString header = cs::tests::readSourceFile("src/ui/tabs/history/HistoryPage.h");

    QVERIFY2(source.contains(QStringLiteral("AsyncThreadScope::startWorker")),
             "HistoryPage::refresh must run loadHistory in a worker, not in the UI thread.");
    QVERIFY2(source.contains(QStringLiteral("HistoryRefreshJob::run(params, cancelToken)")),
             "History worker must delegate heavy bounded query/view-model creation to the core refresh job.");
    QVERIFY2(source.contains(QStringLiteral("m_searchDebounce->setInterval(200)"))
                 && source.contains(QStringLiteral("m_searchDebounce->start(200)")),
             "History date/search changes must be debounced in the 150-250 ms acceptance window.");
    QVERIFY2(header.contains(QStringLiteral("std::shared_ptr<std::atomic_bool> m_refreshCancelToken"))
                 && header.contains(QStringLiteral("QPointer<QThread> m_refreshWorker"))
                 && header.contains(QStringLiteral("closeEvent(QCloseEvent* event) override")),
             "HistoryPage must own cancel token/worker and cancel safely on close/destruction.");
}

void RegressionCurrentHistoryAsyncRefreshTests::runtimeDispatcherAndUiBlockingGuardsAreWired()
{
    const QString dispatcherH = cs::tests::readSourceFile("src/runtime/RuntimeJobDispatcher.h");
    const QString currentService = cs::tests::readSourceFile("src/core/services/orders/CurrentOrdersQueryService.cpp");
    const QString historyService = cs::tests::readSourceFile("src/core/services/HistoryQueryService.cpp");
    const QString currentJob = cs::tests::readSourceFile("src/core/services/orders/CurrentOrdersRefreshJob.cpp");
    const QString historyJob = cs::tests::readSourceFile("src/core/services/HistoryRefreshJob.cpp");
    const QString cmake = cs::tests::readSourceFile("cmake/modules/CarServiceSourcesCore.cmake");

    QVERIFY2(dispatcherH.contains(QStringLiteral("enum class Family"))
                 && dispatcherH.contains(QStringLiteral("CurrentOrders"))
                 && dispatcherH.contains(QStringLiteral("History"))
                 && dispatcherH.contains(QStringLiteral("cancelFamily"))
                 && dispatcherH.contains(QStringLiteral("warnIfUiThreadBlockingCall")),
             "RuntimeJobDispatcher must expose family/generation/cancel/ui-blocking guard primitives.");
    QVERIFY2(currentService.contains(QStringLiteral("warnIfUiThreadBlockingCall(QStringLiteral(\"CurrentOrdersQueryService::loadOpenOrders\"))")),
             "CurrentOrdersQueryService must warn if someone calls the heavy SQL read from the UI thread.");
    QVERIFY2(historyService.contains(QStringLiteral("warnIfUiThreadBlockingCall(QStringLiteral(\"HistoryQueryService::loadHistory\"))")),
             "HistoryQueryService must warn if someone calls the heavy SQL read from the UI thread.");
    QVERIFY2(currentJob.contains(QStringLiteral("CurrentOrdersQueryService::loadOpenOrders(filter)"))
                 && currentJob.contains(QStringLiteral("CurrentOrdersViewModelBuilder::build")),
             "CurrentOrdersRefreshJob must own the heavy SQL read and view-model build outside the UI source file.");
    QVERIFY2(historyJob.contains(QStringLiteral("HistoryQueryService::loadHistory(params)"))
                 && historyJob.contains(QStringLiteral("HistoryViewModelBuilder::build")),
             "HistoryRefreshJob must own the heavy SQL read and view-model build outside the UI source file.");
    QVERIFY2(cmake.contains(QStringLiteral("src/runtime/RuntimeJobDispatcher.cpp"))
                 && cmake.contains(QStringLiteral("src/runtime/RuntimeJobDispatcher.h"))
                 && cmake.contains(QStringLiteral("src/core/services/orders/CurrentOrdersRefreshJob.cpp"))
                 && cmake.contains(QStringLiteral("src/core/services/HistoryRefreshJob.cpp")),
             "RuntimeJobDispatcher and core refresh jobs must be wired into the core build sources.");
}

void RegressionCurrentHistoryAsyncRefreshTests::largeCurrentAndHistoryTablesUseSnapshotTableModel()
{
    const QString modelH = cs::tests::readSourceFile("src/ui/common/TabularSnapshotTableModel.h");
    const QString currentH = cs::tests::readSourceFile("src/ui/tabs/orders/CurrentOrdersPage.h");
    const QString currentBuild = cs::tests::readSourceFile("src/ui/tabs/orders/CurrentOrdersPageBuildUi.cpp");
    const QString historyH = cs::tests::readSourceFile("src/ui/tabs/history/HistoryPage.h");
    const QString historyCpp = cs::tests::readSourceFile("src/ui/tabs/history/HistoryPage.cpp");
    const QString mapper = cs::tests::readSourceFile("src/ui/common/TabularReadModelMapper.cpp");
    const QString cmake = cs::tests::readSourceFile("cmake/modules/CarServiceSourcesUi.cmake");

    QVERIFY2(modelH.contains(QStringLiteral("QAbstractTableModel"))
                 && modelH.contains(QStringLiteral("setSnapshot"))
                 && modelH.contains(QStringLiteral("QVector<QVector<QVariant>>")) == false,
             "TabularSnapshotTableModel must be a lightweight QAbstractTableModel wrapper over TabularReadModel snapshots.");
    QVERIFY2(currentH.contains(QStringLiteral("TabularSnapshotTableModel* m_model"))
                 && currentBuild.contains(QStringLiteral("new TabularSnapshotTableModel(this)")),
             "CurrentOrdersPage must no longer allocate QStandardItemModel for the large table.");
    QVERIFY2(historyH.contains(QStringLiteral("TabularSnapshotTableModel* m_model"))
                 && historyCpp.contains(QStringLiteral("new TabularSnapshotTableModel(this)")),
             "HistoryPage must no longer allocate QStandardItemModel for the large table.");
    QVERIFY2(mapper.contains(QStringLiteral("namespace TabularReadModelMapper"))
                 && mapper.contains(QStringLiteral("void apply(TabularSnapshotTableModel* model"))
                 && mapper.contains(QStringLiteral("model->setSnapshot(readModel)")),
             "TabularReadModelMapper must apply snapshots through beginResetModel/endResetModel, not item loops, for migrated tables.");
    QVERIFY2(cmake.contains(QStringLiteral("src/ui/common/TabularSnapshotTableModel.cpp"))
                 && cmake.contains(QStringLiteral("src/ui/common/TabularSnapshotTableModel.h")),
             "TabularSnapshotTableModel must be wired into the UI CMake module.");
    QVERIFY2(historyCpp.contains(QStringLiteral("ui.history.header_schema.v2"))
                 && historyCpp.contains(QStringLiteral("ui.history.header_state.v2"))
                 && historyCpp.contains(QStringLiteral("ui.history.adaptive.v2"))
                 && historyCpp.contains(QStringLiteral("enforceHistoryVisibleColumns"))
                 && historyCpp.contains(QStringLiteral("setSectionResizeMode(col, QHeaderView::Interactive)")),
             "History table must invalidate stale header state after schema changes and keep manual column width editing enabled.");
}

void RegressionCurrentHistoryAsyncRefreshTests::partsDebtBannerUsesPreparedMapAndDebounce()
{
    const QString header = cs::tests::readSourceFile("src/ui/tabs/parts/PartsPage.h");
    const QString runtime = cs::tests::readSourceFile("src/ui/tabs/parts/PartsPageRuntime.cpp");

    QVERIFY2(header.contains(QStringLiteral("QHash<int, PartRowSnapshot> m_rowsSnapshotById"))
                 && runtime.contains(QStringLiteral("m_rowsSnapshotById.insert(rowData.id, rowData)")),
             "PartsPage must prepare an id->snapshot map when applying the parts snapshot.");
    const QString service = cs::tests::readSourceFile("src/core/services/SupplierDebtReadService.cpp");
    const int bannerStart = service.indexOf("BannerTask prepareBanner()");
    QVERIFY(bannerStart >= 0);
    const QString banner = service.mid(bannerStart, service.indexOf("ReconciliationInput reconciliationInput()") - bannerStart);
    const auto wired = [](const QString& ui, const QString& read) {
        return ui.contains("SupplierDebtReadService::prepareBanner()")
            && ui.contains("AsyncThreadScope::startWorker(this,\"parts_debt_banner\"")
            && ui.contains("readTask(*cancel,&result->error)")
            && read.count("PurchasePaymentService::debts(db,&error)") == 1
            && read.contains("SupplierInvoiceLedger::summary(db,debts,facts,&error).totals")
            && read.contains("if(!error.isEmpty() || cancel.load())totals.clear()");
    };
    QVERIFY(wired(runtime, banner));
    QString disconnected = runtime;
    disconnected.replace("readTask(*cancel,&result->error)", "emptyResult()");
    QVERIFY(!wired(disconnected, banner));
    QVERIFY(!wired(runtime, banner + "PurchasePaymentService::debts(db,&error)"));
    QVERIFY2(!runtime.contains(QStringLiteral("Database::db()"))
                 && !runtime.contains(QStringLiteral("for (const PartRowSnapshot& rowData : m_rowsSnapshot) {\n            if (rowData.id == partId)")),
             "Debt banner must be O(n), not nested visible-row x snapshot scanning.");
    QVERIFY2(header.contains(QStringLiteral("m_debtBannerDebounce"))
                 && header.contains(QStringLiteral("scheduleDebtBannerUpdate"))
                 && runtime.contains(QStringLiteral("m_debtBannerDebounce->start(delayMs)")),
             "Debt banner updates must be debounced so table rebuilds do not recalculate the banner repeatedly.");
}

void RegressionCurrentHistoryAsyncRefreshTests::centralizedCacheInvalidationIsWired()
{
    const QString invalidatorH = cs::tests::readSourceFile("src/core/cache/SnapshotCacheInvalidator.h");
    const QString invalidatorCpp = cs::tests::readSourceFile("src/core/cache/SnapshotCacheInvalidator.cpp");
    const QString mutation = cs::tests::readSourceFile("src/core/services/MutationPostCommitService.cpp");
    const QString cmake = cs::tests::readSourceFile("cmake/modules/CarServiceSourcesCore.cmake");

    QVERIFY2(invalidatorH.contains(QStringLiteral("invalidateAfterMutation"))
                 && invalidatorH.contains(QStringLiteral("invalidateOrdersAndParts"))
                 && invalidatorH.contains(QStringLiteral("invalidateReportsAndStats")),
             "Centralized cache invalidator must expose targeted invalidation operations.");
    QVERIFY2(invalidatorCpp.contains(QStringLiteral("PartsQueryCache::invalidate(reason)"))
                 && invalidatorCpp.contains(QStringLiteral("ReportsSnapshotCache::invalidateReports()"))
                 && invalidatorCpp.contains(QStringLiteral("ReportsSnapshotCache::invalidateStats()")),
             "Centralized invalidation must clear Parts/Reports/Stats caches consistently.");
    QVERIFY2(mutation.contains(QStringLiteral("SnapshotCacheInvalidator::invalidateOrdersAndParts")),
             "Post-commit mutation flow must use the centralized invalidator instead of ad-hoc cache clears.");
    QVERIFY2(cmake.contains(QStringLiteral("src/core/cache/SnapshotCacheInvalidator.cpp"))
                 && cmake.contains(QStringLiteral("src/core/cache/SnapshotCacheInvalidator.h")),
             "SnapshotCacheInvalidator must be wired into the core CMake module.");
}

QTEST_MAIN(RegressionCurrentHistoryAsyncRefreshTests)
#include "RegressionCurrentHistoryAsyncRefreshTests.moc"
