#include <QtTest/QtTest>
#include "../TestSourceAssertions.h"

class RegressionWorkPartsP0ImportedPartsTests final : public QObject {
    Q_OBJECT
private slots:
    void importedPartDateEditPreservesBlankOrderedDateAndSeparatesInvoiceDate();
    void importedPartNameBrandAndPriceEditUseCanonicalSavePayload();
    void fastEnterTabArrowNavigationFlushesEditorBeforeMoving();
    void importEditSaveReopenPathKeepsImportedPartStateAndDirtyRowsSafe();
    void attachmentFailureKeepsDirtyRowsAndPropagatesFalse();
    void allWorkPartsMutationsUseCanonicalTransactionBoundary();
};

void RegressionWorkPartsP0ImportedPartsTests::importedPartDateEditPreservesBlankOrderedDateAndSeparatesInvoiceDate()
{
    const QString policy = cs::tests::partEditServicesImplementation();

    QVERIFY2(policy.contains(QStringLiteral("state.displayOrderedAt.isEmpty() && !importedInvoiceRow")),
             "Imported invoice rows must not receive the current order date when ordered_at is blank.");
    QVERIFY2(policy.contains(QStringLiteral("state.storedOrderedAt = (importedInvoiceRow && state.displayOrderedAt.isEmpty())")),
             "Blank ordered_at from imported rows must be preserved as an empty stored value.");
    QVERIFY2(cs::tests::importedDatesRemainIndependent(policy),
             "Invoice date metadata must stay separated from delivery_date/ordered_at.");
    QVERIFY2(!cs::tests::importedDatesRemainIndependent(policy + "\nstate.deliveryDate = invoiceDate;"),
             "Regression guard: invoice_date must never be copied into delivery_date for imported invoice rows.");

    cs::tests::requireContains("src/ui/tabs/orders/WorkPartsPartPersistence.cpp", "payloadInput.preserveEmptyOrderedAt = importedInvoiceRow");
    cs::tests::requireContains("src/ui/tabs/orders/WorkPartsPartPersistence.cpp", "payloadInput.preserveEmptyDeliveryDate = importedInvoiceRow");
}

void RegressionWorkPartsP0ImportedPartsTests::importedPartNameBrandAndPriceEditUseCanonicalSavePayload()
{
    const QString editServices = cs::tests::partEditServicesImplementation();

    QVERIFY2(editServices.contains(QStringLiteral("state.name = normalizePartSentence")),
             "Part name edits must pass through the canonical normalizer before save.");
    QVERIFY2(editServices.contains(QStringLiteral("state.manufacturer = partManufacturerText")),
             "Imported/editable brand/manufacturer text must be read through the canonical mapper.");
    QVERIFY2(editServices.contains(QStringLiteral("state.price = PartPricePolicyService::resolveForSave")),
             "Edited imported prices must use the canonical price policy before persistence.");
    QVERIFY2(editServices.contains(QStringLiteral("RolePurchasePrice"))
                 && cs::tests::readSourceFile("src/ui/tabs/orders/WorkPartsPartPersistence.cpp").contains(QStringLiteral("payloadInput.manualMarkupOverride = priceItem->data(RoleManualMarkupOverride)")),
             "Regression guard: purchase price and manual override roles must remain separated to prevent duplicate recalculation.");

    const QString persistence = cs::tests::readSourceFile("src/ui/tabs/orders/WorkPartsPartPersistence.cpp");
    QVERIFY2(persistence.contains(QStringLiteral("partState.name")),
             "Persistence must save the canonical part name from WorkPartsPartEditState.");
    QVERIFY2(persistence.contains(QStringLiteral("partState.manufacturer")),
             "Persistence must save the canonical manufacturer/brand from WorkPartsPartEditState.");
    QVERIFY2(persistence.contains(QStringLiteral("partState.price.purchasePrice")),
             "Persistence must save the policy-resolved purchase price for imported rows.");
}

