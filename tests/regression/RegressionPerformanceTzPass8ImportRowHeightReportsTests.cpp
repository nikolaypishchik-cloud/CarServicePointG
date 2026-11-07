#include "TestSourceAssertions.h"
#include <QDirIterator>

static bool snapshotModelOnly(const QString& source)
{
    return !source.contains(QStringLiteral("QTableWidget"))
        && !source.contains(QStringLiteral("QStandardItemModel"));
}

class RegressionPerformanceTzPass8ImportRowHeightReportsTests final : public QObject
{
    Q_OBJECT
private slots:
    void forbiddenReportModelsAreRejected()
    {
        QVERIFY(snapshotModelOnly(QStringLiteral("TabularSnapshotTableModel model;")));
        QVERIFY(!snapshotModelOnly(QStringLiteral("QTableWidget table;")));
        QVERIFY(!snapshotModelOnly(QStringLiteral("QStandardItemModel model;")));
    }
    void importBatchAppliesRowsWithoutDuplicateMarkerChurn()
    {
        using namespace cs::tests;
        requireContains("src/ui/tabs/orders/WorkPartsImportCoordinator.cpp", "P0 import batch optimization: mark the imported invoice row once");
        requireContains("src/ui/tabs/orders/WorkPartsImportCoordinator.cpp", "setRowCount(firstImportedRow + importRowCount)");
        requireContains("src/ui/tabs/orders/WorkPartsImportCoordinator.cpp", "csImportApplyingRows");
        requireContains("src/ui/tabs/orders/WorkPartsImportCoordinator.cpp", "WorkPartsDialog::applyImportedRowsBatch");
        requireContains("src/ui/tabs/orders/WorkPartsImportCoordinator.cpp", "QSignalBlocker blocker(guard->m_partsTable)");
        requireNotContains("src/ui/tabs/orders/WorkPartsImportCoordinator.cpp", "insertEmptyPartRow(row");
    }

    void rowHeightAndCompactFontsAreVisibleRangeCached()
    {
        using namespace cs::tests;
        requireContains("src/ui/tabs/orders/internal/WorkPartsEditorDelegateState.ipp", "RoleBatchRowHeightSignature");
        requireContains("src/ui/tabs/orders/internal/WorkPartsEditorDelegateState.ipp", "bufferedVisibleBatchRows");
        requireContains("src/ui/tabs/orders/internal/WorkPartsEditorDelegateState.ipp", "row-height lazy sync/cache");
        requireContains("src/ui/tabs/orders/internal/WorkPartsEditorDelegateState.ipp", "headerItem->data(RoleBatchRowHeightSignature)");
        requireContains("src/ui/tabs/orders/internal/WorkPartsEditorCommitGuard.ipp", "RoleCompactFontSignature");
        requireContains("src/ui/tabs/orders/internal/WorkPartsEditorCommitGuard.ipp", "bufferedVisibleMainPartsRows");
        requireContains("src/ui/tabs/orders/internal/WorkPartsEditorCommitGuard.ipp", "fitted-font lazy cache");
        requireNotContains("src/ui/tabs/orders/internal/WorkPartsEditorCommitGuard.ipp", "for (int row = 0; row < table->rowCount(); ++row) {\n        refreshCompactMainPartsCellFontsForRow(table, row);");
    }

    void reportsSnapshotMigrationUsesIndexedSpanRowsOnly()
    {
        using namespace cs::tests;
        requireContains("src/ui/tabs/reports/ReportsTableSupport.h", "const QVector<int>& spanRows() const");
        requireContains("src/ui/tabs/reports/ReportsTableSupport.h", "QVector<int> m_spanRows");
        requireContains("src/ui/tabs/reports/ReportsTableSupport.cpp", "m_spanRows.reserve(rows.size())");
        requireContains("src/ui/tabs/reports/ReportsTableSupport.cpp", "for (int r : model->spanRows())");
        requireContains("src/ui/tabs/reports/ReportsTableSupport.cpp", "Large reports no longer scan every report row");
        const QString root=projectRoot();
        QDirIterator files(root+"/src/ui/tabs/reports", {"*.cpp","*.h","*.hpp","*.ipp"},
                           QDir::Files, QDirIterator::Subdirectories);
        int checked=0;
        while(files.hasNext()) {
            const QString relative=QDir(root).relativeFilePath(files.next());
            QVERIFY2(snapshotModelOnly(readSourceFile(relative.toUtf8().constData())),qPrintable(relative));
            ++checked;
        }
        QVERIFY(checked>0);
    }
};

QTEST_MAIN(RegressionPerformanceTzPass8ImportRowHeightReportsTests)
#include "RegressionPerformanceTzPass8ImportRowHeightReportsTests.moc"
