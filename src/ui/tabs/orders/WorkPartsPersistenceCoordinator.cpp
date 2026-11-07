// P2 architecture migration: implementation module converted from legacy .ipp implementation-fragment decomposition.
// Compiled as a separate P2 implementation module; include-order dependencies are isolated in the module prelude.

#include "ui/tabs/orders/WorkPartsDialogModulePrelude.h"
#include "core/services/orders/HistoryPartsAutofillService.h"
#include "ui/tabs/orders/HistoryPartsDraftRoles.h"

#include <algorithm>

bool WorkPartsDialog::stageServiceAttachmentAfterTransaction(int row, int serviceId, const QString& sourcePath)
{
    const QString normalizedSource = sourcePath.trimmed();
    if (normalizedSource.isEmpty()) return true;
    if (serviceId <= 0) {
        qCritical().noquote() << QStringLiteral("ATTACHMENT_STAGE service_stage_failed_missing_owner order=") << m_orderNumber
                              << QStringLiteral(" row=") << row
                              << QStringLiteral(" serviceId=") << serviceId
                              << QStringLiteral(" source=") << QFileInfo(normalizedSource).fileName();
        return false;
    }
    DeferredAttachmentOperation op;
    op.kind = DeferredAttachmentOperation::Kind::Service;
    op.row = row;
    op.ownerId = serviceId;
    op.sourcePath = normalizedSource;
    m_deferredAttachmentOperations.append(op);
    logWorkPartsDiagnostic(QStringLiteral("attachment_stage"),
                           QStringLiteral("service"),
                           row,
                           serviceId,
                           QString(),
                           {{QStringLiteral("phase"), QStringLiteral("queued_after_db_commit")},
                            {QStringLiteral("source"), QFileInfo(normalizedSource).fileName()}});
    qCDebug(csWorkPartsTraceLog).noquote() << QStringLiteral("ATTACHMENT_STAGE queued_after_db_commit type=service order=") << m_orderNumber
                      << QStringLiteral(" row=") << row
                      << QStringLiteral(" serviceId=") << serviceId
                      << QStringLiteral(" source=") << QFileInfo(normalizedSource).fileName();
    return true;
}

bool WorkPartsDialog::stagePartAttachmentAfterTransaction(int row, int partId, const QString& sourcePath,
                                                          const QString& invoiceNumber, const QString& invoiceDate,
                                                          const QString& supplierName, bool importedInvoiceRow,
                                                          const QString& importTraceId, double confirmedInvoiceAmount)
{
    const QString normalizedSource = sourcePath.trimmed();
    if (normalizedSource.isEmpty()) return true;
    if (partId <= 0) {
        qCritical().noquote() << QStringLiteral("ATTACHMENT_STAGE part_stage_failed_missing_owner order=") << m_orderNumber
                              << QStringLiteral(" row=") << row
                              << QStringLiteral(" partId=") << partId
                              << QStringLiteral(" imported=") << importedInvoiceRow
                              << QStringLiteral(" trace=") << importTraceId
                              << QStringLiteral(" source=") << QFileInfo(normalizedSource).fileName();
        return false;
    }
    DeferredAttachmentOperation op;
    op.kind = DeferredAttachmentOperation::Kind::Part;
    op.row = row;
    op.ownerId = partId;
    op.sourcePath = normalizedSource;
    op.invoiceNumber = invoiceNumber.trimmed();
    op.invoiceDate = invoiceDate.trimmed();
    op.supplierName = supplierName.trimmed();
    op.importedInvoiceRow = importedInvoiceRow;
    op.confirmedInvoiceAmount = confirmedInvoiceAmount;
    op.importTraceId = importTraceId;
    m_deferredAttachmentOperations.append(op);
    logWorkPartsDiagnostic(QStringLiteral("attachment_stage"),
                           QStringLiteral("part"),
                           row,
                           partId,
                           op.invoiceNumber,
                           {{QStringLiteral("phase"), QStringLiteral("queued_after_db_commit")},
                            {QStringLiteral("source"), QFileInfo(normalizedSource).fileName()},
                            {QStringLiteral("imported"), importedInvoiceRow},
                            {QStringLiteral("traceId"), importTraceId}});
    qCDebug(csWorkPartsTraceLog).noquote() << QStringLiteral("ATTACHMENT_STAGE queued_after_db_commit type=part order=") << m_orderNumber
                      << QStringLiteral(" row=") << row
                      << QStringLiteral(" partId=") << partId
                      << QStringLiteral(" imported=") << importedInvoiceRow
                      << QStringLiteral(" trace=") << importTraceId
                      << QStringLiteral(" invoiceNumber=") << clippedImportLogText(op.invoiceNumber)
                      << QStringLiteral(" invoiceDate=") << clippedImportLogText(op.invoiceDate)
                      << QStringLiteral(" source=") << QFileInfo(normalizedSource).fileName();
    return true;
}

