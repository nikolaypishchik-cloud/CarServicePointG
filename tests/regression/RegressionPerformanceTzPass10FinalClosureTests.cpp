#include "TestSourceAssertions.h"

class RegressionPerformanceTzPass10FinalClosureTests final : public QObject
{
    Q_OBJECT
private slots:
    void receivedScansPreviewUsesSnapshotModelNotTableWidget()
    {
        using namespace cs::tests;
        requireContains("src/ui/common/dialogs/ReceivedScansDialog.cpp", "ReceivedScansTableModel final : public QAbstractTableModel");
        requireContains("src/ui/common/dialogs/ReceivedScansDialog.cpp", "m_table = new QTableView(card)");
        requireContains("src/ui/common/dialogs/ReceivedScansDialog.cpp", "model->setRows(std::move(rows))");
        requireContains("src/ui/common/dialogs/ReceivedScansDialog.cpp", "cellText(rowIndex, kColOrder)");
        requireNotContains("src/ui/common/dialogs/ReceivedScansDialog.cpp", "QTableWidget");
        requireNotContains("src/ui/common/dialogs/ReceivedScansDialog.cpp", "QTableWidgetItem");
        requireNotContains("src/ui/common/dialogs/ReceivedScansDialog.cpp", "setRowCount(");
        requireNotContains("src/ui/common/dialogs/ReceivedScansDialog.cpp", "setItem(");
        requireNotContains("src/ui/common/dialogs/ReceivedScansDialog.cpp", "m_table->item(");
    }

    void scanImportPricingLogIsDeepDebugOnly()
    {
        using namespace cs::tests;
        requireContains("src/ui/common/dialogs/ReceivedScansDialog.cpp", "RuntimeLoggingControl::deepDebugEnabled()");
        requireContains("src/ui/common/dialogs/ReceivedScansDialog.cpp", "PART_PRICING_SCAN_IMPORT");
    }

    void mutationInvalidationAvoidsUnnecessaryPartsCacheClear()
    {
        using namespace cs::tests;
        requireContains("src/core/cache/SnapshotCacheInvalidator.cpp", "touchesOrderHeaderOrServices");
        requireContains("src/core/cache/SnapshotCacheInvalidator.cpp", "touchesPartsArchive");
        requireContains("src/core/cache/SnapshotCacheInvalidator.cpp", "Service/header/payment edits do not clear the parts archive cache");
        requireContains("src/core/cache/SnapshotCacheInvalidator.cpp", "families=current_orders,history,orders reason=");
        requireContains("src/core/cache/SnapshotCacheInvalidator.cpp", "families=current_orders,history,orders,parts reason=");
    }

    void finalGateCoversPass10Closure()
    {
        using namespace cs::tests;
        requireContains("scripts/check_performance_tz_100_completion.py", "Pass10 closure gates");
        requireContains("scripts/check_performance_tz_100_completion.py", "RegressionPerformanceTzPass10FinalClosureTests");
        requireContains("cmake/modules/CarServiceTests.cmake", "RegressionPerformanceTzPass10FinalClosureTests");
    }
};

QTEST_MAIN(RegressionPerformanceTzPass10FinalClosureTests)
#include "RegressionPerformanceTzPass10FinalClosureTests.moc"
