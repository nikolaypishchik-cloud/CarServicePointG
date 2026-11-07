#include <QtTest/QtTest>
#include <QString>
#include "../TestSourceAssertions.h"

class RegressionP2LoggingCrashDiagnosticsTests final : public QObject {
    Q_OBJECT

private slots:
    void structuredLoggerHasRequiredFields()
    {
        const QString header = cs::tests::readSourceFile("src/core/diagnostics/StructuredDiagnosticsLogger.h");
        const QString impl = cs::tests::readSourceFile("src/core/diagnostics/StructuredDiagnosticsLogger.cpp");

        QVERIFY(header.contains(QStringLiteral("module")));
        QVERIFY(header.contains(QStringLiteral("operation")));
        QVERIFY(header.contains(QStringLiteral("orderNumber")));
        QVERIFY(header.contains(QStringLiteral("orderUid")));
        QVERIFY(header.contains(QStringLiteral("partId")));
        QVERIFY(header.contains(QStringLiteral("rowUid")));
        QVERIFY(header.contains(QStringLiteral("invoiceNumber")));
        QVERIFY(header.contains(QStringLiteral("transactionId")));
        QVERIFY(header.contains(QStringLiteral("correlationId")));
        QVERIFY(header.contains(QStringLiteral("tableRevision")));

        QVERIFY(impl.contains(QStringLiteral("threadId")));
        QVERIFY(impl.contains(QStringLiteral("WORKPARTS_DIAGNOSTIC")));
        QVERIFY(impl.contains(QStringLiteral("QJsonDocument::Compact")));
        QVERIFY(impl.contains(QStringLiteral("ensureCorrelationId")));
        QVERIFY(impl.contains(QStringLiteral("qCInfo(csWorkPartsTraceLog)")));
        QVERIFY(impl.contains(QStringLiteral("qCWarning(csWorkPartsTraceLog)")));
        QVERIFY(impl.contains(QStringLiteral("qCCritical(csWorkPartsTraceLog)")));
    }

    void crashSafeBreadcrumbRingKeepsLastHundredEvents()
    {
        const QString header = cs::tests::readSourceFile("src/core/diagnostics/StructuredDiagnosticsLogger.h");
        const QString impl = cs::tests::readSourceFile("src/core/diagnostics/StructuredDiagnosticsLogger.cpp");

        QVERIFY(header.contains(QStringLiteral("breadcrumbsSnapshot")));
        QVERIFY(header.contains(QStringLiteral("recordBreadcrumb")));
        QVERIFY(impl.contains(QStringLiteral("kMaxBreadcrumbs = 100")));
        QVERIFY(impl.contains(QStringLiteral("while (ring.size() > kMaxBreadcrumbs)")));
        QVERIFY(impl.contains(QStringLiteral("ring.removeFirst()")));
        QVERIFY(impl.contains(QStringLiteral("timestampUtc")));
    }

    void workPartsDialogLogsRequiredCrashDiagnosticOperations()
    {
        const QString diagnostics = cs::tests::readSourceFile("src/ui/tabs/orders/WorkPartsDiagnosticsSupport.cpp");
        const QString importFlow = cs::tests::readSourceFile("src/ui/tabs/orders/WorkPartsImportCoordinator.cpp");
        const QString persistence = cs::tests::readSourceFile("src/ui/tabs/orders/WorkPartsPersistenceCoordinator.cpp");
        const QString partPersistence = cs::tests::readSourceFile("src/ui/tabs/orders/WorkPartsPartPersistence.cpp");
        const QString servicePersistence = cs::tests::readSourceFile("src/ui/tabs/orders/WorkPartsServicePersistence.cpp");
        const QString editor = cs::tests::inlineEditorImplementation();
        const QString loads = cs::tests::readSourceFile("src/ui/tabs/orders/PartTableController.cpp")
            + cs::tests::readSourceFile("src/ui/tabs/orders/ServiceTableController.cpp");

        QVERIFY(diagnostics.contains(QStringLiteral("dirtyServiceRows")));
        QVERIFY(diagnostics.contains(QStringLiteral("dirtyPartRows")));
        QVERIFY(diagnostics.contains(QStringLiteral("event.correlationId")));
        QVERIFY(importFlow.contains(QStringLiteral("import_start")));
        QVERIFY(importFlow.contains(QStringLiteral("import_end_success")));
        QVERIFY(importFlow.contains(QStringLiteral("row_insert_batch_begin")));
        QVERIFY(persistence.contains(QStringLiteral("transaction_begin")));
        QVERIFY(persistence.contains(QStringLiteral("transaction_commit")));
        QVERIFY(persistence.contains(QStringLiteral("transaction_rollback")));
        QVERIFY(persistence.contains(QStringLiteral("attachment_stage")));
        QVERIFY(persistence.contains(QStringLiteral("attachment_save_fail")));
        QVERIFY(partPersistence.contains(QStringLiteral("row_update_begin")));
        QVERIFY(partPersistence.contains(QStringLiteral("row_update_end")));
        QVERIFY(servicePersistence.contains(QStringLiteral("row_update_begin")));
        QVERIFY(servicePersistence.contains(QStringLiteral("row_update_end")));
        QVERIFY(editor.contains(QStringLiteral("editor_open")));
        QVERIFY(editor.contains(QStringLiteral("editor_commit_begin")));
        QVERIFY(editor.contains(QStringLiteral("editor_commit_close")));
        QVERIFY(loads.contains(QStringLiteral("model_rebuild_begin")));
        QVERIFY(loads.contains(QStringLiteral("model_rebuild_end")));
    }

