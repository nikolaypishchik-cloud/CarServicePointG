#include <QtTest/QtTest>

#include "../TestSourceAssertions.h"
#include "ui/tabs/parts/PartsPageTableModelAdapter.h"

class RegressionPerformanceTzFinalGapGuardsTests : public QObject
{
    Q_OBJECT
private slots:
    void partsPageUsesPreparedVisibleIndexesForSelectionAndPaidPatch()
    {
        const QString header = cs::tests::readSourceFile("src/ui/tabs/parts/PartsPageTableModelAdapter.h");
        const QString model = cs::tests::readSourceFile("src/ui/tabs/parts/PartsPageTableModelAdapter.cpp");
        const QString runtime = cs::tests::readSourceFile("src/ui/tabs/parts/PartsPageRuntime.cpp");

        QVERIFY2(header.contains(QStringLiteral("m_rowByPartId"))
                     && header.contains(QStringLiteral("m_firstRowByOrderUid"))
                     && header.contains(QStringLiteral("m_firstRowByOrderNumber")),
                 "P1 PartsPage must keep prepared visible indexes instead of scanning table rows for every paid toggle/selection restore.");
        QVERIFY2(model.contains(QStringLiteral("m_rowByPartId.reserve"))
                     && model.contains(QStringLiteral("m_firstRowByOrderUid.reserve"))
                     && model.contains(QStringLiteral("m_firstRowByOrderNumber.reserve")),
                 "P1 PartsPage visible indexes must be rebuilt once per snapshot apply with reserved hash storage.");
        QVERIFY2(runtime.contains(QStringLiteral("visibleRowForPartId(selectedPartBefore)"))
                     && runtime.contains(QStringLiteral("firstVisibleRowForOrderUid(selectedOrderUidBefore)"))
                     && runtime.contains(QStringLiteral("firstVisibleRowForOrderNumber(selectedOrderBefore)")),
                 "P1 PartsPage selection restore must use prepared indexes, not row-by-row table scans.");
    }

    void modelIndexesTrackReplacementAndReset()
    {
        using namespace PartsPageTableModelAdapter;
        PartsArchiveTableModel model;
        TableRow a; a.id=11; a.orderUid="order-a"; a.orderNumber=4;
        TableRow b=a; b.id=12;
        model.setRows({a,b});
        QCOMPARE(model.rowByPartId(12),1);
        QCOMPARE(model.firstRowByOrderUid("order-a"),0);
        QCOMPARE(model.firstRowByOrderNumber(4),0);
        TableRow c; c.id=13; c.orderUid="order-b"; c.orderNumber=5;
        QVERIFY(model.replaceRow(0,c));
        QCOMPARE(model.rowByPartId(11),-1);
        QCOMPARE(model.rowByPartId(13),0);
        QCOMPARE(model.firstRowByOrderUid("order-a"),1);
        QCOMPARE(model.firstRowByOrderNumber(4),1);
        model.setRows({});
        QCOMPARE(model.rowByPartId(12),-1);
        QCOMPARE(model.firstRowByOrderUid("order-b"),-1);
        QCOMPARE(model.firstRowByOrderNumber(5),-1);
    }

    void importProcessOutputIsBoundedAndLogsTruncation()
    {
        const QString pdfStage = cs::tests::readSourceFile("src/core/import/internal/PdfTextExtractionStage.ipp");
        QVERIFY2(pdfStage.contains(QStringLiteral("kMaxImportProcessChannelBytes"))
                     && pdfStage.contains(QStringLiteral("appendBoundedImportProcessBytes")),
                 "P1/P2 import external-process stdout/stderr must be bounded to avoid memory spikes on noisy OCR/PDF tools.");
        QVERIFY2(pdfStage.contains(QStringLiteral("stdOutTruncated"))
                     && pdfStage.contains(QStringLiteral("stdErrTruncated"))
                     && pdfStage.contains(QStringLiteral("stdoutTruncated=%6"))
                     && pdfStage.contains(QStringLiteral("stderrTruncated=%7")),
                 "P1/P2 import external-process logs must expose stdout/stderr truncation state for diagnostics.");
    }

    void reportsSnapshotApplyUsesModelResetNotWidgetItems()
    {
        const QString reports = cs::tests::readSourceFile("src/ui/tabs/reports/ReportsTableSupport.cpp");
        QVERIFY2(reports.contains(QStringLiteral("beginResetModel()"))
                     && reports.contains(QStringLiteral("endResetModel()")),
                 "P2 report table batch-apply must use model reset around snapshot replacement.");
        QVERIFY2(reports.contains(QStringLiteral("table->clearSpans();")),
                 "P2 report table batch-apply must clear stale empty-state spans before applying a new snapshot.");
        QVERIFY2(!reports.contains(QStringLiteral("insertRow("))
                     && !reports.contains(QStringLiteral("setItem(")),
                 "P2 report tables must not use row/item loops for large snapshot application.");
    }
};

QTEST_MAIN(RegressionPerformanceTzFinalGapGuardsTests)
#include "RegressionPerformanceTzFinalGapGuardsTests.moc"