bool WorkPartsDialog::processDeferredAttachmentOperation(const DeferredAttachmentOperation& op, QString* errorText)
{
    if (op.ownerId <= 0 || op.sourcePath.trimmed().isEmpty()) return true;
    logWorkPartsDiagnostic(QStringLiteral("attachment_save_begin"),
                           op.kind == DeferredAttachmentOperation::Kind::Service ? QStringLiteral("service") : QStringLiteral("part"),
                           op.row,
                           op.ownerId,
                           op.invoiceNumber,
                           {{QStringLiteral("source"), QFileInfo(op.sourcePath).fileName()}});

    QString attachError;
    QString sourceForStorage = op.sourcePath.trimmed();
    if (op.kind == DeferredAttachmentOperation::Kind::Part
        && (!op.invoiceNumber.trimmed().isEmpty() || !op.invoiceDate.trimmed().isEmpty())) {
        const QString pdfSource = ensureInvoicePdfAttachment(sourceForStorage,
                                                            op.invoiceNumber,
                                                            op.invoiceDate,
                                                            op.supplierName,
                                                            &attachError);
        if (!pdfSource.trimmed().isEmpty()) {
            sourceForStorage = pdfSource;
        } else {
            m_dirtyPartRows.insert(op.row);
            if (errorText) *errorText = attachError.trimmed().isEmpty()
                ? QString::fromUtf8("Вкладення накладної не збережено: спочатку підготуйте PDF через імпорт накладної.")
                : attachError;
            return false;
        }
    }

    const QString bucket = op.kind == DeferredAttachmentOperation::Kind::Service
        ? QStringLiteral("services")
        : QStringLiteral("parts");
    const QString storedPath = copiedAttachmentPath(m_orderNumber, bucket, op.ownerId, sourceForStorage, &attachError);
    if (storedPath.trimmed().isEmpty()) {
        const QString message = attachError.trimmed().isEmpty()
            ? QString::fromUtf8("Не вдалося підготувати файл вкладення після збереження рядків.")
            : attachError;
        if (op.kind == DeferredAttachmentOperation::Kind::Service) m_dirtyServiceRows.insert(op.row);
        else m_dirtyPartRows.insert(op.row);
        if (errorText) *errorText = message;
        logWorkPartsDiagnostic(QStringLiteral("attachment_save_fail"),
                               op.kind == DeferredAttachmentOperation::Kind::Service ? QStringLiteral("service") : QStringLiteral("part"),
                               op.row,
                               op.ownerId,
                               op.invoiceNumber,
                               {{QStringLiteral("phase"), QStringLiteral("file_copy")}, {QStringLiteral("error"), message}});
        qCritical().noquote() << QStringLiteral("ATTACHMENT_STAGE attachment_save_failed_dirty_kept order=") << m_orderNumber
                              << QStringLiteral(" row=") << op.row
                              << QStringLiteral(" ownerId=") << op.ownerId
                              << QStringLiteral(" type=") << (op.kind == DeferredAttachmentOperation::Kind::Service ? QStringLiteral("service") : QStringLiteral("part"))
                              << QStringLiteral(" phase=file_copy")
                              << QStringLiteral(" error=") << message;
        qCritical().noquote() << QStringLiteral("ATTACHMENT_STAGE file_copy_failed_after_db_commit order=") << m_orderNumber
                              << QStringLiteral(" row=") << op.row
                              << QStringLiteral(" ownerId=") << op.ownerId
                              << QStringLiteral(" error=") << message;
        return false;
    }

    bool metadataOk = false;
    if (op.kind == DeferredAttachmentOperation::Kind::Service) {
        metadataOk = WorkPartsMutationService::setServiceAttachment(op.ownerId,
                                                                    storedPath,
                                                                    QFileInfo(sourceForStorage).fileName(),
                                                                    &attachError);
    } else {
        metadataOk = WorkPartsMutationService::setPartAttachment(op.ownerId,
                                                                 storedPath,
                                                                 QFileInfo(sourceForStorage).fileName(),
                                                                 op.invoiceNumber,
                                                                 op.invoiceDate,
                                                                 &attachError, op.importedInvoiceRow, op.confirmedInvoiceAmount);
    }
    if (!metadataOk) {
        removeAttachmentFileIfManaged(storedPath);
        const QString message = attachError.trimmed().isEmpty()
            ? QString::fromUtf8("Файл вкладення збережено, але не вдалося записати metadata у БД; файл відкачено.")
            : attachError;
        if (op.kind == DeferredAttachmentOperation::Kind::Service) m_dirtyServiceRows.insert(op.row);
        else m_dirtyPartRows.insert(op.row);
        if (errorText) *errorText = message;
        logWorkPartsDiagnostic(QStringLiteral("attachment_save_fail"),
                               op.kind == DeferredAttachmentOperation::Kind::Service ? QStringLiteral("service") : QStringLiteral("part"),
                               op.row,
                               op.ownerId,
                               op.invoiceNumber,
                               {{QStringLiteral("phase"), QStringLiteral("file_copy")}, {QStringLiteral("error"), message}});
        qCritical().noquote() << QStringLiteral("ATTACHMENT_STAGE attachment_save_failed_dirty_kept order=") << m_orderNumber
                              << QStringLiteral(" row=") << op.row
                              << QStringLiteral(" ownerId=") << op.ownerId
                              << QStringLiteral(" type=") << (op.kind == DeferredAttachmentOperation::Kind::Service ? QStringLiteral("service") : QStringLiteral("part"))
                              << QStringLiteral(" phase=metadata")
                              << QStringLiteral(" error=") << message;
        qCritical().noquote() << QStringLiteral("ATTACHMENT_STAGE metadata_save_failed_after_file_copy order=") << m_orderNumber
                              << QStringLiteral(" row=") << op.row
                              << QStringLiteral(" ownerId=") << op.ownerId
                              << QStringLiteral(" storedPath=") << storedPath
                              << QStringLiteral(" error=") << message;
        return false;
    }

    if (op.kind == DeferredAttachmentOperation::Kind::Service && m_servicesTable && op.row >= 0 && op.row < m_servicesTable->rowCount()) {
        if (auto* attachmentItem = m_servicesTable->item(op.row, ServiceColAttachment)) {
            const QSignalBlocker blocker(m_servicesTable);
            attachmentItem->setData(RoleAttachmentPath, storedPath);
            attachmentItem->setData(RoleAttachmentSource, sourceForStorage);
            attachmentItem->setData(RoleAttachmentName, QFileInfo(sourceForStorage).fileName());
        }
        refreshServiceAttachmentCell(op.row);
    } else if (op.kind == DeferredAttachmentOperation::Kind::Part && m_partsTable && op.row >= 0 && op.row < m_partsTable->rowCount()) {
        if (auto* attachmentItem = m_partsTable->item(op.row, PartColAttachment)) {
            const QSignalBlocker blocker(m_partsTable);
            attachmentItem->setData(RoleAttachmentPath, storedPath);
            attachmentItem->setData(RoleAttachmentSource, sourceForStorage);
            attachmentItem->setData(RoleAttachmentName, QFileInfo(sourceForStorage).fileName());
            attachmentItem->setData(RoleAttachmentDocumentNumber, op.invoiceNumber);
            attachmentItem->setData(RoleAttachmentDocumentDate, op.invoiceDate);
        }
        refreshPartAttachmentCell(op.row);
        refreshPartInvoiceNumberCell(op.row);
    }

    if (op.kind == DeferredAttachmentOperation::Kind::Service) {
        m_dirtyServiceRows.remove(op.row);
    } else {
        m_dirtyPartRows.remove(op.row);
    }
    qCDebug(csWorkPartsTraceLog).noquote() << QStringLiteral("ATTACHMENT_STAGE dirty_cleared_after_deferred_attachment order=") << m_orderNumber
                      << QStringLiteral(" row=") << op.row
                      << QStringLiteral(" ownerId=") << op.ownerId
                      << QStringLiteral(" type=") << (op.kind == DeferredAttachmentOperation::Kind::Service ? QStringLiteral("service") : QStringLiteral("part"));
    logWorkPartsDiagnostic(QStringLiteral("attachment_save_end"),
                           op.kind == DeferredAttachmentOperation::Kind::Service ? QStringLiteral("service") : QStringLiteral("part"),
                           op.row,
                           op.ownerId,
                           op.invoiceNumber,
                           {{QStringLiteral("phase"), QStringLiteral("saved_after_db_commit")},
                            {QStringLiteral("path"), QFileInfo(storedPath).fileName()}});
    qCDebug(csWorkPartsTraceLog).noquote() << QStringLiteral("ATTACHMENT_STAGE saved_after_db_commit order=") << m_orderNumber
                      << QStringLiteral(" row=") << op.row
                      << QStringLiteral(" ownerId=") << op.ownerId
                      << QStringLiteral(" type=") << (op.kind == DeferredAttachmentOperation::Kind::Service ? QStringLiteral("service") : QStringLiteral("part"))
                      << QStringLiteral(" path=") << storedPath;
    return true;
}

