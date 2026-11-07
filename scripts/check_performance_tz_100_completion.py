#!/usr/bin/env python3
from __future__ import annotations
import sys
from pathlib import Path

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]

def read(rel: str) -> str:
    path = ROOT / rel
    if not path.exists():
        raise AssertionError(f"missing {rel}")
    return path.read_text(encoding='utf-8', errors='ignore')

checks = [
    ("src/runtime/RuntimeJobDispatcher.h", ["cancelAll", "enterShutdown", "isShutdownRequested", "Import,", "Backup,", "Update,"]),
    ("src/runtime/AsyncThreadScope.h", ["worker_cancelled_shutdown_requested", "RuntimeJobDispatcher::instance().isShutdownRequested()"]),
    ("src/app/AppContext.cpp", ["enterShutdown(QStringLiteral(\"AppContext::shutdownRuntimeServices\"))", "leaveShutdown()"]),
    ("src/core/import/ImportJob.cpp", ["RuntimeJob::Family::Import", "RuntimeJobDispatcher::instance().markRunning", "RuntimeJobDispatcher::instance().finish"]),
    ("src/core/backup/BackupManager.cpp", ["BackupManager::reconcile", "backup_manifest_reconcile_worker", "RuntimeJob::Family::Backup"]),
    ("src/app/update/internal/UpdateInstallerRunner.ipp", ["RuntimeJob::Family::Update", "m_runtimeTicket", "markRunning(m_runtimeTicket)"]),
    ("src/app/update/internal/UpdateVersionDetector.ipp", ["isShutdownRequested()", "update skipped: application shutdown"]),
    ("src/ui/tabs/parts/PartsPageRuntime.cpp", ['AsyncThreadScope::startWorker(this,"parts_debt_banner",cancel', "SupplierDebtReadService::prepareBanner()", "readTask(*cancel,&result->error)", "m_rowsSnapshot", "m_partsModel->rowByPartId(partId)", "firstVisibleRowForOrderUid", "scheduleDebtBannerUpdate", "parts_page_refresh_timeout_cancel", "runtimeTicket.timeoutMs > 0"]),
    ("src/core/services/SupplierDebtReadService.cpp", ["PurchasePaymentService::debts(db", "SupplierInvoiceLedger::summary", "QSQLITE_OPEN_READONLY", "db.transaction()", "db.rollback()", "QSqlDatabase::removeDatabase(name)"]),
    ("src/ui/tabs/orders/CurrentOrdersPageActions.cpp", ["current_orders_refresh_timeout_cancel", "runtimeTicket.timeoutMs > 0", "workerGuard->requestInterruption()"]),
    ("src/ui/tabs/history/HistoryPage.cpp", ["history_refresh_timeout_cancel", "runtimeTicket.timeoutMs > 0", "workerGuard->requestInterruption()"]),
    ("src/ui/common/TabularSnapshotTableModel.cpp", ["beginResetModel", "endResetModel"]),
    ("tests/regression/RegressionRuntimeJobDispatcherShutdownTests.cpp", ["sameFamilyNewJobCancelsPrevious", "shutdownCancelsAllFamiliesAndRejectsApply"]),
    ("tests/regression/RegressionRuntimeJobTimeoutGuardsTests.cpp", ["longRefreshFamiliesHaveTimeoutCancellationGuards", "dispatcherRejectsApplyDuringShutdownEvenBeforeTokenObserved"]),
    ("tests/regression/RegressionPerformanceTzFinalGapGuardsTests.cpp", ["partsPageUsesPreparedVisibleIndexesForSelectionAndPaidPatch", "importProcessOutputIsBoundedAndLogsTruncation", "reportsSnapshotApplyUsesModelResetNotWidgetItems"]),
    ("src/core/import/internal/PdfTextExtractionStage.ipp", ["kMaxImportProcessChannelBytes", "appendBoundedImportProcessBytes", "stdoutTruncated", "stderrTruncated"]),
    ("src/ui/tabs/reports/ReportsTableSupport.h", ["ReportsSnapshotTableModel final : public QAbstractTableModel", "setSnapshot", "setMessageRow"]),
    ("src/ui/tabs/reports/ReportsTableSupport.cpp", ["beginResetModel()", "endResetModel()", "clearSpans", "QHeaderView::Fixed", "setSnapshot", "rowSpansAllColumns"]),
    ("src/ui/tabs/reports/ReportsPageBuildUi.cpp", ["table = new QTableView(page)", "table->setModel(model)", "QTableView::doubleClicked"]),
    ("tests/regression/RegressionPerformanceTzFinal100ClosureTests.cpp", ["partsPageSelectionUsesSnapshotNotWidgetItems", "partsPageVisibleHeightUsesSnapshotText", "reportsTablesUseQTableViewSnapshotModels"]),
    ("src/ui/tabs/parts/PartsPageTableModelAdapter.h", ["PartsArchiveTableModel final : public QAbstractTableModel", "setRows", "replaceRow"]),
    ("src/ui/tabs/parts/PartsPageTableModelAdapter.cpp", ["PartsArchiveTableModel::setRows", "beginResetModel();", "endResetModel();", "Qt::BackgroundRole", "case ColPaid:", "PartMarkupPolicyService::isCustomerSupplied(row.storeDisplay)", "row.partiallyPaid", "paidDisplayText(row.paid)"]),
    ("src/ui/tabs/parts/PartsPageBuildUi.cpp", ["m_table = new QTableView(this)", "m_partsModel = new PartsPageTableModelAdapter::PartsArchiveTableModel", "m_table->setModel(m_partsModel)", "QTableView::clicked", "QTableView::doubleClicked"]),
    ("tests/regression/RegressionPerformanceTzPartsArchiveModelClosureTests.cpp", ["partsArchiveUsesQTableViewAndAbstractModel", "partsArchivePopulationDoesNotAllocateWidgetItems", "partsArchiveOverduePaidRolesStayModelDriven"]),

    ("src/core/cache/PartsQueryCache.cpp", ["std::atomic<quint64> g_cacheRevision", "parts|rows|rev=%1|db=%2|filter=%3|from=%4|to=%5", "bumpRevision(reason.trimmed().isEmpty() ? QStringLiteral(\"parts\") : reason)"]),
    ("src/core/cache/ReportsSnapshotCache.cpp", ["std::atomic<quint64> g_cacheRevision", "reports|rev=%1|family=%2|db=%3|from=%4|to=%5|scope=%6", "stats|rev=%1|db=%2|from=%3|to=%4", "bumpRevision(QStringLiteral(\"reports\"))"]),
    ("tests/regression/RegressionPerformanceTzCacheRevisionTests.cpp", ["partsCacheKeysIncludeRevisionAndFilters", "reportsAndStatsCacheKeysIncludeRevisionAndScope", "mutationInvalidatorStillRoutesThroughCentralPoint"]),
    ("tests/regression/RegressionPerformanceTzPass4WorkPartsAsyncReadTests.cpp", ["workPartsRefreshReadsDataInWorkerAndAppliesSnapshotOnUiThread", "legacySyncFallbacksAreExplicitlyGuarded", "scopedSqliteConnectionsCannotCollideWithinSameMillisecond"]),
    ("src/ui/tabs/orders/WorkPartsDialog.cpp", ["AsyncThreadScope::startWorker(this,", "WorkPartsReadQueryService::listServicesByInternalNumber(orderNumber", "QMetaObject::invokeMethod(self.data()", "workparts_dialog_refresh_timeout_cancel"]),
    ("src/infrastructure/db/sqlite/ScopedConnectionManager.cpp", ["g_scopedConnectionSequence", "QThread::currentThread()", "scoped_%1_thread_%2_seq_%3_ts_%4"]),
    ("src/ui/tabs/reports/ReportsDialogSupport.cpp", ["auto* table = new QTableView(&dlg)", "ReportsSnapshotTableModel(table)", "model->setSnapshot(payrollOrdersHeaders(), buildPayrollOrdersRows(orders))", "QTableView::doubleClicked", "model->cellUserValue(orderRow, 2)"]),
    ("tests/regression/RegressionPerformanceTzPass5ReportsDialogModelTests.cpp", ["payrollOrdersDialogUsesSnapshotModelNotTableWidget", "payrollOrdersDialogDoesNotAllocateWidgetItemsInLoop"]),
    ("src/core/cache/OrderSnapshotCache.cpp", ["std::atomic<quint64> g_cacheRevision", "orders|current|rev=%1|db=%2|filter=%3", "orders|history|rev=%1|db=%2|from=%3|to=%4|filter=%5", "ORDER_SNAPSHOT_CACHE_REVISION_BUMP"]),
    ("src/core/cache/SnapshotCacheInvalidator.cpp", ["OrderSnapshotCache::invalidateAll(reason)", "families=current_orders,history,orders,parts"]),
    ("src/core/services/orders/CurrentOrdersRefreshJob.cpp", ["OrderSnapshotCache::tryGetCurrentOrders(cacheKey, &cached)", "OrderSnapshotCache::storeCurrentOrders(cacheKey, {result.queryResult, result.readModel})"]),
    ("src/core/services/HistoryRefreshJob.cpp", ["OrderSnapshotCache::tryGetHistory(cacheKey, &cached)", "OrderSnapshotCache::storeHistory(cacheKey, {result.queryResult, result.readModel})"]),
    ("src/runtime/RuntimeJobDispatcher.h", ["QString cacheKey;", "const QString& cacheKey = QString()"]),
    ("src/runtime/RuntimeJobDispatcher.cpp", ["ticket.cacheKey = cacheKey.trimmed().left(512)", "cacheKey="]),
    ("tests/regression/RegressionPerformanceTzPass6OrderSnapshotCacheTests.cpp", ["currentAndHistoryRefreshJobsUseCentralOrderSnapshotCache", "mutationInvalidatorClearsOrdersHistoryPartsAndReportsTogether", "runtimeJobTelemetryCarriesCacheKey"]),
    ("tests/regression/RegressionPerformanceTzPass7FreezeLogDbClosureTests.cpp", ["workPartsSyncFallbacksRouteToWorkerExceptDraftRestore", "productionInfoSpamRequiresDeepDebug", "dbCloseUnregistersThreadConnectionAfterSafeRemove"]),
    ("tests/regression/RegressionPerformanceTzPass8ImportRowHeightReportsTests.cpp", ["importBatchAppliesRowsWithoutDuplicateMarkerChurn", "rowHeightAndCompactFontsAreVisibleRangeCached", "reportsSnapshotMigrationUsesIndexedSpanRowsOnly"]),
    ("tests/regression/RegressionPerformanceTzPass9SnapshotCacheGatesTests.cpp", ["currentHistoryPartsUseSnapshotModelsOnly", "partsArchiveModelOwnsVisibleRowIndexes", "cacheKeysNormalizeDbIdentityAndCentralInvalidationIsScoped", "testAndGateInventoryCoversPass9Closure"]),
    ("src/ui/common/dialogs/ReceivedScansDialog.cpp", ["ReceivedScansTableModel final : public QAbstractTableModel", "m_table = new QTableView(card)", "model->setRows(std::move(rows))", "cellText(rowIndex, kColOrder)", "RuntimeLoggingControl::deepDebugEnabled()"]),
    ("src/core/cache/SnapshotCacheInvalidator.cpp", ["P1/pass10 cache/invalidation closure", "touchesOrderHeaderOrServices", "touchesPartsArchive", "Service/header/payment edits do not clear the parts archive cache"]),
    ("tests/regression/RegressionPerformanceTzPass10FinalClosureTests.cpp", ["receivedScansPreviewUsesSnapshotModelNotTableWidget", "mutationInvalidationAvoidsUnnecessaryPartsCacheClear", "finalGateCoversPass10Closure"]),
    ("tests/regression/RegressionPerformanceTzPass13ProductionLogClosureTests.cpp", ["hotPathProductionInfoLogsRequireDeepDebug", "pass13GateCoversProductionLogClosure"]),
    ("tests/regression/RegressionPerformanceTzPass12WorkPartsRowIndexTests.cpp", ["workPartsDialogKeepsIdRowIndexesForSelectionAndDraftRestore", "pass12GateCoversLegacyEditorHotPathReduction"]),
    ("tests/regression/RegressionUnikTradeInvoiceImportTests.cpp", ["xlsxSupplierHeaderIsDetectedAsUnikTrade", "storeNormalizerHasCanonicalUnikTradeKey", "genericXlsxHeadersStillCoverUnikTradeExportColumns"]),
    ("tests/regression/RegressionHybridInvoiceColumnDetectorTests.cpp", ["detectorRequiresVatIncludedPriceForImports", "vatIncludedColumnWinsOverAmbiguousOrWithoutVatColumns", "strictVatAndNumericConfidenceClosure"]),
    ("src/core/import/internal/InvoiceImportPersistenceStage.ipp", ["evaluateInvoiceColumnConfidence", "invoiceAmountMatchesVatPrice", "SupplierInvoiceTemplateStore::read", "SupplierInvoiceTemplateStore::update", "priceWithVat", "automaticAllowed", "totalConfidence >= 85"]),
    ("src/core/io/SupplierInvoiceTemplateStore.cpp", ["supplier_invoice_templates.json", "QLockFile", "lock.tryLock(0)", "mutation(root)", "QSaveFile", "file.cancelWriting()", "file.commit()"]),
    ("src/core/import/internal/InvoiceImportDiagnostics.ipp", ["persistSupplierInvoiceTemplateIfUseful", "!invoiceAmountMatchesVatPrice(qtyValue, priceValue, amountValue)"]),

    ("tests/regression/RegressionHybridInvoiceImportManualMappingTests.cpp", ["previewExposesColumnConfidenceAndManualVatMapping", "manualConfirmationPersistsSupplierTemplate", "pdfOcrPipelineUsesSameVatAwareMappingContract"]),
    ("src/core/import/InvoiceImportTypes.h", ["struct InvoiceColumnMappingPreview", "columnConfidence", "priceWithVatConfirmed"]),
    ("src/core/import/InvoiceImportParsing.h", ["detectInvoiceColumnMappingPreview", "parseImportedPartDraftsWithConfirmedVatMapping"]),
    ("src/core/import/internal/InvoiceImportPersistenceStage.ipp", ["buildInvoiceColumnMappingPreview", "persistSupplierInvoiceTemplateFromManualConfirmation", "parseImportedPartDraftsWithConfirmedVatMapping", "Ручний імпорт заблоковано: підтвердьте колонку 'Ціна з ПДВ'"]),]

