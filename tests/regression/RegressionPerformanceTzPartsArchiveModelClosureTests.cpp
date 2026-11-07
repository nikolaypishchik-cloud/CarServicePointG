#include "TestSourceAssertions.h"
#include "ui/tabs/parts/PartsPageTableModelAdapter.h"

class RegressionPerformanceTzPartsArchiveModelClosureTests final : public QObject
{
    Q_OBJECT
private slots:
    void partsArchiveUsesQTableViewAndAbstractModel()
    {
        using namespace cs::tests;
        requireContains("src/ui/tabs/parts/PartsPage.h", "QTableView* m_table = nullptr");
        requireContains("src/ui/tabs/parts/PartsPage.h", "PartsArchiveTableModel* m_partsModel = nullptr");
        requireContains("src/ui/tabs/parts/PartsPageTableModelAdapter.h", "class PartsArchiveTableModel final : public QAbstractTableModel");
        requireContains("src/ui/tabs/parts/PartsPageTableModelAdapter.cpp", "beginResetModel();");
        requireContains("src/ui/tabs/parts/PartsPageTableModelAdapter.cpp", "endResetModel();");
        requireContains("src/ui/tabs/parts/PartsPageBuildUi.cpp", "m_table = new QTableView(this)");
        requireContains("src/ui/tabs/parts/PartsPageBuildUi.cpp", "m_table->setModel(m_partsModel)");
        requireNotContains("src/ui/tabs/parts/PartsPage.h", "QTableWidget* m_table");
        requireNotContains("src/ui/tabs/parts/PartsPageBuildUi.cpp", "new QTableWidget");
    }

    void partsArchivePopulationDoesNotAllocateWidgetItems()
    {
        using namespace cs::tests;
        requireContains("src/ui/tabs/parts/PartsPageRuntime.cpp", "m_partsModel->setRows(adaptedRows.rows)");
        requireContains("src/ui/tabs/parts/PartsPageRuntime.cpp", "m_partsModel->replaceRow(row, rowData)");
        requireContains("src/ui/tabs/parts/PartsPageRuntime.cpp", "m_rowsSnapshot.at(row)");
        requireNotContains("src/ui/tabs/parts/PartsPageRuntime.cpp", "setRowCount(");
        requireNotContains("src/ui/tabs/parts/PartsPageRuntime.cpp", "clearContents(");
        requireNotContains("src/ui/tabs/parts/PartsPageRuntime.cpp", "new QTableWidgetItem");
        requireNotContains("src/ui/tabs/parts/PartsPageRuntime.cpp", "setItem(");
        requireNotContains("src/ui/tabs/parts/PartsPageRuntime.cpp", "m_table->item(");
    }

    void paymentDisplaySupportsPartialAndPaidStates()
    {
        using namespace PartsPageTableModelAdapter;
        PartsArchiveTableModel model;
        TableRow row; row.id=1; row.storeDisplay=QString::fromUtf8("Еліт");
        model.setRows({row});
        const auto cell=model.index(0,model.columnCount()-1);
        QCOMPARE(model.data(cell).toString(),QString::fromUtf8("Не сплачено"));
        row.partiallyPaid=true; QVERIFY(model.replaceRow(0,row));
        QCOMPARE(model.data(cell).toString(),QString::fromUtf8("Частково"));
        row.partiallyPaid=false; row.paid=true; QVERIFY(model.replaceRow(0,row));
        QCOMPARE(model.data(cell).toString(),QString::fromUtf8("Сплачено"));
    }

    void partsArchiveOverduePaidRolesStayModelDriven()
    {
        using namespace cs::tests;
        requireContains("src/ui/tabs/parts/PartsPageTableModelAdapter.cpp", "Qt::BackgroundRole");
        requireContains("src/ui/tabs/parts/PartsPageTableModelAdapter.cpp", "col == ColPaid ? 0 : row.overdueSeverity");
        requireContains("src/ui/tabs/parts/PartsPageTableModelAdapter.cpp", "return row.partiallyPaid ? QString::fromUtf8(\"Частково\") : paidDisplayText(row.paid)");
        requireContains("src/ui/tabs/parts/PartsPageTableModelAdapter.cpp", "emit dataChanged(left, right");
    }
};

QTEST_MAIN(RegressionPerformanceTzPartsArchiveModelClosureTests)
#include "RegressionPerformanceTzPartsArchiveModelClosureTests.moc"
