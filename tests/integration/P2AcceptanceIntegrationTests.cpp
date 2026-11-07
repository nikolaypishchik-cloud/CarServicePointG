#include <QtTest/QtTest>
#include "../TestSourceAssertions.h"

class P2AcceptanceIntegrationTests final : public QObject {
    Q_OBJECT

private slots:
    void sqliteTransactionRollbackIntegrationGuard();
    void importedPartsEditSaveReopenIntegrationGuard();
    void invoiceImportWithoutExcelIntegrationGuard();
    void backupRecoveryAfterSaveIntegrationGuard();
    void updatePackageApplyRollbackIntegrationGuard();
};

void P2AcceptanceIntegrationTests::sqliteTransactionRollbackIntegrationGuard()
{
    const QString sqliteTests = cs::tests::readSourceFile("tests/regression/RegressionP0SQLiteIntegrationTests.cpp");
    const QString executor = cs::tests::readSourceFile("src/core/services/MutationExecutor.cpp");
    const QString runner = cs::tests::readSourceFile("src/infrastructure/db/sqlite/TransactionRunner.cpp");

    QVERIFY2(sqliteTests.contains(QStringLiteral("batchSaveWorksAndPartsCommitsAtomically")),
             "P2 integration acceptance: batch save works+parts scenario must stay registered.");
    QVERIFY2(sqliteTests.contains(QStringLiteral("rollbackOnAttachmentStageFailureLeavesDirtyDataUncommitted")),
             "P2 integration acceptance: rollback on attachment failure scenario must stay registered.");
    QVERIFY2(sqliteTests.contains(QStringLiteral("databaseLockedSimulationReportsFailure")),
             "P2 integration acceptance: database locked simulation must stay registered.");
    QVERIFY2(sqliteTests.contains(QStringLiteral("repeatedOpenEditSaveImportedPartsKeepsDatesExplicit")),
             "P2 integration acceptance: repeated imported part open/edit/save scenario must stay registered.");
    QVERIFY2(executor.contains(QStringLiteral("transaction"))
                 || runner.contains(QStringLiteral("rollback")),
             "Mutation/transaction layer must keep an observable transaction boundary.");
}

void P2AcceptanceIntegrationTests::importedPartsEditSaveReopenIntegrationGuard()
{
    const QString imported = cs::tests::readSourceFile("tests/regression/RegressionWorkPartsP0ImportedPartsTests.cpp");
    const QString partPersistence = cs::tests::readSourceFile("src/ui/tabs/orders/WorkPartsPartPersistence.cpp");
    const QString partPolicy = cs::tests::partEditServicesImplementation();

    QVERIFY2(imported.contains(QStringLiteral("importedPartDateEditPreservesBlankOrderedDateAndSeparatesInvoiceDate")),
             "Imported date edit scenario must be covered.");
    QVERIFY2(imported.contains(QStringLiteral("importedPartNameBrandAndPriceEditUseCanonicalSavePayload")),
             "Imported name/brand/price edit scenario must be covered.");
    QVERIFY2(imported.contains(QStringLiteral("importEditSaveReopenPathKeepsImportedPartStateAndDirtyRowsSafe")),
             "Import -> edit -> save -> reopen scenario must be covered.");
    QVERIFY2(partPersistence.contains(QStringLiteral("payloadInput.preserveEmptyOrderedAt = importedInvoiceRow"))
                 && partPersistence.contains(QStringLiteral("payloadInput.preserveEmptyDeliveryDate = importedInvoiceRow")),
             "Imported save path must keep explicit blank-date preservation.");
    QVERIFY2(partPolicy.contains(QStringLiteral("partRowHasImportedInvoiceMarker")),
             "The edit/save path must identify imported invoice rows before persistence.");
}

