#include <QtTest/QtTest>
#include "../TestSourceAssertions.h"

class RegressionP1PartsPageLargeTableAdapterTests final : public QObject {
    Q_OBJECT
private slots:
    void partsPageUsesModelAdapterBoundary();
    void paidColumnDoesNotCreateWidgetPerRow();
};

void RegressionP1PartsPageLargeTableAdapterTests::partsPageUsesModelAdapterBoundary()
{
    const QString header = cs::tests::readSourceFile("src/ui/tabs/parts/PartsPageTableModelAdapter.h");
    const QString source = cs::tests::readSourceFile("src/ui/tabs/parts/PartsPageTableModelAdapter.cpp");
    const QString runtime = cs::tests::readSourceFile("src/ui/tabs/parts/PartsPageRuntime.cpp");
    const QString cmake = cs::tests::compiledSourceInventory();

    QVERIFY2(header.contains(QStringLiteral("struct TableRow"))
                 && header.contains(QStringLiteral("struct AdaptedRows"))
                 && header.contains(QStringLiteral("struct ViewPolicy"))
                 && header.contains(QStringLiteral("AdaptedRows build")),
             "P1 PartsPage p2 requires a dedicated model-adapter boundary before full QAbstractTableModel migration.");
    QVERIFY2(source.contains(QStringLiteral("PartsQueryService::refreshDerivedFields"))
                 && source.contains(QStringLiteral("PartsQueryService::sortRowsForTable")),
             "Sorting and derived display preparation must move from the widget loop into the adapter.");
    QVERIFY2(runtime.contains(QStringLiteral("PartsPageTableModelAdapter::build(rows, viewPolicy"))
                 && runtime.contains(QStringLiteral("adaptedRows.rows"))
                 && runtime.contains(QStringLiteral("m_rowsSnapshot = adaptedRows.snapshots")),
             "PartsPage::populateRows must consume adapted rows instead of building every display field inline.");
    QVERIFY2(cmake.contains(QStringLiteral("src/ui/tabs/parts/PartsPageTableModelAdapter.cpp")),
             "The PartsPage model adapter implementation must be part of the build.");
}

void RegressionP1PartsPageLargeTableAdapterTests::paidColumnDoesNotCreateWidgetPerRow()
{
    const QString runtime = cs::tests::readSourceFile("src/ui/tabs/parts/PartsPageRuntime.cpp");
    const QString ui = cs::tests::readSourceFile("src/ui/tabs/parts/PartsPageBuildUi.cpp");

    QVERIFY2(!runtime.contains(QStringLiteral("new QPushButton(rowData.paid")),
             "P1 PartsPage p1 forbids creating a QPushButton for every row in the large parts table.");
    QVERIFY2(!runtime.contains(QStringLiteral("setCellWidget(row, ColPaid")),
             "P1 PartsPage p1 requires the Paid column to avoid QWidget-per-row population.");
    const QString model = cs::tests::readSourceFile("src/ui/tabs/parts/PartsPageTableModelAdapter.cpp");
    QVERIFY2(runtime.contains(QStringLiteral("m_partsModel->setRows(adaptedRows.rows)"))
                 && model.contains(QStringLiteral("case ColPaid: return row.paid")),
             "Paid state must be represented as item data so large tables do not allocate row widgets.");
    QVERIFY2(ui.contains(QStringLiteral("QTableView::clicked"))
                 && ui.contains(QStringLiteral("index.column() != ColPaid"))
                 && ui.contains(QStringLiteral("togglePaidForPart(snapshot.id, !snapshot.paid)")),
             "Paid toggling must be routed through a single item-click handler after removing row buttons.");
}

QTEST_MAIN(RegressionP1PartsPageLargeTableAdapterTests)
#include "RegressionP1PartsPageLargeTableAdapterTests.moc"
