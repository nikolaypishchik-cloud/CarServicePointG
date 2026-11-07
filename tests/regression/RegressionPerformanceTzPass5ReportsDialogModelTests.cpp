#include "TestSourceAssertions.h"

class RegressionPerformanceTzPass5ReportsDialogModelTests final : public QObject
{
    Q_OBJECT
private slots:
    void payrollOrdersDialogUsesSnapshotModelNotTableWidget()
    {
        using namespace cs::tests;
        requireContains("src/ui/tabs/reports/ReportsDialogSupport.cpp", "auto* table = new QTableView(&dlg)");
        requireContains("src/ui/tabs/reports/ReportsDialogSupport.cpp", "ReportsSnapshotTableModel(table)");
        requireContains("src/ui/tabs/reports/ReportsDialogSupport.cpp", "model->setSnapshot(payrollOrdersHeaders(), buildPayrollOrdersRows(orders))");
        requireContains("src/ui/tabs/reports/ReportsDialogSupport.cpp", "QTableView::doubleClicked");
        requireContains("src/ui/tabs/reports/ReportsDialogSupport.cpp", "model->cellUserValue(orderRow, 2)");
        requireContains("src/ui/tabs/reports/ReportsDialogSupport.cpp", "row.spanAllColumns = true");
    }

    void payrollOrdersDialogDoesNotAllocateWidgetItemsInLoop()
    {
        using namespace cs::tests;
        requireNotContains("src/ui/tabs/reports/ReportsDialogSupport.cpp", "QTableWidget");
        requireNotContains("src/ui/tabs/reports/ReportsDialogSupport.cpp", "QTableWidgetItem");
        requireNotContains("src/ui/tabs/reports/ReportsDialogSupport.cpp", "setRowCount(");
        requireNotContains("src/ui/tabs/reports/ReportsDialogSupport.cpp", "setItem(");
        requireNotContains("src/ui/tabs/reports/ReportsDialogSupport.cpp", "cellDoubleClicked");
        requireNotContains("src/ui/tabs/reports/ReportsDialogSupport.cpp", "setHorizontalHeaderLabels");
    }
};

QTEST_MAIN(RegressionPerformanceTzPass5ReportsDialogModelTests)
#include "RegressionPerformanceTzPass5ReportsDialogModelTests.moc"