errors: list[str] = []
for rel, markers in checks:
    try:
        text = read(rel)
    except AssertionError as exc:
        errors.append(str(exc))
        continue
    for marker in markers:
        if marker not in text:
            errors.append(f"{rel}: missing marker {marker}")

cmake_tests = read("cmake/modules/CarServiceTests.cmake")
for required_test in ["RegressionRuntimeJobDispatcherShutdownTests",
                      "RegressionRuntimeJobTimeoutGuardsTests",
                      "RegressionPerformanceTzFinalGapGuardsTests",
                      "RegressionPerformanceTzFinal100ClosureTests",
                      "RegressionPerformanceTzPartsArchiveModelClosureTests",
                      "RegressionPerformanceTzCacheRevisionTests",
                      "RegressionPerformanceTzPass3CacheAndHeightTests",
                      "RegressionPerformanceTzPass4WorkPartsAsyncReadTests",
                      "RegressionPerformanceTzPass5ReportsDialogModelTests",
                      "RegressionPerformanceTzPass6OrderSnapshotCacheTests",
                      "RegressionPerformanceTzPass7FreezeLogDbClosureTests",
                      "RegressionPerformanceTzPass8ImportRowHeightReportsTests",
                      "RegressionPerformanceTzPass9SnapshotCacheGatesTests",
                      "RegressionPerformanceTzPass10FinalClosureTests",
                      "RegressionUnikTradeInvoiceImportTests",
                      "RegressionHybridInvoiceColumnDetectorTests"]:
    if required_test not in cmake_tests:
        errors.append(f"{required_test} is not registered in CMake")