bool WorkPartsDialog::processDeferredAttachmentOperations(QString* errorText)
{
    if (m_deferredAttachmentOperations.isEmpty()) return true;
    const QVector<DeferredAttachmentOperation> operations = m_deferredAttachmentOperations;
    m_deferredAttachmentOperations.clear();
    logWorkPartsDiagnostic(QStringLiteral("attachment_drain_begin"),
                           QStringLiteral("attachment"),
                           -1,
                           -1,
                           QString(),
                           {{QStringLiteral("count"), operations.size()}});
    qCDebug(csWorkPartsTraceLog).noquote() << QStringLiteral("ATTACHMENT_STAGE drain_begin_after_db_commit order=") << m_orderNumber
                      << QStringLiteral(" count=") << operations.size();
    for (const auto& op : operations) {
        QString localError;
        if (!processDeferredAttachmentOperation(op, &localError)) {
            if (errorText) *errorText = localError;
            logWorkPartsDiagnostic(QStringLiteral("attachment_drain_fail"),
                                   op.kind == DeferredAttachmentOperation::Kind::Service ? QStringLiteral("service") : QStringLiteral("part"),
                                   op.row,
                                   op.ownerId,
                                   op.invoiceNumber,
                                   {{QStringLiteral("error"), localError}});
            qCritical().noquote() << QStringLiteral("ATTACHMENT_STAGE drain_failed_after_db_commit order=") << m_orderNumber
                                  << QStringLiteral(" row=") << op.row
                                  << QStringLiteral(" ownerId=") << op.ownerId
                                  << QStringLiteral(" error=") << localError;
            return false;
        }
    }
    logWorkPartsDiagnostic(QStringLiteral("attachment_drain_end"),
                           QStringLiteral("attachment"),
                           -1,
                           -1,
                           QString(),
                           {{QStringLiteral("count"), operations.size()}});
    qCDebug(csWorkPartsTraceLog).noquote() << QStringLiteral("ATTACHMENT_STAGE drain_done_after_db_commit order=") << m_orderNumber
                      << QStringLiteral(" count=") << operations.size();
    return true;
}
// extracted from WorkPartsDialog.cpp as part of p.4 decomposition
void WorkPartsDialog::refreshAfterMutation(bool reloadServices, bool reloadParts, bool reloadHeader, bool ensureServiceSelection)
{
    bumpWorkPartsTableRevision(QStringLiteral("refresh_after_mutation_begin"));
    logWorkPartsDiagnostic(QStringLiteral("model_refresh_begin"),
                           QStringLiteral("model"),
                           -1,
                           -1,
                           QString(),
                           {{QStringLiteral("reloadServices"), reloadServices},
                            {QStringLiteral("reloadParts"), reloadParts},
                            {QStringLiteral("reloadHeader"), reloadHeader},
                            {QStringLiteral("ensureServiceSelection"), ensureServiceSelection}});
    AppRuntimeService::refreshOrderRegistry(m_orderNumber);
    scheduleWorkPartsRefresh(reloadServices,
                             reloadParts,
                             reloadHeader,
                             ensureServiceSelection,
                             QStringLiteral("mutation_refresh"));
    logWorkPartsDiagnostic(QStringLiteral("model_refresh_end"),
                           QStringLiteral("model"),
                           -1,
                           -1,
                           QString(),
                           {{QStringLiteral("reloadServices"), reloadServices},
                            {QStringLiteral("reloadParts"), reloadParts},
                            {QStringLiteral("reloadHeader"), reloadHeader}});
}

