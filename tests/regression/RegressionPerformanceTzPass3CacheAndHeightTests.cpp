#include "TestSourceAssertions.h"

class RegressionPerformanceTzPass3CacheAndHeightTests final : public QObject
{
    Q_OBJECT
private slots:
    void pairedRowHeightWorkIsCachedAndViewportBounded()
    {
        using namespace cs::tests;
        requireContains("src/ui/tabs/orders/WorkPartsDialog.h", "m_pairedRowHeightSignatureByRow");
        requireContains("src/ui/tabs/orders/WorkPartsDialogUiBuilder.cpp", "workPartsPairedHeightSignature");
        requireContains("src/ui/tabs/orders/WorkPartsDialogUiBuilder.cpp", "m_pairedRowHeightByRow");
        requireContains("src/ui/tabs/orders/internal/WorkPartsEditorDelegateState.ipp", "limit the expensive font/height work to");
        requireNotContains("src/ui/tabs/orders/internal/WorkPartsEditorDelegateState.ipp", "for (int row = 0; row < table->rowCount(); ++row) {\n        updateSingleMainPartsRowHeight(table, row);");
    }

    void snapshotModelProvidesIndexedSelectionRestore()
    {
        using namespace cs::tests;
        requireContains("src/ui/common/TabularSnapshotTableModel.h", "rowByColumnValue");
        requireContains("src/ui/common/TabularSnapshotTableModel.cpp", "m_firstRowByColumnValue");
        requireContains("src/ui/tabs/orders/CurrentOrdersPageActions.cpp", "rowByColumnValue(QStringLiteral(\"__internal_number\")");
        requireContains("src/ui/tabs/history/HistoryPage.cpp", "rowByColumnValue(QStringLiteral(\"__internal_number\")");
    }

    void viewportFillRowsGrowModelOnce()
    {
        using namespace cs::tests;
        requireContains("src/ui/tabs/orders/WorkPartsEditorController.cpp", "setRowCount(currentRows + rowsToAdd)");
        requireContains("src/ui/tabs/orders/internal/WorkPartsEditorNavigation.ipp", "setRowCount(currentRows + rowsToAdd)");
        requireContains("src/ui/tabs/orders/WorkPartsEditorController.cpp", "grow the backing model once instead of insertRow()");
        requireContains("src/ui/tabs/orders/internal/WorkPartsEditorNavigation.ipp", "resize once instead of calling insertRow()");
    }
};

QTEST_MAIN(RegressionPerformanceTzPass3CacheAndHeightTests)
#include "RegressionPerformanceTzPass3CacheAndHeightTests.moc"