# This gate protects the most important ТЗ rule: no blocking primitives in src.
for rel in [p.relative_to(ROOT).as_posix() for p in (ROOT / 'src').rglob('*') if p.suffix in {'.cpp', '.h', '.ipp', '.hpp'}]:
    text = read(rel)
    for forbidden in ["waitForFinished(", "QThread::msleep(", "processEvents(", "QEventLoop"]:
        if forbidden in text:
            errors.append(f"{rel}: forbidden blocking primitive remains: {forbidden}")

parts_runtime = read("src/ui/tabs/parts/PartsPageRuntime.cpp")
for forbidden in ["m_table->item(row, ColOrder) ? m_table->item(row, ColOrder)", "m_table->item(idx.row(), ColId)", "rowHeightForText(fm, item->text(), nameWidth, minRowHeight)"]:
    if forbidden in parts_runtime:
        errors.append(f"PartsPageRuntime.cpp: hot path still reads widget item instead of snapshot: {forbidden}")
reports_table = read("src/ui/tabs/reports/ReportsTableSupport.cpp")
for forbidden in ["insertRow(", "setItem(", "setRowCount(", "QTableWidget"]:
    if forbidden in reports_table:
        errors.append(f"ReportsTableSupport.cpp: forbidden legacy table population remains: {forbidden}")
