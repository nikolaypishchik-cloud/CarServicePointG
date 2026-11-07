#include "TestSourceAssertions.h"

class RegressionPerformanceTzPass4WorkPartsAsyncReadTests final : public QObject
{
    Q_OBJECT
private slots:
    void workPartsRefreshReadsDataInWorkerAndAppliesSnapshotOnUiThread()
    {
        using namespace cs::tests;
        requireContains("src/ui/tabs/orders/WorkPartsDialog.cpp", "AsyncThreadScope::startWorker(this,");
        requireContains("src/ui/tabs/orders/WorkPartsDialog.cpp", "WorkPartsReadQueryService::listServicesByInternalNumber(orderNumber");
        requireContains("src/ui/tabs/orders/WorkPartsDialog.cpp", "WorkPartsReadQueryService::listPartsByInternalNumber(orderNumber");
        requireContains("src/ui/tabs/orders/WorkPartsDialog.cpp", "QMetaObject::invokeMethod(self.data()");
        requireContains("src/ui/tabs/orders/WorkPartsDialog.cpp", "self->applyServicesSnapshot(payload.serviceRows");
        requireContains("src/ui/tabs/orders/WorkPartsDialog.cpp", "self->applyPartsSnapshot(payload.partRows");
        requireContains("src/ui/tabs/orders/WorkPartsDialog.cpp", "workparts_dialog_refresh_timeout_cancel");
        requireNotContains("src/ui/tabs/orders/WorkPartsDialog.cpp", "self->loadServices();\n            if (ensureServiceSelection)");
        requireNotContains("src/ui/tabs/orders/WorkPartsDialog.cpp", "self->loadPartsForActiveService();");
    }

    void legacySyncFallbacksAreExplicitlyGuarded()
    {
        using namespace cs::tests;
        requireContains("src/ui/tabs/orders/ServiceTableController.cpp", "WorkPartsDialog::loadServices.sync_fallback");
        requireContains("src/ui/tabs/orders/PartTableController.cpp", "WorkPartsDialog::loadPartsForActiveService.sync_fallback");
        requireContains("src/ui/tabs/orders/ServiceTableController.cpp", "applyServicesSnapshot");
        requireContains("src/ui/tabs/orders/PartTableController.cpp", "applyPartsSnapshot");
    }

    void scopedSqliteConnectionsCannotCollideWithinSameMillisecond()
    {
        using namespace cs::tests;
        requireContains("src/infrastructure/db/sqlite/ScopedConnectionManager.cpp", "g_scopedConnectionSequence");
        requireContains("src/infrastructure/db/sqlite/ScopedConnectionManager.cpp", "QThread::currentThread()");
        requireContains("src/infrastructure/db/sqlite/ScopedConnectionManager.cpp", "scoped_%1_thread_%2_seq_%3_ts_%4");
    }
};

QTEST_MAIN(RegressionPerformanceTzPass4WorkPartsAsyncReadTests)
#include "RegressionPerformanceTzPass4WorkPartsAsyncReadTests.moc"
