#include <QtTest/QtTest>
#include "../TestSourceAssertions.h"

static bool boundedRowHeightWork(const QString& source)
{
    return source.contains(QStringLiteral("const int totalRows = m_partsModel ? m_partsModel->rowCount() : 0;"))
        && source.contains(QStringLiteral("if (totalRows > 200)"))
        && source.contains(QStringLiteral("topIndex.row() - 20"))
        && source.contains(QStringLiteral("bottomIndex.row() + 24"))
        && source.contains(QStringLiteral("for (int r = firstRow; r <= lastRow; ++r)"));
}

class RegressionP1PartsPageIncrementalRefreshTests final : public QObject {
    Q_OBJECT
private slots:
    void singlePaidMutationUsesIncrementalPatchNotFullRebuild();
    void largeTableGuardKeepsVisibleRowHeightWorkBounded();
    void unboundedRowHeightWorkIsRejected()
    {
        const QString source = cs::tests::readSourceFile("src/ui/tabs/parts/PartsPageRuntime.cpp");
        QVERIFY(boundedRowHeightWork(source));
        QString broken = source;
        broken.replace(QStringLiteral("for (int r = firstRow; r <= lastRow; ++r)"),
                       QStringLiteral("for (int r = 0; r < totalRows; ++r)"));
        QVERIFY(!boundedRowHeightWork(broken));
        broken = source;
        broken.replace(QStringLiteral("if (totalRows > 200)"), QStringLiteral("if (false)"));
        QVERIFY(!boundedRowHeightWork(broken));
    }
};

void RegressionP1PartsPageIncrementalRefreshTests::singlePaidMutationUsesIncrementalPatchNotFullRebuild()
{
    const QString header = cs::tests::readSourceFile("src/ui/tabs/parts/PartsPage.h");
    const QString runtime = cs::tests::readSourceFile("src/ui/tabs/parts/PartsPageRuntime.cpp");
    const QString adapterHeader = cs::tests::readSourceFile("src/ui/tabs/parts/PartsPageTableModelAdapter.h");
    const QString adapterCpp = cs::tests::readSourceFile("src/ui/tabs/parts/PartsPageTableModelAdapter.cpp");

    QVERIFY2(header.contains(QStringLiteral("applyIncrementalPaidToggle"))
                 && header.contains(QStringLiteral("populateTableRow")),
             "P1 PartsPage p5 requires an explicit incremental single-row patch boundary.");
    QVERIFY2(adapterHeader.contains(QStringLiteral("TableRow adaptRow"))
                 && adapterCpp.contains(QStringLiteral("TableRow adaptRow")),
             "The adapter must expose single-row adaptation so one changed row can be patched without rebuilding the whole table.");
    QVERIFY2(runtime.contains(QStringLiteral("applyIncrementalPaidToggle(partId, newPaidState)"))
                 && runtime.contains(QStringLiteral("PARTS_PAGE_INCREMENTAL_REFRESH_FALLBACK")),
             "togglePaidForPart must try the incremental path first and only fall back to refresh when the row is unavailable.");

    const int resortStart = runtime.indexOf(QStringLiteral("void PartsPage::resortRowsAfterPaidToggle"));
    QVERIFY(resortStart >= 0);
    const int resortEnd = runtime.indexOf(QStringLiteral("void PartsPage::togglePaidForPart"), resortStart);
    QVERIFY(resortEnd > resortStart);
    const QString resortBody = runtime.mid(resortStart, resortEnd - resortStart);
    QVERIFY2(!resortBody.contains(QStringLiteral("populateRows(m_rowsSnapshot)"))
                 && !resortBody.contains(QStringLiteral("setRowCount"))
                 && resortBody.contains(QStringLiteral("scheduleRowHeightsUpdate")),
             "resortRowsAfterPaidToggle must not rebuild the whole QTableWidget for a single paid-state change.");
}

void RegressionP1PartsPageIncrementalRefreshTests::largeTableGuardKeepsVisibleRowHeightWorkBounded()
{
    const QString runtime = cs::tests::readSourceFile("src/ui/tabs/parts/PartsPageRuntime.cpp");

    QVERIFY2(boundedRowHeightWork(runtime),
             "P1 PartsPage p6 requires large-table checks to keep expensive row-height recalculation bounded to visible rows.");
    QVERIFY2(runtime.contains(QStringLiteral("if (m_refreshInProgress || m_refreshWorker)"))
                 && runtime.contains(QStringLiteral("m_refreshQueued = true"))
                 && runtime.contains(QStringLiteral("m_refreshAllDebounce")),
             "Refresh work for 1000+ rows must stay debounced/queued rather than starting parallel full rebuilds.");
}

QTEST_MAIN(RegressionP1PartsPageIncrementalRefreshTests)
#include "RegressionP1PartsPageIncrementalRefreshTests.moc"
