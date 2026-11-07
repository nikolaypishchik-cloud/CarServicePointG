#include <QtTest/QtTest>
#include "../TestSourceAssertions.h"

class P2AcceptanceUnitTests final : public QObject {
    Q_OBJECT

private slots:
    void partDatePolicyServiceUnitGuard();
    void partRowPayloadBuilderUnitGuard();
    void importParserUnitGuard();
    void workPartsDirtyStateUnitGuard();
    void attachmentServiceUnitGuard();
    void updateHashAndPathLogicUnitGuard();
};

void P2AcceptanceUnitTests::partDatePolicyServiceUnitGuard()
{
    const QString policy = cs::tests::partEditServicesImplementation();
    const QString persistence = cs::tests::readSourceFile("src/ui/tabs/orders/WorkPartsPartPersistence.cpp");
    const QString payload = cs::tests::readSourceFile("src/core/services/orders/PartRowPayloadBuilderService.cpp");

    QVERIFY2(policy.contains(QStringLiteral("class PartDatePolicyService final")),
             "P2 unit acceptance: PartDatePolicyService must remain explicit and test-visible.");
    QVERIFY2(policy.contains(QStringLiteral("state.displayOrderedAt.isEmpty() && !importedInvoiceRow")),
             "Manual rows may default blank ordered_at, but imported invoice rows must not.");
    QVERIFY2(policy.contains(QStringLiteral("state.storedOrderedAt = (importedInvoiceRow && state.displayOrderedAt.isEmpty())")),
             "Imported invoice rows must preserve blank ordered_at as an empty stored value.");
    QVERIFY2(cs::tests::importedDatesRemainIndependent(policy),
             "invoice_date metadata must remain separated from ordered_at and delivery_date.");
    QVERIFY2(!cs::tests::importedDatesRemainIndependent(policy + "\nstate.deliveryDate = invoiceDate;"),
             "invoice_date must never be silently copied into delivery_date.");
    QVERIFY2(persistence.contains(QStringLiteral("payloadInput.preserveEmptyOrderedAt = importedInvoiceRow")),
             "Imported rows must pass preserveEmptyOrderedAt into the payload builder.");
    QVERIFY2(persistence.contains(QStringLiteral("payloadInput.preserveEmptyDeliveryDate = importedInvoiceRow")),
             "Imported rows must pass preserveEmptyDeliveryDate into the payload builder.");
    QVERIFY2(payload.contains(QStringLiteral("preserveEmptyOrInvalid")),
             "The payload date normalizer must expose the preserve-empty policy to unit guards.");
}

void P2AcceptanceUnitTests::partRowPayloadBuilderUnitGuard()
{
    const QString header = cs::tests::readSourceFile("src/core/services/orders/PartRowPayloadBuilderService.h");
    const QString impl = cs::tests::readSourceFile("src/core/services/orders/PartRowPayloadBuilderService.cpp");

    QVERIFY2(header.contains(QStringLiteral("struct PartRowPayloadInput")),
             "PartRowPayloadBuilderService must keep an explicit input DTO.");
    QVERIFY2(header.contains(QStringLiteral("preserveEmptyOrderedAt"))
                 && header.contains(QStringLiteral("preserveEmptyDeliveryDate")),
             "PartRowPayloadBuilderService must preserve imported blank date policy.");
    QVERIFY2(header.contains(QStringLiteral("static WorkOrderPartRow build")),
             "PartRowPayloadBuilderService must expose one canonical build method.");
    QVERIFY2(impl.contains(QStringLiteral("normalizeToIsoDate(input.orderedAt"))
                 && impl.contains(QStringLiteral("normalizeToIsoDate(deliverySource")),
             "ordered_at and delivery_date must be normalized through the canonical helper.");
    QVERIFY2(impl.contains(QStringLiteral("PartPricingEngine::calculate"))
                 && header.contains(QStringLiteral("manualMarkupOverride"))
                 && header.contains(QStringLiteral("purchasePrice")),
             "PartRowPayloadBuilderService unit guard must route pricing through PartPricingEngine with markup override metadata.");
    QVERIFY2(impl.contains(QStringLiteral("row.serviceId = input.serviceId")),
             "Part payload must preserve service linkage for save/update paths.");
}

