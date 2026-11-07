#include <QtTest/QtTest>
#include "../TestSourceAssertions.h"

class RegressionP1PartsPageDelegateProxyTests final : public QObject {
    Q_OBJECT
private slots:
    void paidColumnUsesDelegateNotCellWidgets();
    void sortingFilteringStaysInAdapterBoundary();
    void textSearchDoesNotReapplyDateRangeInAdapter();
};

void RegressionP1PartsPageDelegateProxyTests::paidColumnUsesDelegateNotCellWidgets()
{
    const QString cpp = cs::tests::readSourceFile("src/ui/tabs/parts/PartsPage.cpp");
    const QString ui = cs::tests::readSourceFile("src/ui/tabs/parts/PartsPageBuildUi.cpp");
    const QString runtime = cs::tests::readSourceFile("src/ui/tabs/parts/PartsPageRuntime.cpp");

    const QString prelude = cs::tests::readSourceFile("src/ui/tabs/parts/PartsPageModulePrelude.h");
    const QString delegates = cs::tests::readSourceFile("src/ui/tabs/parts/internal/PartsPagePreludeDelegates.ipp");
    const QString helpers = cs::tests::readSourceFile("src/ui/tabs/parts/internal/PartsPagePreludeColumns.ipp");
    QVERIFY(cs::tests::sourceIncludesDirective(ui, "ui/tabs/parts/PartsPageModulePrelude.h"));
    QVERIFY(cs::tests::sourceIncludesDirective(prelude, "ui/tabs/parts/internal/PartsPagePreludeDelegates.ipp"));
    QVERIFY(cs::tests::sourceIncludesDirective(prelude, "ui/tabs/parts/internal/PartsPagePreludeColumns.ipp"));
    const auto paintsStatus = [](const QString& d, const QString& h) {
        return d.contains("class PartsPaidStatusDelegate")
            && d.contains("drawPaidStatusButton(painter, option.rect, option.font, text, paid, paymentActionsEnabled, index.data(Qt::UserRole + 4).toBool())")
            && h.contains("QLinearGradient fill") && h.contains("painter->drawText");
    };
    QVERIFY(paintsStatus(delegates, helpers));
    QString noPaint = delegates;
    noPaint.replace("drawPaidStatusButton(", "removed(");
    QVERIFY(!paintsStatus(noPaint, helpers));
    QVERIFY2(cpp.contains(QStringLiteral("class PartsPaidStatusDelegate"))
                 && cpp.contains(QStringLiteral("drawPaidStatusButton(painter,")),
             "P1 PartsPage p3 requires button/state rendering to live in a delegate, not in per-row widgets.");
    QVERIFY2(ui.contains(QStringLiteral("setItemDelegateForColumn(ColPaid, new PartsPaidStatusDelegate")),
             "The Paid column must be wired to the delegate.");
    QVERIFY2(!runtime.contains(QStringLiteral("setCellWidget"))
                 && !runtime.contains(QStringLiteral("cellWidget"))
                 && !runtime.contains(QStringLiteral("findChild<QPushButton*")),
             "P1 PartsPage p3 forbids QWidget/QPushButton per row for large tables.");
}

void RegressionP1PartsPageDelegateProxyTests::sortingFilteringStaysInAdapterBoundary()
{
    const QString header = cs::tests::readSourceFile("src/ui/tabs/parts/PartsPageTableModelAdapter.h");
    const QString adapter = cs::tests::readSourceFile("src/ui/tabs/parts/PartsPageTableModelAdapter.cpp");
    const QString runtime = cs::tests::readSourceFile("src/ui/tabs/parts/PartsPageRuntime.cpp");

    QVERIFY2(header.contains(QStringLiteral("struct ViewPolicy"))
                 && header.contains(QStringLiteral("textFilter"))
                 && header.contains(QStringLiteral("QDate from"))
                 && header.contains(QStringLiteral("QDate to")),
             "P1 PartsPage p4 requires a model/proxy-style policy boundary for filtering.");
    QVERIFY2(adapter.contains(QStringLiteral("matchesDatePolicy"))
                 && adapter.contains(QStringLiteral("PartsQueryService::sortRowsForTable(&out.snapshots, effectiveToday)"))
                 && adapter.contains(QStringLiteral("std::stable_sort")),
             "Sorting/filtering must happen in the adapter boundary before QTableWidget population.");
    QVERIFY2(!runtime.contains(QStringLiteral("setRowHidden"))
                 && !runtime.contains(QStringLiteral("PartsQueryService::sortRowsForTable(&m_rowsSnapshot)"))
                 && runtime.contains(QStringLiteral("PartsPageTableModelAdapter::build(rows, viewPolicy)")),
             "PartsPage must consume ordered/filtered adapter rows instead of hiding or manually reordering UI rows.");
}


void RegressionP1PartsPageDelegateProxyTests::textSearchDoesNotReapplyDateRangeInAdapter()
{
    const QString adapter = cs::tests::readSourceFile("src/ui/tabs/parts/PartsPageTableModelAdapter.cpp");

    QVERIFY2(adapter.contains(QStringLiteral("if (!hasTextFilter && !matchesDatePolicy(rowData, policy)) continue;"))
                 && adapter.contains(QStringLiteral("must not be narrowed by the saved date range")),
             "Searching on the parts page must ignore the saved date range in the adapter as well as in PartsQueryService; otherwise manual rows outside the date range disappear for an order-number search.");
}

QTEST_MAIN(RegressionP1PartsPageDelegateProxyTests)
#include "RegressionP1PartsPageDelegateProxyTests.moc"
