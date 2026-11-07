#include "TestSourceAssertions.h"

class RegressionWorkPartsPerformanceContinuationTests final : public QObject
{
    Q_OBJECT
private slots:
    void workPartsRefreshesAreCoalescedThroughRuntimeJobFamily()
    {
        using namespace cs::tests;
        requireContains("src/runtime/RuntimeJobDispatcher.h", "WorkPartsDialog,");
        requireContains("src/runtime/RuntimeJobDispatcher.cpp", "workparts_dialog");
        requireContains("src/ui/tabs/orders/WorkPartsDialog.h", "scheduleWorkPartsRefresh");
        requireContains("src/ui/tabs/orders/WorkPartsDialog.cpp", "RuntimeJob::Family::WorkPartsDialog");
        requireContains("src/ui/tabs/orders/WorkPartsPersistenceCoordinator.cpp", "scheduleWorkPartsRefresh(reloadServices");
        requireNotContains("src/ui/tabs/orders/WorkPartsPersistenceCoordinator.cpp", "        loadServices();\n");
        requireNotContains("src/ui/tabs/orders/WorkPartsPersistenceCoordinator.cpp", "        loadPartsForActiveService();\n");
    }

    void workPartsTableRebuildsAvoidAttachmentNPlusOneQueries()
    {
        using namespace cs::tests;
        requireContains("src/core/services/orders/OrderAttachmentService.h", "struct AttachmentInfo");
        requireContains("src/core/services/orders/OrderAttachmentService.cpp", "serviceAttachmentMap(QSqlDatabase db, const QList<int>& serviceIds)");
        requireContains("src/core/services/orders/OrderAttachmentService.cpp", "partAttachmentMap(QSqlDatabase db, const QList<int>& partIds)");
        requireContains("src/ui/tabs/orders/ServiceTableController.cpp", "attachmentsByServiceId");
        requireContains("src/ui/tabs/orders/PartTableController.cpp", "attachmentsByPartId");
        requireNotContains("src/ui/tabs/orders/ServiceTableController.cpp", "refreshServiceAttachmentCell(row);");
        requireNotContains("src/ui/tabs/orders/PartTableController.cpp", "refreshPartAttachmentCell(row);");
        requireNotContains("src/ui/tabs/orders/PartTableController.cpp", "refreshPartInvoiceNumberCell(row);");
    }
};

QTEST_MAIN(RegressionWorkPartsPerformanceContinuationTests)
#include "RegressionWorkPartsPerformanceContinuationTests.moc"