bool WorkPartsDialog::commitPendingEdits(QString* errorText, bool applyGrouping, bool skipUiRefreshAfterCommit)
{
    logWorkPartsDiagnostic(QStringLiteral("editor_commit_begin"),
                           QStringLiteral("model"),
                           -1,
                           -1,
                           QString(),
                           {{QStringLiteral("applyGrouping"), applyGrouping},
                            {QStringLiteral("skipUiRefreshAfterCommit"), skipUiRefreshAfterCommit}});
    ScopedOrderSaveBackupDisable backupDisableGuard;
    if (property("csImportBusy").toBool() || property("csImportApplyingRows").toBool()
        || (m_partsTable && (m_partsTable->property("csImportBusy").toBool() || m_partsTable->property("csImportApplyingRows").toBool()))) {
        const QString message = QString::fromUtf8("Імпорт накладної ще не завершив внутрішню обробку. Спробуйте ще раз через мить.");
        qWarning().noquote() << QStringLiteral("WorkPartsDialog commitPendingEdits blocked: import state active order=") << m_orderNumber;
        if (errorText) *errorText = message;
        logWorkPartsDiagnostic(QStringLiteral("editor_commit_close"), QStringLiteral("model"), -1, -1, QString(), {{QStringLiteral("ok"), false}, {QStringLiteral("reason"), QStringLiteral("import_busy")}});
        return false;
    }
    if (QWidget* popup = QApplication::activePopupWidget()) {
        const QString popupName = popup->metaObject() ? QString::fromLatin1(popup->metaObject()->className()) : QString();
        const QString message = QString::fromUtf8("Завершіть вибір у відкритому списку/підказці перед збереженням наряду.");
        qWarning().noquote() << QStringLiteral("WorkPartsDialog commitPendingEdits blocked: activePopup=") << popupName
                             << QStringLiteral(" order=") << m_orderNumber;
        if (errorText) *errorText = message;
        logWorkPartsDiagnostic(QStringLiteral("editor_commit_close"), QStringLiteral("model"), -1, -1, QString(), {{QStringLiteral("ok"), false}, {QStringLiteral("reason"), QStringLiteral("active_popup")}});
        return false;
    }
    if (m_commitPendingInProgress) {
        qWarning() << "WorkPartsDialog commitPendingEdits reentry blocked for order" << m_orderNumber;
        logWorkPartsDiagnostic(QStringLiteral("editor_commit_close"), QStringLiteral("model"), -1, -1, QString(), {{QStringLiteral("ok"), true}, {QStringLiteral("reason"), QStringLiteral("reentry_blocked")}});
        return true;
    }
    QScopedValueRollback<bool> commitGuard(m_commitPendingInProgress, true);

    flushInlineEditorValueToModel(m_servicesTable);
    flushInlineEditorValueToModel(m_partsTable);
    if (QWidget* fw = focusWidget()) fw->clearFocus();
    flushInlineEditorValueToModel(m_servicesTable);
    flushInlineEditorValueToModel(m_partsTable);

    const QSignalBlocker servicesBlocker(m_servicesTable);
    const QSignalBlocker partsBlocker(m_partsTable);
    const bool prevServicesUpdates = m_servicesTable ? m_servicesTable->updatesEnabled() : false;
    const bool prevPartsUpdates = m_partsTable ? m_partsTable->updatesEnabled() : false;
    if (m_servicesTable) m_servicesTable->setUpdatesEnabled(false);
    if (m_partsTable) m_partsTable->setUpdatesEnabled(false);
    auto restoreTableUpdates = qScopeGuard([&]() {
        if (m_servicesTable) m_servicesTable->setUpdatesEnabled(prevServicesUpdates);
        if (m_partsTable) m_partsTable->setUpdatesEnabled(prevPartsUpdates);
    });

    QString localError;

    QElapsedTimer timer;
    timer.start();

    auto collectServiceRowsToPersist = [this]() {
        QSet<int> rows;
        for (int dirtyRow : m_dirtyServiceRows) {
            if (dirtyRow >= 0 && m_servicesTable && dirtyRow < m_servicesTable->rowCount()) rows.insert(dirtyRow);
        }
        if (m_servicesTable) {
            for (int row = 0; row < m_servicesTable->rowCount(); ++row) {
                auto* titleItem = m_servicesTable->item(row, ServiceColTitle);
                if (!titleItem || isSpacerItem(titleItem)) continue;
                const bool hasStoredId = titleItem->data(RoleId).toInt() > 0;
                if (!hasStoredId && rowHasMeaningfulServiceData(m_servicesTable, row)) {
                    rows.insert(row);
                }
            }
        }
        QList<int> result = rows.values();
        std::sort(result.begin(), result.end());
        return result;
    };

    auto collectPartRowsToPersist = [this]() {
        QSet<int> rows;
        for (int dirtyRow : m_dirtyPartRows) {
            if (dirtyRow >= 0 && m_partsTable && dirtyRow < m_partsTable->rowCount()) rows.insert(dirtyRow);
        }
        if (m_partsTable) {
            for (int row = 0; row < m_partsTable->rowCount(); ++row) {
                auto* nameItem = m_partsTable->item(row, PartColName);
                if (!nameItem || isSpacerItem(nameItem)) continue;
                const bool hasStoredId = nameItem->data(RoleId).toInt() > 0;
                if (!hasStoredId && rowHasMeaningfulPartData(m_partsTable, row)) {
                    rows.insert(row);
                }
            }
        }
        QList<int> result = rows.values();
        std::sort(result.begin(), result.end());
        return result;
    };

    const QList<int> serviceRowsToPersist = collectServiceRowsToPersist();
    for (int row : serviceRowsToPersist) autofillDraftPartsForService(row);
    const QList<int> partRowsToPersist = collectPartRowsToPersist();
    if (!commitPendingRowsInSingleTransaction(serviceRowsToPersist, partRowsToPersist, applyGrouping, &localError)) {
        if (errorText) *errorText = localError;
        logWorkPartsDiagnostic(QStringLiteral("editor_commit_close"),
                               QStringLiteral("model"),
                               -1,
                               -1,
                               QString(),
                               {{QStringLiteral("ok"), false},
                                {QStringLiteral("error"), localError},
                                {QStringLiteral("serviceRows"), serviceRowsToPersist.size()},
                                {QStringLiteral("partRows"), partRowsToPersist.size()}});
        return false;
    }

    const bool hadChanges = !serviceRowsToPersist.isEmpty() || !partRowsToPersist.isEmpty();
    if (hadChanges && !skipUiRefreshAfterCommit) {
        refreshAfterMutation(!serviceRowsToPersist.isEmpty(), true, true, !serviceRowsToPersist.isEmpty());
    }
    if (hadChanges) {
        m_userTouchedPendingContent = false;
        notifyDraftDirtyChanged();
    }
    qCDebug(csWorkPartsTraceLog).noquote() << QStringLiteral("SAVE_DONE order=%1 serviceRows=%2 partRows=%3 elapsedMs=%4")
        .arg(m_orderNumber)
        .arg(serviceRowsToPersist.size())
        .arg(partRowsToPersist.size())
        .arg(timer.elapsed());
    qCDebug(csWorkPartsTraceLog) << "WorkPartsDialog commitPendingEdits elapsed_ms" << timer.elapsed()
            << "order" << m_orderNumber
            << "services_saved" << serviceRowsToPersist.size()
            << "parts_saved" << partRowsToPersist.size()
            << "skip_ui_refresh" << skipUiRefreshAfterCommit;
    logWorkPartsDiagnostic(QStringLiteral("editor_commit_close"),
                           QStringLiteral("model"),
                           -1,
                           -1,
                           QString(),
                           {{QStringLiteral("ok"), true},
                            {QStringLiteral("elapsedMs"), static_cast<int>(timer.elapsed())},
                            {QStringLiteral("serviceRows"), serviceRowsToPersist.size()},
                            {QStringLiteral("partRows"), partRowsToPersist.size()},
                            {QStringLiteral("skipUiRefresh"), skipUiRefreshAfterCommit}});
    return true;
}