reports_build = read("src/ui/tabs/reports/ReportsPageBuildUi.cpp")
for forbidden in ["new QTableWidget(page)", "QTableWidget::cellDoubleClicked", "setColumnCount", "setHorizontalHeaderLabels"]:
    if forbidden in reports_build:
        errors.append(f"ReportsPageBuildUi.cpp: report tab still uses legacy QTableWidget setup: {forbidden}")

reports_dialog = read("src/ui/tabs/reports/ReportsDialogSupport.cpp")
for forbidden in ["QTableWidget", "QTableWidgetItem", "setRowCount(", "setItem(", "cellDoubleClicked", "setHorizontalHeaderLabels", "setColumnCount"]:
    if forbidden in reports_dialog:
        errors.append(f"ReportsDialogSupport.cpp: payroll dialog must be QTableView/snapshot-driven; forbidden remains: {forbidden}")

parts_sources = [
    "src/ui/tabs/parts/PartsPage.h",
    "src/ui/tabs/parts/PartsPageBuildUi.cpp",
    "src/ui/tabs/parts/PartsPageRuntime.cpp",
]
for rel in parts_sources:
    text = read(rel)
    for forbidden in ["QTableWidget", "QTableWidgetItem", "setRowCount(", "clearContents(", "setItem(", "m_table->item(", "setCurrentCell(", "scrollToItem("]:
        if forbidden in text:
            errors.append(f"{rel}: Parts archive must be QTableView/QAbstractTableModel snapshot-driven; forbidden remains: {forbidden}")