    void workPartsInformationalTracesDoNotUseRawQInfo()
    {
        const QString support = cs::tests::readSourceFile("src/ui/tabs/orders/WorkPartsDiagnosticsSupport.cpp");
        const QString importFlow = cs::tests::readSourceFile("src/ui/tabs/orders/WorkPartsImportCoordinator.cpp");
        const QString uiBuilder = cs::tests::readSourceFile("src/ui/tabs/orders/WorkPartsDialogUiBuilder.cpp");
        const QString persistence = cs::tests::readSourceFile("src/ui/tabs/orders/WorkPartsPersistenceCoordinator.cpp");
        const QString partPersistence = cs::tests::readSourceFile("src/ui/tabs/orders/WorkPartsPartPersistence.cpp");
        const QString editor = cs::tests::inlineEditorImplementation();
        const QString combined = support + importFlow + uiBuilder + persistence + partPersistence + editor;

        QVERIFY(!combined.contains(QStringLiteral("qInfo")));
        QVERIFY(combined.contains(QStringLiteral("qCDebug(csWorkPartsTraceLog)")));
    }

    void exceptionCatchPathsLogContextAndDoNotReportSuccess()
    {
        const QString importJob = cs::tests::readSourceFile("src/core/import/ImportJob.cpp");
        const QString asyncScope = cs::tests::readSourceFile("src/runtime/AsyncThreadScope.h");
        const QString logger = cs::tests::readSourceFile("src/core/diagnostics/StructuredDiagnosticsLogger.cpp");

        QVERIFY(importJob.contains(QStringLiteral("StructuredDiagnosticsLogger::logException")));
        QVERIFY(asyncScope.contains(QStringLiteral("StructuredDiagnosticsLogger::logException")));
        QVERIFY(importJob.contains(QStringLiteral("failed_not_success")));
        QVERIFY(asyncScope.contains(QStringLiteral("failed_not_success")));
        QVERIFY(importJob.contains(QStringLiteral("unknown_non_std_exception")));
        QVERIFY(asyncScope.contains(QStringLiteral("unknown_non_std_exception")));
        QVERIFY(logger.contains(QStringLiteral("stackTrace")));
        QVERIFY(logger.contains(QStringLiteral("breadcrumbs")));
    }

    void cmakeRegistersDiagnosticsModules()
    {
        const QString core = cs::tests::readSourceFile("cmake/modules/CarServiceSourcesCore.cmake");
        const QString ui = cs::tests::readSourceFile("cmake/modules/CarServiceSourcesUi.cmake");
        const QString tests = cs::tests::readSourceFile("cmake/modules/CarServiceTests.cmake");

        QVERIFY(core.contains(QStringLiteral("src/core/diagnostics/StructuredDiagnosticsLogger.cpp")));
        QVERIFY(core.contains(QStringLiteral("src/core/diagnostics/StructuredDiagnosticsLogger.h")));
        QVERIFY(ui.contains(QStringLiteral("src/ui/tabs/orders/WorkPartsDiagnosticsSupport.cpp")));
        QVERIFY(tests.contains(QStringLiteral("RegressionP2LoggingCrashDiagnosticsTests")));
    }
};

QTEST_MAIN(RegressionP2LoggingCrashDiagnosticsTests)
#include "RegressionP2LoggingCrashDiagnosticsTests.moc"