void P2AcceptanceUnitTests::importParserUnitGuard()
{
    const QString elit = cs::tests::readSourceFile("src/core/import/parsers/ElitParser.cpp");
    const QString avtonova = cs::tests::readSourceFile("src/core/import/parsers/AvtonovaParser.cpp");
    const QString xls = cs::tests::readSourceFile("src/core/import/parsers/GenericXlsParser.cpp");
    const QString pdf = cs::tests::readSourceFile("src/core/import/parsers/GenericPdfParser.cpp");
    const QString engine = cs::tests::invoiceEngineImplementation();

    QVERIFY2(elit.contains(QStringLiteral("ElitParser::parsePdfText"))
                 && elit.contains(QStringLiteral("parseElitePdfTextInternal")),
             "Elit parser must remain vendor-specific and independently unit-guarded.");
    QVERIFY2(avtonova.contains(QStringLiteral("AvtonovaParser::parsePdfText"))
                 && avtonova.contains(QStringLiteral("parseAvtonovaPdfTextInternal")),
             "Avtonova parser must remain vendor-specific and independently unit-guarded.");
    QVERIFY2(xls.contains(QStringLiteral("GenericXlsParser::readRows"))
                 && xls.contains(QStringLiteral("readSpreadsheetInvoiceRowsInternal")),
             "Generic XLS parser must keep the non-UI spreadsheet import boundary.");
    QVERIFY2(pdf.contains(QStringLiteral("GenericPdfParser::readRows"))
                 && pdf.contains(QStringLiteral("readPdfInvoiceRowsInternal")),
             "Generic PDF parser must keep the non-Excel PDF import boundary.");
    QVERIFY2(pdf.contains(QStringLiteral("hasEliteMarker"))
                 && pdf.contains(QStringLiteral("hasAvtonovaMarker")),
             "Generic PDF parser must route vendor-specific text through explicit marker logic.");
    QVERIFY2(engine.contains(QStringLiteral("pdf_direct_python"))
                 || engine.contains(QStringLiteral("pdf_powershell_fallback"))
                 || engine.contains(QStringLiteral("readPdfInvoiceRowsInternal")),
             "Invoice import unit guard must preserve a PDF path that does not depend on installed Excel.");
}

void P2AcceptanceUnitTests::workPartsDirtyStateUnitGuard()
{
    const QString header = cs::tests::readSourceFile("src/ui/tabs/orders/WorkPartsDialog.h");
    const QString persistence = cs::tests::readSourceFile("src/ui/tabs/orders/WorkPartsPersistenceCoordinator.cpp");
    const QString importFlow = cs::tests::readSourceFile("src/ui/tabs/orders/WorkPartsImportCoordinator.cpp");
    const QString diagnostics = cs::tests::readSourceFile("src/ui/tabs/orders/WorkPartsDiagnosticsSupport.cpp");

    QVERIFY2(header.contains(QStringLiteral("QSet<int> m_dirtyServiceRows"))
                 && header.contains(QStringLiteral("QSet<int> m_dirtyPartRows")),
             "WorkParts dirty-state must stay explicit and independently testable.");
    QVERIFY2(persistence.contains(QStringLiteral("m_dirtyServiceRows.insert"))
                 && persistence.contains(QStringLiteral("m_dirtyPartRows.insert")),
             "Attachment/save failures must re-mark dirty rows rather than hiding errors.");
    QVERIFY2(persistence.contains(QStringLiteral("m_dirtyServiceRows.remove"))
                 && persistence.contains(QStringLiteral("m_dirtyPartRows.remove")),
             "Dirty-state cleanup must happen only after successful deferred attachment save.");
    QVERIFY2(importFlow.contains(QStringLiteral("dirty-state"))
                 || importFlow.contains(QStringLiteral("m_dirtyPartRows"))
                 || importFlow.contains(QStringLiteral("dirtyPartRows")),
             "Import/edit flows must remain wired to WorkParts dirty-state.");
    QVERIFY2(diagnostics.contains(QStringLiteral("dirtyServiceRows"))
                 && diagnostics.contains(QStringLiteral("dirtyPartRows")),
             "Dirty-state changes must be visible in structured diagnostics.");
}