# Pass8 closure gates: import batch, row-height cache and reports migration.
import_coord = read("src/ui/tabs/orders/WorkPartsImportCoordinator.cpp")
for required in ["P0 import batch optimization: mark the imported invoice row once",
                 "setRowCount(firstImportedRow + importRowCount)",
                 "csImportApplyingRows",
                 "WorkPartsDialog::applyImportedRowsBatch"]:
    if required not in import_coord:
        errors.append(f"WorkPartsImportCoordinator.cpp: pass8 import-batch marker missing: {required}")
for forbidden in ["insertEmptyPartRow(row", "markImportedInvoiceRow(guard->m_partsTable, row, vmRow.traceId);\n\n                applyImportedCellValueLikeManualInput"]:
    if forbidden in import_coord:
        errors.append(f"WorkPartsImportCoordinator.cpp: import batch still has legacy per-row churn: {forbidden}")

delegate_state = read("src/ui/tabs/orders/internal/WorkPartsEditorDelegateState.ipp")
for required in ["RoleBatchRowHeightSignature", "bufferedVisibleBatchRows", "row-height lazy sync/cache", "headerItem->data(RoleBatchRowHeightSignature)"]:
    if required not in delegate_state:
        errors.append(f"WorkPartsEditorDelegateState.ipp: pass8 batch row-height cache marker missing: {required}")

commit_guard = read("src/ui/tabs/orders/internal/WorkPartsEditorCommitGuard.ipp")
for required in ["RoleCompactFontSignature", "bufferedVisibleMainPartsRows", "fitted-font lazy cache"]:
    if required not in commit_guard:
        errors.append(f"WorkPartsEditorCommitGuard.ipp: pass8 compact-font cache marker missing: {required}")
if "for (int row = 0; row < table->rowCount(); ++row) {\n        refreshCompactMainPartsCellFontsForRow(table, row);" in commit_guard:
    errors.append("WorkPartsEditorCommitGuard.ipp: compact font refresh still scans all rows")