void RegressionWorkPartsP0ImportedPartsTests::fastEnterTabArrowNavigationFlushesEditorBeforeMoving()
{
    const QString editor = cs::tests::inlineEditorImplementation();

    QVERIFY2(editor.contains(QStringLiteral("flushInlineEditorValueToModel")),
             "Inline editor flush must remain wired before keyboard navigation changes cells.");
    QVERIFY2(editor.contains(QStringLiteral("Qt::Key_Return")) || editor.contains(QStringLiteral("Qt::Key_Enter")),
             "Enter navigation must stay covered by the table keyboard navigator.");
    QVERIFY2(editor.contains(QStringLiteral("Qt::Key_Tab")),
             "Tab navigation must stay covered by the table keyboard navigator.");
    QVERIFY2(editor.contains(QStringLiteral("Qt::Key_Down"))
                 && editor.contains(QStringLiteral("Qt::Key_Up"))
                 && editor.contains(QStringLiteral("Qt::Key_Left"))
                 && editor.contains(QStringLiteral("Qt::Key_Right")),
             "Arrow-key navigation must stay covered by the table keyboard navigator.");
    QVERIFY2(editor.contains(QStringLiteral("inlineEditorActive"))
                 && editor.contains(QStringLiteral("inlineEditingCell(table)"))
                 && editor.contains(QStringLiteral("clearInlineEditingIndex(table)")),
             "Regression guard: inline editor state must remain explicit until the typed controller fully replaces it.");
}

void RegressionWorkPartsP0ImportedPartsTests::importEditSaveReopenPathKeepsImportedPartStateAndDirtyRowsSafe()
{
    const QString importCoordinator = cs::tests::readSourceFile("src/ui/tabs/orders/WorkPartsImportCoordinator.cpp");
    const QString persistence = cs::tests::readSourceFile("src/ui/tabs/orders/WorkPartsPartPersistence.cpp");
    const QString dialogSupport = cs::tests::workPartsDialogSupportImplementation();

    QVERIFY2(importCoordinator.contains(QStringLiteral("RoleImportedInvoiceRow"))
                 || dialogSupport.contains(QStringLiteral("RoleImportedInvoiceRow")),
             "Imported invoice row marker must be written and readable after import.");
    QVERIFY2(dialogSupport.contains(QStringLiteral("partRowHasImportedInvoiceMarker")),
             "Imported row detection must remain centralized for save/reopen flows.");
    QVERIFY2(dialogSupport.contains(QStringLiteral("importedPartTraceId")),
             "Imported rows must keep a trace id for crash diagnostics across edit/save/reopen.");
    QVERIFY2(persistence.contains(QStringLiteral("importedInvoiceRow")),
             "Persistence must keep imported-row state in the save path.");
    QVERIFY2(persistence.contains(QStringLiteral("m_dirtyPartRows"))
                 && persistence.contains(QStringLiteral("remove(row)")),
             "Dirty imported rows must be cleared only after the save path reaches its success boundary.");

    cs::tests::requireAnyContains("src/ui/tabs/orders/WorkPartsPersistenceCoordinator.cpp",
                                  {"commitPendingEdits", "commitPendingRowsInSingleTransaction"});
}

void RegressionWorkPartsP0ImportedPartsTests::attachmentFailureKeepsDirtyRowsAndPropagatesFalse()
{
    const QString partPersistence = cs::tests::readSourceFile("src/ui/tabs/orders/WorkPartsPartPersistence.cpp");
    const QString servicePersistence = cs::tests::readSourceFile("src/ui/tabs/orders/WorkPartsServicePersistence.cpp");
    const QString coordinator = cs::tests::readSourceFile("src/ui/tabs/orders/WorkPartsPersistenceCoordinator.cpp");

    QVERIFY2(partPersistence.contains(QStringLiteral("attachment_save_failed_dirty_kept")),
             "Part attachment failure must be visible and must keep the part row dirty.");
    QVERIFY2(partPersistence.contains(QStringLiteral("part_dirty_kept_until_batch_commit")),
             "Part rows saved inside an external transaction must not clear dirty-state before deferred attachment processing succeeds.");
    QVERIFY2(servicePersistence.contains(QStringLiteral("service_dirty_kept_until_batch_commit")),
             "Service rows saved inside an external transaction must not clear dirty-state before deferred attachment processing succeeds.");
    QVERIFY2(partPersistence.contains(QStringLiteral("removeAttachmentFileIfManaged(storedPath)"))
                 && servicePersistence.contains(QStringLiteral("removeAttachmentFileIfManaged(storedPath)")),
             "If metadata save fails after file copy, the copied attachment must be rolled back.");
    QVERIFY2(coordinator.contains(QStringLiteral("attachment_save_failed_dirty_kept")),
             "Deferred attachment failures must be logged as dirty-kept failures, not hidden success.");
    QVERIFY2(coordinator.contains(QStringLiteral("dirty_cleared_after_deferred_attachment"))
                 && coordinator.contains(QStringLiteral("dirty_cleared_after_full_commit")),
             "Dirty-state may only clear after deferred attachment and full batch commit success boundaries.");
    QVERIFY2(coordinator.contains(QStringLiteral("if (!processDeferredAttachmentOperations(&deferredAttachmentError))"))
                 && coordinator.contains(QStringLiteral("return false;")),
             "Batch commit must return false when deferred attachment save fails.");
}