void P2AcceptanceUnitTests::attachmentServiceUnitGuard()
{
    const QString header = cs::tests::readSourceFile("src/core/services/orders/OrderAttachmentService.h");
    const QString impl = cs::tests::readSourceFile("src/core/services/orders/OrderAttachmentService.cpp");
    const QString persistence = cs::tests::readSourceFile("src/ui/tabs/orders/WorkPartsPersistenceCoordinator.cpp");

    QVERIFY2(header.contains(QStringLiteral("bool setServiceAttachment"))
                 && header.contains(QStringLiteral("bool setPartAttachment")),
             "AttachmentService APIs must return bool so callers can keep dirty-state on failure.");
    QVERIFY2(header.contains(QStringLiteral("QString* errorText")),
             "AttachmentService must expose structured error propagation to callers.");
    QVERIFY2(impl.contains(QStringLiteral("if (errorText) *errorText"))
                 && impl.contains(QStringLiteral("return false")),
             "AttachmentService implementation must propagate failures rather than reporting success.");
    QVERIFY2(impl.contains(QStringLiteral("removeUnreferencedManagedFile"))
                 && impl.contains(QStringLiteral("TransactionContext::deferUntilCommit(db,"))
                 && impl.contains(QStringLiteral("publishAttachmentBackupEvent")),
             "AttachmentService unit guard must cover managed cleanup and backup event publication.");
    QVERIFY2(persistence.contains(QStringLiteral("attachment_save_fail"))
                 && persistence.contains(QStringLiteral("dirty_cleared_after_deferred_attachment")),
             "WorkParts persistence must keep attachment staging success/failure observable.");
}

