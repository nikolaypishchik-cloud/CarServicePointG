#include <QtTest/QtTest>
#include "../TestSourceAssertions.h"

class P2AcceptanceCriteriaTests final : public QObject {
    Q_OBJECT

private slots:
    void architectureAcceptanceCriteriaGuard();
    void importAndAttachmentAcceptanceCriteriaGuard();
    void importedPartAndEditorAcceptanceCriteriaGuard();
    void updateAndP0StabilityAcceptanceCriteriaGuard();
};

void P2AcceptanceCriteriaTests::architectureAcceptanceCriteriaGuard()
{
    const QString architectureGate = cs::tests::readSourceFile("scripts/check_architecture.py");
    const QString cmake = cs::tests::readSourceFile("CMakeLists.txt");
    const QString workflow = cs::tests::readSourceFile(".github/workflows/structure-contract.yml");

    QVERIFY2(architectureGate.contains(QStringLiteral("UI layer must not call Database::db() directly"))
                 && architectureGate.contains(QStringLiteral("UI layer must not use Qt SQL query/database primitives directly"))
                 && architectureGate.contains(QStringLiteral("UI layer must not block on QProcess::waitForFinished()")),
             "P2 p4 readiness: src/ui must stay free of direct SQL/Database::db and blocking waitForFinished calls.");
    QVERIFY2(architectureGate.contains(QStringLiteral("Service layer must not show QMessageBox directly"))
                 && architectureGate.contains(QStringLiteral("Repository layer must not perform UI/notification side effects")),
             "P2 p4 readiness: repository/service layers must not own UI side effects.");
    QVERIFY2(cmake.contains(QStringLiteral("p2_acceptance_tests_gate"))
                 && cmake.contains(QStringLiteral("check_p2_acceptance_tests.py"))
                 && QRegularExpression("(?m)^\\s+run: python -B scripts/run_change_gates.py --static-only\\s*$").match(workflow).hasMatch()
                 && cs::tests::readSourceFile("scripts/run_change_gates.py").section("REQUIRED_TESTS", 0, 0).contains("'check_p2_acceptance_tests.py'"),
             "P2 p4 readiness: acceptance gate must be wired into local build and CI workflow.");
}

void P2AcceptanceCriteriaTests::importAndAttachmentAcceptanceCriteriaGuard()
{
    const QString importUi = cs::tests::readSourceFile("src/ui/tabs/orders/WorkPartsDialogExcelSupport.cpp");
    const QString importJob = cs::tests::readSourceFile("src/core/import/ImportJob.cpp");
    const QString persistence = cs::tests::readSourceFile("src/ui/tabs/orders/WorkPartsPersistenceCoordinator.cpp");
    const QString attachmentService = cs::tests::readSourceFile("src/core/services/orders/OrderAttachmentService.cpp");

    QVERIFY2(!importUi.contains(QStringLiteral("waitForFinished("))
                 && importJob.contains(QStringLiteral("ImportJob"))
                 && importJob.contains(QStringLiteral("AsyncThreadScope::isCancellationRequested")),
             "P2 p4 readiness: invoice import must not block the UI thread and must run through cancellation-aware ImportJob.");
    QVERIFY2(persistence.contains(QStringLiteral("queued_after_db_commit"))
                 && persistence.contains(QStringLiteral("attachment_save_begin"))
                 && persistence.contains(QStringLiteral("attachment_save_fail")),
             "P2 p4 readiness: attachment file operations must be staged outside the DB transaction and observable after commit.");
    QVERIFY2(persistence.contains(QStringLiteral("attachment_save_failed_dirty_kept"))
                 && persistence.contains(QStringLiteral("m_dirtyPartRows.insert"))
                 && persistence.contains(QStringLiteral("dirty_cleared_after_deferred_attachment"))
                 && attachmentService.contains(QStringLiteral("return false")),
             "P2 p4 readiness: attachment failure must not clear dirty-state or hide failed persistence.");
}

