#include "TestSourceAssertions.h"

class RegressionPerformanceTzPass7FreezeLogDbClosureTests final : public QObject
{
    Q_OBJECT
private slots:
    void workPartsSyncFallbacksRouteToWorkerExceptDraftRestore()
    {
        using namespace cs::tests;
        requireContains("src/ui/tabs/orders/ServiceTableController.cpp", "sync_fallback_denied");
        requireContains("src/ui/tabs/orders/ServiceTableController.cpp", "scheduleWorkPartsRefresh(true, false, false, true");
        requireContains("src/ui/tabs/orders/PartTableController.cpp", "sync_fallback_denied");
        requireContains("src/ui/tabs/orders/PartTableController.cpp", "scheduleWorkPartsRefresh(false, true, false, false");
        requireContains("src/ui/tabs/orders/WorkPartsEditorController.cpp", "csAllowSyncWorkPartsReload");
        requireContains("src/ui/tabs/orders/WorkPartsDialog.cpp", "AsyncThreadScope::startWorker(this,");
        requireContains("src/ui/tabs/orders/WorkPartsDialog.cpp", "Qt::QueuedConnection");
    }

    void productionInfoSpamRequiresDeepDebug()
    {
        using namespace cs::tests;
        requireContains("src/ui/tabs/orders/WorkPartsDiagnosticsSupport.cpp", "shouldLogWorkPartsDiagnosticInProduction");
        requireContains("src/ui/tabs/orders/WorkPartsDiagnosticsSupport.cpp", "CS_DEEP_DEBUG=1");
        requireContains("src/ui/tabs/orders/WorkPartsDiagnosticsSupport.cpp", "return false;");
        requireContains("src/core/services/orders/CurrentOrdersInvoiceImportService.cpp", "RuntimeLoggingControl::deepDebugEnabled()) qInfo");
        requireContains("src/core/services/orders/PartPricingEngine.cpp", "RuntimeLoggingControl::deepDebugEnabled()) qInfo");
        requireContains("src/infrastructure/db/sqlite/DbContext.cpp", "RuntimeLoggingControl::deepDebugEnabled()");
        requireContains("src/app/ClickSoundFilter.cpp", "deepClickDiagnosticsEnabled()");
    }

    void dbCloseUnregistersThreadConnectionAfterSafeRemove()
    {
        using namespace cs::tests;
        requireContains("src/infrastructure/db/sqlite/DbRuntimePreflight.cpp", "SqliteConnectionUtils::safeCloseAndRemoveDatabase(connName)");
        requireContains("src/infrastructure/db/sqlite/DbRuntimePreflight.cpp", "DbThreadConnectionSupport::unregisterConnectionName(connName)");
        requireContains("src/infrastructure/db/sqlite/ScopedConnectionManager.cpp", "g_scopedConnectionSequence");
        requireContains("src/infrastructure/db/sqlite/ScopedConnectionManager.cpp", "db = QSqlDatabase()");
        requireContains("src/infrastructure/db/sqlite/ScopedConnectionManager.cpp", "QSqlDatabase::removeDatabase(nameCopy)");
    }
};

QTEST_MAIN(RegressionPerformanceTzPass7FreezeLogDbClosureTests)
#include "RegressionPerformanceTzPass7FreezeLogDbClosureTests.moc"