void RegressionWorkPartsP0ImportedPartsTests::allWorkPartsMutationsUseCanonicalTransactionBoundary()
{
    const QString partPersistence = cs::tests::readSourceFile("src/ui/tabs/orders/WorkPartsPartPersistence.cpp");
    const QString servicePersistence = cs::tests::readSourceFile("src/ui/tabs/orders/WorkPartsServicePersistence.cpp");
    const QString invoiceImport = cs::tests::readSourceFile("src/core/services/orders/CurrentOrdersInvoiceImportService.cpp");
    const QString mutationService = cs::tests::readSourceFile("src/core/services/orders/WorkPartsMutationService.cpp");

    QVERIFY2(!partPersistence.contains(QStringLiteral("WorkPartsMutationService::withDatabase")),
             "Part save UI path must not open a parallel Database::db()/withDatabase mutation path.");
    QVERIFY2(!servicePersistence.contains(QStringLiteral("WorkPartsMutationService::withDatabase")),
             "Service save UI path must not open a parallel Database::db()/withDatabase mutation path.");
    QVERIFY2(!partPersistence.contains(QStringLiteral("MutationExecutor::execute")),
             "Part save UI path must use WorkPartsMutationService::execute as the canonical transaction boundary.");
    QVERIFY2(!servicePersistence.contains(QStringLiteral("MutationExecutor::execute")),
             "Service save UI path must use WorkPartsMutationService::execute as the canonical transaction boundary.");

    QVERIFY2(partPersistence.contains(QStringLiteral("WorkPartsMutationService::execute(request")),
             "Part save path must enter the canonical WorkPartsMutationService transaction boundary.");
    QVERIFY2(servicePersistence.contains(QStringLiteral("WorkPartsMutationService::execute(request")),
             "Service save path must enter the canonical WorkPartsMutationService transaction boundary.");
    QVERIFY2(servicePersistence.contains(QStringLiteral("runInActiveTransaction(connectionName"))
                 && partPersistence.contains(QStringLiteral("runInActiveTransaction(connectionName")),
             "Batch save substeps must reuse the active transaction connection instead of opening a second transaction.");

    QVERIFY2(invoiceImport.contains(QStringLiteral("WorkPartsMutationService::execute(request")),
             "Invoice import must use the WorkPartsMutationService transaction boundary.");
    QVERIFY2(!invoiceImport.contains(QStringLiteral("MutationExecutor::execute(db")),
             "Invoice import must not bypass WorkPartsMutationService with its own MutationExecutor call.");
    QVERIFY2(invoiceImport.contains(QStringLiteral("OrderAttachmentService::setPartAttachment")),
             "Invoice attachment metadata after commit must be persisted through the canonical mutation service.");

    QVERIFY2(mutationService.contains(QStringLiteral("request.operation = QStringLiteral(\"set_part_attachment\")"))
                 && mutationService.contains(QStringLiteral("request.operation = QStringLiteral(\"set_service_attachment\")"))
                 && mutationService.contains(QStringLiteral("request.operation = QStringLiteral(\"remove_part\")")),
             "WorkPartsMutationService helper mutations must be wrapped in explicit MutationExecutor requests.");
    QVERIFY2(!mutationService.contains(QStringLiteral("OrderAttachmentService::setPartAttachment(Database::db()"))
                 && !mutationService.contains(QStringLiteral("PartRepository::remove(Database::db()")),
             "WorkPartsMutationService helpers must not mutate through raw Database::db() calls.");
}

QTEST_MAIN(RegressionWorkPartsP0ImportedPartsTests)
#include "RegressionWorkPartsP0ImportedPartsTests.moc"
