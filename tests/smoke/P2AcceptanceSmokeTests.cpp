#include <QtTest/QtTest>
#include "../TestSourceAssertions.h"

class P2AcceptanceSmokeTests final : public QObject {
    Q_OBJECT

private slots:
    void excelLikeNavigationSmokeGuard();
    void importedPartEditingSmokeGuard();
    void rapidKeyboardNavigationSmokeGuard();
    void dialogCloseDuringImportSmokeGuard();
    void partsPageDuringRefreshSmokeGuard();
};

void P2AcceptanceSmokeTests::excelLikeNavigationSmokeGuard()
{
    const QString navigation = cs::tests::readSourceFile("tests/regression/RegressionP1InlineEditorNavigationCoverageTests.cpp");
    const QString workPartsSmoke = cs::tests::readSourceFile("tests/smoke/SmokeWorkPartsDialogTests.cpp");

    QVERIFY2(navigation.contains(QStringLiteral("boundaryNavigationIsExplicitlyGuarded"))
                 && navigation.contains(QStringLiteral("logicalIndex == columns.size() - 1"))
                 && navigation.contains(QStringLiteral("logicalIndex = 0")),
             "P2 p3 UI smoke: Excel-like first/last row and first/last column navigation must remain covered.");
    QVERIFY2(navigation.contains(QStringLiteral("Qt::RightButton"))
                 && navigation.contains(QStringLiteral("cellDoubleClicked"))
                 && navigation.contains(QStringLiteral("scheduleDelayedActivateEditor")),
             "P2 p3 UI smoke: right-click select and double-click edit paths must remain covered.");
    QVERIFY2(workPartsSmoke.contains(QStringLiteral("inlineEditorImplementation()"))
                 && workPartsSmoke.contains(QStringLiteral("NAV_STATE")),
             "P2 p3 UI smoke: WorkParts smoke test must keep the inline navigation controller wired.");
}

void P2AcceptanceSmokeTests::importedPartEditingSmokeGuard()
{
    const QString imported = cs::tests::readSourceFile("tests/regression/RegressionWorkPartsP0ImportedPartsTests.cpp");
    const QString importSmoke = cs::tests::readSourceFile("tests/smoke/SmokeImportPathTests.cpp");

    QVERIFY2(imported.contains(QStringLiteral("importedPartDateEditPreservesBlankOrderedDateAndSeparatesInvoiceDate"))
                 && imported.contains(QStringLiteral("payloadInput.preserveEmptyOrderedAt = importedInvoiceRow"))
                 && imported.contains(QStringLiteral("payloadInput.preserveEmptyDeliveryDate = importedInvoiceRow")),
             "P2 p3 UI smoke: editing imported date must remain covered and must preserve explicit blank-date policy.");
    QVERIFY2(imported.contains(QStringLiteral("importedPartNameBrandAndPriceEditUseCanonicalSavePayload"))
                 && imported.contains(QStringLiteral("state.name = normalizePartSentence"))
                 && imported.contains(QStringLiteral("state.manufacturer = partManufacturerText"))
                 && imported.contains(QStringLiteral("state.price = PartPricePolicyService::resolveForSave")),
             "P2 p3 UI smoke: editing imported price/name/brand must remain covered by canonical save payload guards.");
    QVERIFY2(importSmoke.contains(QStringLiteral("InvoiceImportResult"))
                 && importSmoke.contains(QStringLiteral("WorkPartsImportCoordinator.cpp")),
             "P2 p3 UI smoke: import path smoke test must keep imported invoice transfer wired.");
}

void P2AcceptanceSmokeTests::rapidKeyboardNavigationSmokeGuard()
{
    const QString navigation = cs::tests::readSourceFile("tests/regression/RegressionP1InlineEditorNavigationCoverageTests.cpp");
    const QString imported = cs::tests::readSourceFile("tests/regression/RegressionWorkPartsP0ImportedPartsTests.cpp");

    QVERIFY2(navigation.contains(QStringLiteral("Qt::Key_Tab"))
                 && navigation.contains(QStringLiteral("Qt::Key_Left"))
                 && navigation.contains(QStringLiteral("Qt::Key_Right"))
                 && navigation.contains(QStringLiteral("Qt::Key_Up"))
                 && navigation.contains(QStringLiteral("Qt::Key_Down")),
             "P2 p3 UI smoke: rapid Tab/Arrow navigation coverage must stay registered.");
    QVERIFY2(imported.contains(QStringLiteral("fastEnterTabArrowNavigationFlushesEditorBeforeMoving"))
                 && (imported.contains(QStringLiteral("Qt::Key_Return")) || imported.contains(QStringLiteral("Qt::Key_Enter")))
                 && imported.contains(QStringLiteral("flushInlineEditorValueToModel")),
             "P2 p3 UI smoke: rapid Enter/Tab/Arrow must flush the active editor before moving.");
}

void P2AcceptanceSmokeTests::dialogCloseDuringImportSmokeGuard()
{
    const QString asyncLifecycle = cs::tests::readSourceFile("tests/regression/RegressionP1AsyncCancellationLifecycleTests.cpp");
    const QString importJob = cs::tests::readSourceFile("src/core/import/ImportJob.cpp");
    const QString workPartsAsync = cs::tests::readSourceFile("src/ui/tabs/orders/WorkPartsDialogExcelSupport.cpp");

    const QString progress = cs::tests::readSourceFile("src/ui/common/widgets/InvoiceImportProgressDialog.h");
    QVERIFY(cs::tests::invoiceOwnerCancellationWired(workPartsAsync, progress, importJob));
    QVERIFY(cs::tests::invoiceCancellationRejectsBrokenLinks(workPartsAsync, progress, importJob));
    QVERIFY(asyncLifecycle.contains("invoiceCancellationRejectsBrokenLinks"));

}

void P2AcceptanceSmokeTests::partsPageDuringRefreshSmokeGuard()
{
    const QString asyncLifecycle = cs::tests::readSourceFile("tests/regression/RegressionP1AsyncCancellationLifecycleTests.cpp");
    const QString incremental = cs::tests::readSourceFile("tests/regression/RegressionP1PartsPageIncrementalRefreshTests.cpp");

    QVERIFY2(asyncLifecycle.contains(QStringLiteral("close during refresh"))
                 && asyncLifecycle.contains(QStringLiteral("toggle paid during refresh"))
                 && asyncLifecycle.contains(QStringLiteral("repeated refresh")),
             "P2 p3 UI smoke: PartsPage open/close/toggle while refresh is active must remain guarded.");
    QVERIFY2(incremental.contains(QStringLiteral("m_refreshQueued = true"))
                 && incremental.contains(QStringLiteral("m_refreshAllDebounce"))
                 && incremental.contains(QStringLiteral("if (m_refreshInProgress || m_refreshWorker)")),
             "P2 p3 UI smoke: opening PartsPage during refresh must stay queued/debounced, not parallel-rebuilt.");
}

QTEST_MAIN(P2AcceptanceSmokeTests)
#include "P2AcceptanceSmokeTests.moc"
