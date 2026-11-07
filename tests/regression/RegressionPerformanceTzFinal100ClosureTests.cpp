#include "TestSourceAssertions.h"

class RegressionPerformanceTzFinal100ClosureTests final : public QObject
{
    Q_OBJECT
private slots:
    void partsPageSelectionUsesSnapshotNotWidgetItems()
    {
        using namespace cs::tests;
        requireContains("src/ui/tabs/parts/PartsPageRuntime.cpp", "m_rowsSnapshot.at(row).orderNumber");
        requireContains("src/ui/tabs/parts/PartsPageRuntime.cpp", "m_rowsSnapshot.at(row).orderUid.trimmed()");
        requireContains("src/ui/tabs/parts/PartsPageRuntime.cpp", "m_rowsSnapshot.at(row).id");
        requireContains("src/ui/tabs/parts/PartsPageBuildUi.cpp", "const PartRowSnapshot& snapshot = m_rowsSnapshot.at(row)");
        requireNotContains("src/ui/tabs/parts/PartsPageRuntime.cpp", "m_table->item(row, ColOrder) ? m_table->item(row, ColOrder)");
        requireNotContains("src/ui/tabs/parts/PartsPageRuntime.cpp", "m_table->item(idx.row(), ColId)");
    }

    void partsPageVisibleHeightUsesSnapshotText()
    {
        using namespace cs::tests;
        requireContains("src/ui/tabs/parts/PartsPageRuntime.cpp", "const QString nameText = (r >= 0 && r < m_rowsSnapshot.size()) ? m_rowsSnapshot.at(r).name : QString()");
        requireNotContains("src/ui/tabs/parts/PartsPageRuntime.cpp", "rowHeightForText(fm, item->text(), nameWidth, minRowHeight)");
    }

    void reportsTablesUseQTableViewSnapshotModels()
    {
        using namespace cs::tests;
        requireContains("src/ui/tabs/reports/ReportsTableSupport.h", "class ReportsSnapshotTableModel final : public QAbstractTableModel");
        requireContains("src/ui/tabs/reports/ReportsTableSupport.cpp", "beginResetModel()");
        requireContains("src/ui/tabs/reports/ReportsTableSupport.cpp", "endResetModel()");
        requireContains("src/ui/tabs/reports/ReportsPageBuildUi.cpp", "table = new QTableView(page)");
        requireContains("src/ui/tabs/reports/ReportsPageBuildUi.cpp", "table->setModel(model)");
        requireNotContains("src/ui/tabs/reports/ReportsTableSupport.cpp", "insertRow(");
        requireNotContains("src/ui/tabs/reports/ReportsTableSupport.cpp", "setItem(");
    }
};

QTEST_MAIN(RegressionPerformanceTzFinal100ClosureTests)
#include "RegressionPerformanceTzFinal100ClosureTests.moc"