reports_h2 = read("src/ui/tabs/reports/ReportsTableSupport.h")
reports_cpp2 = read("src/ui/tabs/reports/ReportsTableSupport.cpp")
for required in ["const QVector<int>& spanRows() const", "QVector<int> m_spanRows"]:
    if required not in reports_h2:
        errors.append(f"ReportsTableSupport.h: reports span index marker missing: {required}")
for required in ["m_spanRows.reserve(rows.size())", "for (int r : model->spanRows())", "Large reports no longer scan every report row"]:
    if required not in reports_cpp2:
        errors.append(f"ReportsTableSupport.cpp: reports indexed-span marker missing: {required}")
for rel in [p.relative_to(ROOT).as_posix() for p in (ROOT / 'src/ui/tabs/reports').rglob('*') if p.suffix in {'.cpp', '.h', '.ipp', '.hpp'}]:
    text = read(rel)
    for forbidden in ["QTableWidget", "QTableWidgetItem", "QStandardItemModel", "insertRow(", "setItem(", "setRowCount("]:
        if forbidden in text:
            errors.append(f"{rel}: reports snapshot migration closure forbids legacy table API: {forbidden}")

# Pass9 closure gates: current/history/parts snapshots, cache invalidation and tests/gates verification.
mapper_h = read("src/ui/common/TabularReadModelMapper.h")
mapper_cpp = read("src/ui/common/TabularReadModelMapper.cpp")
if "legacy item-model mapper" not in mapper_h or "model->setSnapshot(readModel)" not in mapper_cpp:
    errors.append("TabularReadModelMapper: snapshot-only mapper closure markers missing")
for forbidden in ["#include <QStandardItem>", "#include <QStandardItemModel>", "QStandardItemModel*", "new QStandardItem"]:
    if forbidden in mapper_h or forbidden in mapper_cpp:
        errors.append(f"TabularReadModelMapper: legacy item-model API remains: {forbidden}")

parts_model_h = read("src/ui/tabs/parts/PartsPageTableModelAdapter.h")
parts_model_cpp = read("src/ui/tabs/parts/PartsPageTableModelAdapter.cpp")
for required in ["rowByPartId", "firstRowByOrderUid", "firstRowByOrderNumber", "rebuildIndexes"]:
    if required not in parts_model_h or required not in parts_model_cpp:
        errors.append(f"PartsArchiveTableModel: pass9 visible-row index marker missing: {required}")
parts_runtime2 = read("src/ui/tabs/parts/PartsPageRuntime.cpp")
for required in ["m_partsModel->rowByPartId(partId)", "m_partsModel->firstRowByOrderUid(orderUid)", "m_partsModel->firstRowByOrderNumber(orderNumber)"]:
    if required not in parts_runtime2:
        errors.append(f"PartsPageRuntime.cpp: pass9 model-owned visible row index marker missing: {required}")

history_h = read("src/ui/tabs/history/HistoryPage.h")
history_cpp = read("src/ui/tabs/history/HistoryPage.cpp")
for required in ["QSortFilterProxyModel* m_proxy", "m_proxy->mapFromSource(sourceIndex)", "source snapshot index and map through the proxy"]:
    if required not in history_h + history_cpp:
        errors.append(f"HistoryPage: pass9 source/proxy selection restore marker missing: {required}")

for rel in ["src/core/cache/PartsQueryCache.cpp", "src/core/cache/OrderSnapshotCache.cpp"]:
    text = read(rel)
    for required in ["normalizedCacheDbIdentity", "canonicalFilePath", "RuntimeLoggingControl::deepDebugEnabled()"]:
        if required not in text:
            errors.append(f"{rel}: pass9 normalized cache/deep-debug marker missing: {required}")
invalidator = read("src/core/cache/SnapshotCacheInvalidator.cpp")
for required in ["touchesReportsOrStats", "all mutation invalidation routes through this central point", "invalidateReportsAndStats(reason)"]:
    if required not in invalidator:
        errors.append(f"SnapshotCacheInvalidator.cpp: pass9 scoped invalidation marker missing: {required}")