void P2AcceptanceCriteriaTests::importedPartAndEditorAcceptanceCriteriaGuard()
{
    const QString importedTests = cs::tests::readSourceFile("tests/regression/RegressionWorkPartsP0ImportedPartsTests.cpp");
    const QString importCoordinator = cs::tests::readSourceFile("src/ui/tabs/orders/WorkPartsImportCoordinator.cpp");
    const QString persistence = cs::tests::readSourceFile("src/ui/tabs/orders/WorkPartsPartPersistence.cpp");
    const QString editor = cs::tests::inlineEditorImplementation();
    const QString uiBuilder = cs::tests::readSourceFile("src/ui/tabs/orders/WorkPartsDialogUiBuilder.cpp");

    QVERIFY2(importedTests.contains(QStringLiteral("importedPartDateEditPreservesBlankOrderedDateAndSeparatesInvoiceDate"))
                 && persistence.contains(QStringLiteral("payloadInput.preserveEmptyOrderedAt = importedInvoiceRow"))
                 && persistence.contains(QStringLiteral("payloadInput.preserveEmptyDeliveryDate = importedInvoiceRow")),
             "P2 p4 readiness: editing imported dates must stay crash-safe and preserve explicit blank dates.");
    QVERIFY2(importCoordinator.contains(QStringLiteral("transfer_to_order_viewmodel"))
                 && importCoordinator.contains(QStringLiteral("RoleImportedInvoiceRow"))
                 && importCoordinator.contains(QStringLiteral("m_dirtyPartRows.insert")),
             "P2 p4 readiness: imported parts must be transferred into the order-parts view model and marked dirty for saving.");
    QVERIFY2(editor.contains(QStringLiteral("flushInlineEditorValueToModel"))
                 && editor.contains(QStringLiteral("queuedEditorActionStillValid"))
                 && editor.contains(QStringLiteral("csTableRevision"))
                 && uiBuilder.contains(QStringLiteral("inlineEditingPersistentIndex(m_partsTable)")),
             "P2 p4 readiness: Enter/Tab must commit once and stale queued editor actions must be cancelled after refresh/sort.");
}

void P2AcceptanceCriteriaTests::updateAndP0StabilityAcceptanceCriteriaGuard()
{
    static constexpr int P0_STABLE_REPEAT_COUNT = 20;

    const QString update = cs::tests::readSourceFile("src/app/update/UpdateService.cpp");
    const QString support = cs::tests::updateSupportImplementation();
    const QString testsModule = cs::tests::readSourceFile("cmake/modules/CarServiceTests.cmake");

    QVERIFY2(update.contains(QStringLiteral("restart_old"))
                 && update.contains(QStringLiteral("restore_backup"))
                 && update.contains(QStringLiteral("update_failed"))
                 && update.contains(QStringLiteral("CarServicePointG.exe")),
             "P2 p4 readiness: update must apply atomically or enter rollback/restart_old failure flow.");
    const int hashStart = support.indexOf(QStringLiteral("QString computeFileSha256"));
    QVERIFY2(hashStart >= 0, "P2 p4 readiness: computeFileSha256 implementation must exist.");
    int hashEnd = support.indexOf(QStringLiteral("UpdateService::ReleaseInfo parseReleaseInfo"), hashStart);
    if (hashEnd < 0) hashEnd = support.size();
    const QString hashBlock = support.mid(hashStart, hashEnd - hashStart);
    QVERIFY2(hashBlock.contains(QStringLiteral("QCryptographicHash hash(QCryptographicHash::Sha256)"))
                 && hashBlock.contains(QStringLiteral("hash.addData(&f)"))
                 && !hashBlock.contains(QStringLiteral("readAll()")),
             "P2 p4 readiness: update package hash verification must stream only inside computeFileSha256; network reply bodies may still use readAll().");
    QVERIFY2(support.contains(QStringLiteral("if (Start-ElevatedSelfIfNeeded)"))
                 && support.contains(QStringLiteral("Verb RunAs"))
                 && support.contains(QStringLiteral("/VERYSILENT"))
                 && support.contains(QStringLiteral("OLD_VERSION_SCAN"))
                 && support.contains(QStringLiteral("OLD_VERSION_CLEANUP"))
                 && support.contains(QStringLiteral("SHORTCUT_REFRESH"))
                 && support.contains(QStringLiteral("START_UPDATED_APP"))
                 && support.contains(QStringLiteral("VERIFY_SIGNATURE")),
             "P2 p4 readiness: updater handoff must be elevated/silent and cover signature, cleanup, shortcuts and updated app launch stages.");
    QVERIFY2(support.contains(QStringLiteral("manifest_download_failed"))
                 && support.contains(QStringLiteral("no_valid_installer_asset"))
                 && support.contains(QStringLiteral("sha256_missing"))
                 && support.contains(QStringLiteral("sha256_mismatch"))
                 && support.contains(QStringLiteral("signature_invalid"))
                 && support.contains(QStringLiteral("updated_app_start_failed")),
             "P2 p4 readiness: update reporter must have stable failed reasons for manifest, asset, SHA, signature and launch failures.");
    QVERIFY2(P0_STABLE_REPEAT_COUNT == 20
                 && testsModule.contains(QStringLiteral("RegressionP0SQLiteIntegrationTests"))
                 && testsModule.contains(QStringLiteral("RegressionWorkPartsP0ImportedPartsTests"))
                 && testsModule.contains(QStringLiteral("RegressionP0InvoiceImportScenarioTests")),
             "P2 p4 readiness: P0 regression pack must stay registered for a 20-repeat stability run before release.");
}

QTEST_MAIN(P2AcceptanceCriteriaTests)
#include "P2AcceptanceCriteriaTests.moc"