void P2AcceptanceUnitTests::updateHashAndPathLogicUnitGuard()
{
    const QString supportH = cs::tests::readSourceFile("src/app/update/UpdateServiceSupport.h");
    const QString supportCpp = cs::tests::updateSupportImplementation();
    const QString updateCpp = cs::tests::readSourceFile("src/app/update/UpdateService.cpp");

    QVERIFY2(supportH.contains(QStringLiteral("computeFileSha256")),
             "UpdateService hash helper must remain publicly visible to unit/source tests.");
    const int start = supportCpp.indexOf(QStringLiteral("QString computeFileSha256"));
    QVERIFY2(start >= 0, "computeFileSha256 implementation must exist.");
    int end = supportCpp.indexOf(QStringLiteral("UpdateService::ReleaseInfo parseReleaseInfo"), start);
    if (end < 0) end = supportCpp.size();
    const QString hashBlock = supportCpp.mid(start, end - start);
    QVERIFY2(hashBlock.contains(QStringLiteral("QCryptographicHash hash(QCryptographicHash::Sha256)")),
             "SHA256 helper must use QCryptographicHash.");
    QVERIFY2(hashBlock.contains(QStringLiteral("hash.addData(&f)")),
             "SHA256 helper must stream file contents through addData(&QFile).");
    QVERIFY2(!hashBlock.contains(QStringLiteral("readAll()")),
             "SHA256 helper must not read the whole package into memory.");
    QVERIFY2(supportCpp.contains(QStringLiteral("looksLikeBlockedExecutableAsset"))
                 && supportCpp.contains(QStringLiteral("vc_redist"))
                 && supportCpp.contains(QStringLiteral("unins"))
                 && supportCpp.contains(QStringLiteral("maintenancetool"))
                 && supportCpp.contains(QStringLiteral("helper"))
                 && supportCpp.contains(QStringLiteral("debug")),
             "UpdateService unit guard must block non-installer EXE assets from GitHub Release selection.");
    QVERIFY2(supportCpp.contains(QStringLiteral("looksLikeInstallerAsset"))
                 && supportCpp.contains(QStringLiteral("carservicepointg_"))
                 && supportCpp.contains(QStringLiteral("CarServicePointG_Setup_x86_x64"))
                 && supportCpp.contains(QStringLiteral("chooseChecksumAssetName"))
                 && supportCpp.contains(QStringLiteral("sha256sums.txt"))
                 && supportCpp.contains(QStringLiteral("packageSpecificShaFile")),
             "UpdateService unit guard must accept canonical setup-style installer assets and package-specific/SHA256SUMS checksum formats.");
    QVERIFY2(supportCpp.contains(QStringLiteral("Start-ElevatedSelfIfNeeded"))
                 && supportCpp.contains(QStringLiteral("Verb RunAs"))
                 && supportCpp.contains(QStringLiteral("-WindowStyle"))
                 && supportCpp.contains(QStringLiteral("Hidden"))
                 && supportCpp.contains(QStringLiteral("/VERYSILENT"))
                 && supportCpp.contains(QStringLiteral("/SUPPRESSMSGBOXES"))
                 && supportCpp.contains(QStringLiteral("/NORESTART")),
             "UpdateService script-generation guard must launch elevated/hidden and pass silent installer arguments.");
    QVERIFY2(supportCpp.contains(QStringLiteral("OLD_VERSION_SCAN"))
                 && supportCpp.contains(QStringLiteral("OLD_VERSION_CLEANUP"))
                 && supportCpp.contains(QStringLiteral("SHORTCUT_REFRESH"))
                 && supportCpp.contains(QStringLiteral("START_UPDATED_APP"))
                 && supportCpp.contains(QStringLiteral("VERIFY_SIGNATURE")),
             "UpdateService script-generation guard must keep old-install cleanup, shortcut refresh, signature verification and updated-app launch stages.");
    QVERIFY2(supportCpp.contains(QStringLiteral("manifest_download_failed"))
                 && supportCpp.contains(QStringLiteral("no_valid_installer_asset"))
                 && supportCpp.contains(QStringLiteral("sha256_missing"))
                 && supportCpp.contains(QStringLiteral("sha256_mismatch"))
                 && supportCpp.contains(QStringLiteral("signature_invalid"))
                 && supportCpp.contains(QStringLiteral("uac_cancelled_or_elevation_denied"))
                 && supportCpp.contains(QStringLiteral("updated_app_start_failed")),
             "UpdateService must keep stable machine-searchable update failure reasons for reporter and logs.");
    QVERIFY2(supportCpp.contains(QStringLiteral("Resolve-InstallLocationFromEntry"))
                 && supportCpp.contains(QStringLiteral("Write-OldInstallFileList"))
                 && supportCpp.contains(QStringLiteral("old_install_files_"))
                 && supportCpp.contains(QStringLiteral("CarService PointG 2.1.1.lnk")),
             "Old-install cleanup must resolve safe install locations, log file lists and refresh canonical shortcut names.");
    QVERIFY2(updateCpp.contains(QStringLiteral("Expand-Archive -LiteralPath"))
                 && updateCpp.contains(QStringLiteral("restart_old"))
                 && updateCpp.contains(QStringLiteral("backup")),
             "UpdateService unit guard must keep path-safe extraction and atomic backup/restart_old rollback markers.");
}

QTEST_MAIN(P2AcceptanceUnitTests)
#include "P2AcceptanceUnitTests.moc"