# Pass10 closure gates: import/OCR preview no longer uses legacy table widgets, and mutation invalidation is scoped.
received_scans = read("src/ui/common/dialogs/ReceivedScansDialog.cpp")
for forbidden in ["QTableWidget", "QTableWidgetItem", "setRowCount(", "setItem(", "m_table->item("]:
    if forbidden in received_scans:
        errors.append(f"ReceivedScansDialog.cpp: import/OCR preview must be QTableView/QAbstractTableModel snapshot-driven; forbidden remains: {forbidden}")
for required in ["ReceivedScansTableModel final : public QAbstractTableModel", "m_table = new QTableView(card)", "model->setRows(std::move(rows))", "cellText(rowIndex, kColOrder)"]:
    if required not in received_scans:
        errors.append(f"ReceivedScansDialog.cpp: pass10 preview snapshot marker missing: {required}")
invalidator_pass10 = read("src/core/cache/SnapshotCacheInvalidator.cpp")
for required in ["touchesOrderHeaderOrServices", "touchesPartsArchive", "Service/header/payment edits do not clear the parts archive cache", "families=current_orders,history,orders reason="]:
    if required not in invalidator_pass10:
        errors.append(f"SnapshotCacheInvalidator.cpp: pass10 scoped invalidation marker missing: {required}")



# Pass11: both import paths must use the shared editable snapshot model.
preview_contracts = [
    ("src/ui/tabs/orders/CurrentOrdersPageLocalSupport.cpp", [
        "CurrentOrdersExcelImportDialog final : public InvoiceReviewDialog",
        "InvoiceReviewDialog(drafts, data, parent)"]),
    ("src/ui/tabs/orders/WorkPartsImportCoordinator.cpp", [
        "InvoiceReviewDialog dlg(parsedDrafts, *data, parent, QString::number(orderNumber))",
        "dlg.selectedDrafts()"]),
    ("src/ui/common/dialogs/InvoiceReviewDialog.h", [
        "new QTableView", "new InvoiceReviewTableModel", "m_table->setModel(m_model)",
        "m_model->setRows(", "m_model->setAllChecked(checked)",
        "m_model->setColumnForSelected(", "QList<Draft> selectedDrafts() const"]),
    ("src/ui/common/dialogs/InvoiceReviewTableModel.h", [
        "InvoiceReviewTableModel final : public QAbstractTableModel",
        "beginResetModel()", "endResetModel()", "beginInsertRows(", "endInsertRows()",
        "beginRemoveRows(", "endRemoveRows()", "bool setData(", "emit dataChanged(",
        "void setAllChecked(", "void setColumnForSelected("]),
]
for path, markers in preview_contracts:
    source = read(path)
    for required in markers:
        if required not in source:
            errors.append(f"{path}: shared import snapshot contract missing: {required}")
    forbidden_markers = ["QTableWidget", "QStandardItemModel"]
    if path.endswith("CurrentOrdersPageLocalSupport.cpp"):
        forbidden_markers = ["m_table = new QTableWidget", "m_table->setItem(", "m_table->setRowCount(", "m_table->item("]
    elif path.endswith("WorkPartsImportCoordinator.cpp"):
        forbidden_markers = ["m_table = new QTableWidget", "m_table->item(row, 0)", "m_table->setItem(row, 0"]
    for forbidden in forbidden_markers:
        if forbidden in source:
            errors.append(f"{path}: shared import preview must be model-driven: {forbidden}")


# Pass12 closure gates: the remaining WorkPartsDialog legacy editor hot paths keep indexed id->row maps so
# selection, draft restore and post-refresh focus do not linearly scan every row after each snapshot apply.
workparts_header_pass12 = read("src/ui/tabs/orders/WorkPartsDialog.h")
workparts_dialog_pass12 = read("src/ui/tabs/orders/WorkPartsDialog.cpp")
workparts_services_pass12 = read("src/ui/tabs/orders/ServiceTableController.cpp")
workparts_parts_pass12 = read("src/ui/tabs/orders/PartTableController.cpp")
workparts_editor_pass12 = read("src/ui/tabs/orders/WorkPartsEditorController.cpp")
for required in ["QHash<int, int> m_serviceRowById", "QHash<int, int> m_partRowById", "serviceRowById", "partRowById"]:
    if required not in workparts_header_pass12:
        errors.append(f"WorkPartsDialog.h: pass12 WorkPartsDialog row-index closure marker missing: {required}")