void P2AcceptanceIntegrationTests::invoiceImportWithoutExcelIntegrationGuard()
{
    const QString scenarios = cs::tests::readSourceFile("tests/regression/RegressionP0InvoiceImportScenarioTests.cpp");
    const QString importJob = cs::tests::readSourceFile("src/core/import/ImportJob.cpp");
    const QString xlsParser = cs::tests::readSourceFile("src/core/import/parsers/GenericXlsParser.cpp");
    const QString pdfParser = cs::tests::readSourceFile("src/core/import/parsers/GenericPdfParser.cpp");

    QVERIFY2(scenarios.contains(QStringLiteral("pdfInvoiceWithoutExcelScenarioIsCovered")),
             "PDF invoice without Excel scenario must remain registered.");
    QVERIFY2(scenarios.contains(QStringLiteral("xlsInvoiceWithoutInstalledExcelScenarioIsCovered")),
             "XLS invoice without installed Excel scenario must remain registered.");
    QVERIFY2(pdfParser.contains(QStringLiteral("readPdfInvoiceRowsInternal")),
             "PDF import must stay independent from local Excel.");
    QVERIFY2(xlsParser.contains(QStringLiteral("readSpreadsheetInvoiceRowsInternal")),
             "XLS import must stay behind GenericXlsParser rather than UI/Excel COM calls.");
    QVERIFY2(importJob.contains(QStringLiteral("ImportJob"))
                 && importJob.contains(QStringLiteral("StructuredDiagnosticsLogger::logException")),
             "Invoice import integration guard must keep async job/error diagnostics coverage.");
}

void P2AcceptanceIntegrationTests::backupRecoveryAfterSaveIntegrationGuard()
{
    const QString backupTest = cs::tests::readSourceFile("tests/BackupManagerCriticalFlowTests.cpp");
    const QString recoveryTest = cs::tests::readSourceFile("tests/RecoveryConflictResolutionTests.cpp");
    const QString backup = cs::tests::readSourceFile("src/core/backup/BackupManager.cpp");
    const QString restore = cs::tests::readSourceFile("src/core/backup/RestoreCoordinator.cpp");
    const QString native = cs::tests::readSourceFile("src/core/services/NativeBackupRestoreService.cpp");

    QVERIFY2(backupTest.contains(QStringLiteral("BackupManagerCriticalFlowTests")),
             "Backup critical flow test must stay registered.");
    QVERIFY2(recoveryTest.contains(QStringLiteral("RecoveryConflictResolutionTests")),
             "Recovery conflict resolution test must stay registered.");
    QVERIFY2(backup.contains(QStringLiteral("Backup")) || backup.contains(QStringLiteral("snapshot")),
             "Backup manager must keep a post-save backup/snapshot flow.");
    QVERIFY2(restore.contains(QStringLiteral("Restore")) || native.contains(QStringLiteral("restore")),
             "Restore/recovery flow must stay wired for backup recovery after save.");
}

void P2AcceptanceIntegrationTests::updatePackageApplyRollbackIntegrationGuard()
{
    const QString update = cs::tests::readSourceFile("src/app/update/UpdateService.cpp");
    const QString support = cs::tests::updateSupportImplementation();

    QVERIFY2(update.contains(QStringLiteral("stage"))
                 && update.contains(QStringLiteral("packageSha256"))
                 && update.contains(QStringLiteral("sha256")),
             "Update helper must log/apply stage and hash verification markers.");
    QVERIFY2(update.contains(QStringLiteral("backup"))
                 && update.contains(QStringLiteral("restart_old"))
                 && update.contains(QStringLiteral("restore_backup"))
                 && update.contains(QStringLiteral("update_failed")),
             "Update helper must keep backup and rollback/restart_old failure flow.");
    QVERIFY2(update.contains(QStringLiteral("CarServicePointG.exe"))
                 && update.contains(QStringLiteral("Qt6Core.dll")),
             "Update helper must verify installed executable/resources.");
    QVERIFY2(support.contains(QStringLiteral("computeFileSha256"))
                 && support.contains(QStringLiteral("hash.addData(&f)")),
             "Update apply/rollback integration guard must keep streaming hash verification.");
}

QTEST_MAIN(P2AcceptanceIntegrationTests)
#include "P2AcceptanceIntegrationTests.moc"