bool WorkPartsDialog::savePrimaryMaster()
{
    if (!m_masterCombo || m_masterCombo->currentIndex() < 0) return false;
    const QString masterId = m_masterCombo->currentData().toString();
    const QString masterName = m_masterCombo->currentText().trimmed();
    if (masterId.isEmpty()) return false;

    QString mutationError;
    if (!OrderMutationFacade::updatePrimaryMaster(m_orderNumber, masterId, masterName, &mutationError)) {
        if (!mutationError.trimmed().isEmpty()) QMessageBox::critical(this, QString::fromUtf8("Помилка"), mutationError);
        return false;
    }

    refreshAfterMutation(true, false, true, false);
    publishWorkPartsBackupEvent(QStringLiteral("order.master"),
                                QStringLiteral("work_order"),
                                QString::number(m_orderNumber),
                                currentOrderUid(),
                                m_orderNumber,
                                QStringLiteral("update_master"),
                                QString::fromUtf8("Оновлено основного майстра"),
                                {QStringLiteral("work_orders")});
    QMessageBox::information(this, QString::fromUtf8("Готово"), QString::fromUtf8("Основного майстра збережено"));
    return true;
}


bool WorkPartsDialog::commitPendingRowsInSingleTransaction(const QList<int>& serviceRowsToPersist,
                                                          const QList<int>& partRowsToPersist,
                                                          bool applyGrouping,
                                                          QString* errorText)
{
    const bool hadChanges = !serviceRowsToPersist.isEmpty() || !partRowsToPersist.isEmpty();
    if (!hadChanges) return true;

    QList<int> newServiceRows;
    for (int row : serviceRowsToPersist) {
        const auto* title = m_servicesTable ? m_servicesTable->item(row, ServiceColTitle) : nullptr;
        if (title && title->data(RoleId).toInt() <= 0) newServiceRows.append(row);
    }
    QList<int> newPartRows;
    for (int row : partRowsToPersist) {
        const auto* name = m_partsTable ? m_partsTable->item(row, PartColName) : nullptr;
        if (name && name->data(RoleId).toInt() <= 0) newPartRows.append(row);
    }
    const int activeServiceBeforeSave = m_activeServiceId;

    MutationExecutor::Request request;
    request.sourceModule = QStringLiteral("ui.tabs.orders.WorkPartsPersistenceCoordinator");
    request.entityType = QStringLiteral("order");
    request.entityId = QString::number(m_orderNumber);
    request.operation = QStringLiteral("commit_pending_rows");
    request.description = QString::fromUtf8("Пакетне збереження змінених робіт і запчастин");
    request.changedTables = {QStringLiteral("work_order_services"),
                             QStringLiteral("service_masters"),
                             QStringLiteral("work_order_parts"),
                             QStringLiteral("work_orders")};
    request.beginErrorFallback = QString::fromUtf8("Не можу відкрити транзакцію БД");
    request.commitErrorFallback = QString::fromUtf8("Не вдалося завершити збереження робіт і запчастин.");

    m_deferredAttachmentOperations.clear();
    const QString transactionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    setProperty("csActiveTransactionId", transactionId);
    logWorkPartsDiagnostic(QStringLiteral("transaction_begin"),
                           QStringLiteral("order"),
                           -1,
                           -1,
                           QString(),
                           {{QStringLiteral("serviceRows"), serviceRowsToPersist.size()},
                            {QStringLiteral("partRows"), partRowsToPersist.size()},
                            {QStringLiteral("applyGrouping"), applyGrouping}});
    qCDebug(csWorkPartsTraceLog).noquote() << QStringLiteral("ATTACHMENT_STAGE queue_reset_before_db_transaction order=") << m_orderNumber;

    const auto execResult = WorkPartsMutationService::execute(request,
                                                      [&](auto& txDb, QString* txError) {
        const QString previousExternalSaveConnection = m_externalSaveConnectionName;
        QScopedValueRollback<bool> externalSaveStateGuard(m_useExternalSaveTransaction, true);
        QScopedValueRollback<QString> externalSaveConnectionGuard(m_externalSaveConnectionName, previousExternalSaveConnection);
        m_externalSaveConnectionName = txDb.connectionName();

        QString localError;
        for (int row : serviceRowsToPersist) {
            if (!saveServiceRow(row, &localError, false)) {
                if (txError) *txError = localError;
                return false;
            }
        }

        for (int row : partRowsToPersist) {
            if (!savePartRow(row, &localError, false)) {
                if (txError) *txError = localError;
                return false;
            }
        }

        // Manual rows must be present before duplicate detection by the history matcher.
        for (int row : newServiceRows) {
            const auto* title = m_servicesTable->item(row, ServiceColTitle);
            const int serviceId = title ? title->data(RoleId).toInt() : 0;
            const bool previewHandled = title && title->data(HistoryPartsDraftRoles::CheckedTitle).toString()
                == title->text().simplified().toCaseFolded();
            if (!previewHandled && serviceId > 0 && !HistoryPartsAutofillService::apply(txDb, m_orderNumber, serviceId, txError)) return false;
        }

        QString statusError;
        if (!promoteOrderStatusToInProgressIfNeeded(txDb.connectionName(), m_orderNumber, &statusError)) {
            if (txError) *txError = statusError;
            return false;
        }
                return true;
    });

    if (!execResult.ok) {
        // Rolled-back IDs cannot be reused when the user retries the same draft.
        auto resetNewIds = [](QTableWidget* table, const QList<int>& rows) {
            if (!table) return;
            const QSignalBlocker blocker(table);
            for (int row : rows) for (int col = 0; col < table->columnCount(); ++col)
                if (auto* item = table->item(row, col)) item->setData(RoleId, QVariant());
        };
        resetNewIds(m_servicesTable, newServiceRows);
        resetNewIds(m_partsTable, newPartRows);
        m_activeServiceId = activeServiceBeforeSave;
        logWorkPartsDiagnostic(QStringLiteral("transaction_rollback"),
                               QStringLiteral("order"),
                               -1,
                               -1,
                               QString(),
                               {{QStringLiteral("error"), execResult.errorText}});
        setProperty("csActiveTransactionId", QString());
        m_deferredAttachmentOperations.clear();
        if (errorText) *errorText = execResult.errorText;
        return false;
    }

    QString deferredAttachmentError;
    if (!processDeferredAttachmentOperations(&deferredAttachmentError)) {
        logWorkPartsDiagnostic(QStringLiteral("transaction_rollback"),
                               QStringLiteral("order"),
                               -1,
                               -1,
                               QString(),
                               {{QStringLiteral("phase"), QStringLiteral("deferred_attachment")}, {QStringLiteral("error"), deferredAttachmentError}});
        setProperty("csActiveTransactionId", QString());
        if (errorText) *errorText = deferredAttachmentError;
        return false;
    }
    logWorkPartsDiagnostic(QStringLiteral("transaction_commit"),
                           QStringLiteral("order"),
                           -1,
                           -1,
                           QString(),
                           {{QStringLiteral("serviceRows"), serviceRowsToPersist.size()},
                            {QStringLiteral("partRows"), partRowsToPersist.size()}});
    setProperty("csActiveTransactionId", QString());

    for (int row : serviceRowsToPersist) {
        if (m_servicesTable && row >= 0 && row < m_servicesTable->rowCount()) {
            clearRowHintState(m_servicesTable, row);
            setRowEditingVisual(m_servicesTable, row);
            refreshServiceAttachmentCell(row);
        }
        m_dirtyServiceRows.remove(row);
        logWorkPartsDiagnostic(QStringLiteral("dirty_state_changed"), QStringLiteral("service"), row, -1, QString(), {{QStringLiteral("reason"), QStringLiteral("commit_clear")}});
    }
    for (int row : partRowsToPersist) {
        if (m_partsTable && row >= 0 && row < m_partsTable->rowCount()) {
            clearRowHintState(m_partsTable, row);
            setRowEditingVisual(m_partsTable, row);
            refreshPartAttachmentCell(row);
        }
        m_dirtyPartRows.remove(row);
        logWorkPartsDiagnostic(QStringLiteral("dirty_state_changed"), QStringLiteral("part"), row, -1, QString(), {{QStringLiteral("reason"), QStringLiteral("commit_clear")}});
    }
    qCDebug(csWorkPartsTraceLog).noquote() << QStringLiteral("SAVE_DONE dirty_cleared_after_full_commit order=") << m_orderNumber
                      << QStringLiteral(" serviceRows=") << serviceRowsToPersist.size()
                      << QStringLiteral(" partRows=") << partRowsToPersist.size();

    if (applyGrouping) autoAssignPartsToServices();

    QStringList changedTables = {QStringLiteral("work_order_services"),
                                 QStringLiteral("service_masters"),
                                 QStringLiteral("work_order_parts"),
                                 QStringLiteral("work_orders")};
    QString operationType = QStringLiteral("batch_commit");
    QString description = QString::fromUtf8("Пакетне збереження робіт і запчастин");
    if (!serviceRowsToPersist.isEmpty() && partRowsToPersist.isEmpty()) {
        changedTables = {QStringLiteral("work_order_services"), QStringLiteral("service_masters"),
                         QStringLiteral("work_order_parts"), QStringLiteral("work_orders")};
        operationType = QStringLiteral("batch_commit_services");
        description = QString::fromUtf8("Пакетне збереження робіт");
    } else if (serviceRowsToPersist.isEmpty() && !partRowsToPersist.isEmpty()) {
        changedTables = {QStringLiteral("work_order_parts"), QStringLiteral("work_orders")};
        operationType = QStringLiteral("batch_commit_parts");
        description = QString::fromUtf8("Пакетне збереження запчастин");
    }

    publishWorkPartsBackupEvent(QStringLiteral("order.commit"),
                                QStringLiteral("order"),
                                QString::number(m_orderNumber),
                                currentOrderUid(),
                                m_orderNumber,
                                operationType,
                                description,
                                changedTables);
    return true;
}