for required in ["P1/pass12 WorkPartsDialog row-index closure", "WorkPartsDialog::serviceRowById", "WorkPartsDialog::partRowById", "indexServiceRow", "indexPartRow"]:
    if required not in workparts_dialog_pass12:
        errors.append(f"WorkPartsDialog.cpp: pass12 WorkPartsDialog row-index closure marker missing: {required}")
for required in ["m_serviceRowById.clear()", "indexServiceRow(service.id, row)", "serviceRowById(m_activeServiceId)"]:
    if required not in workparts_services_pass12:
        errors.append(f"ServiceTableController.cpp: pass12 WorkPartsDialog row-index closure marker missing: {required}")
for required in ["m_partRowById.clear()", "indexPartRow(part.id, row)"]:
    if required not in workparts_parts_pass12:
        errors.append(f"PartTableController.cpp: pass12 WorkPartsDialog row-index closure marker missing: {required}")
for required in ["use the snapshot-built id index first", "serviceRowById(id)", "partRowById(id)"]:
    if required not in workparts_editor_pass12:
        errors.append(f"WorkPartsEditorController.cpp: pass12 WorkPartsDialog row-index closure marker missing: {required}")

# Pass13 closure gates: hot-path production qInfo is deep-debug only, while warnings/errors stay in production.
pass13_hot_paths = {
    "src/core/services/ActiveArchiveRoutingLayer.cpp": ["PERF ORDER_ROUTE"],
    "src/core/services/MutationExecutor.cpp": ["DB_MUTATION executor_started", "DB_MUTATION executor_finished"],
    "src/core/services/MutationPostCommitService.cpp": ["DB_MUTATION post_commit_mutation_log_ok", "DB_MUTATION post_commit_cache_invalidated"],
    "src/core/services/PartsQueryService.cpp": ["PARTS_SORT_REFRESH", "PART_DELIVERY_CHANGED"],
    "src/core/sync/SyncPayloadUtils.cpp": ["PART_PRICING_SYNC_IMPORT"],
    "src/ui/common/dialogs/OrderDialog.cpp": ["OrderDialog accept begin order", "OrderDialog accept elapsed_ms"],
    "src/ui/common/dialogs/OrderDialogBuildUi.cpp": ["OrderDialog lazy workparts init elapsed_ms"],
    "src/ui/common/dialogs/OrderDialogLifecycleFlow.cpp": ["OrderDialog loadOrder elapsed_ms", "OrderDialog ensureWorkPartsPage elapsed_ms"],
    "src/ui/main/MainWindowRuntime.cpp": ["PAGE_PREWARM", "PAGE_OPEN [%1] [stable]"],
    "src/ui/runtime/PageActivationCoordinator.cpp": ["activation_requested", "activation_done"],
    "src/core/services/ReportsPdfRenderer.cpp": ["Report PDF export started", "Report PDF export finished"],
    "src/ui/common/dialogs/OrderDialogPdfFlow.cpp": ["Order PDF export started", "Order PDF export finished"],
}
for rel, markers in pass13_hot_paths.items():
    text = read(rel)
    if "RuntimeLoggingControl::deepDebugEnabled()" not in text:
        errors.append(f"{rel}: pass13 production-log hot-path closure missing deep-debug guard")
    for marker in markers:
        pos = text.find(marker)
        if pos < 0:
            errors.append(f"{rel}: pass13 production-log hot-path marker missing: {marker}")
            continue
        guard = text.rfind("RuntimeLoggingControl::deepDebugEnabled()", 0, pos)
        if guard < 0 or pos - guard > 640:
            errors.append(f"{rel}: pass13 production-log hot-path closure requires deep-debug guard near {marker}")


if errors:
    print("[performance_tz_100_completion_gate] ERROR")
    for error in errors:
        print(" -", error)
    sys.exit(1)
print("[performance_tz_100_completion_gate] OK: runtime families, shutdown guards, refresh timeouts, import/backup/update dispatchers, bounded import process output, O(n) debt banner, prepared visible indexes and snapshot model gates are present")
