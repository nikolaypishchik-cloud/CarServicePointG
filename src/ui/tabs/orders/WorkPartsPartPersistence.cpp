// P2 architecture migration: implementation module converted from legacy .ipp implementation-fragment decomposition.
// Compiled as a separate P2 implementation module; include-order dependencies are isolated in the module prelude.

#include "ui/tabs/orders/WorkPartsDialogModulePrelude.h"

#include "core/services/MutationExecutor.h"
#include "core/services/orders/PartRowPayloadBuilderService.h"
#include "core/services/orders/WorkPartsQueryService.h"
#include "ui/tabs/orders/HistoryPartsDraftRoles.h"
// extracted from WorkPartsPersistenceCoordinator.cpp as part of p.4 decomposition
bool WorkPartsDialog::savePartRow(int row, QString* errorText, bool reloadAfterSave)
{
    if (!PartEditSessionService::canPersistPartsRow(m_partsTable, m_loadingParts, row)) return false;

    QString routeError;
    if (!ensureOrderReadyForEditing(m_orderNumber, &routeError)) {
        const QString message = QString::fromUtf8("Наряд не готовий до збереження в робочій БД.\n%1").arg(routeError);
        if (errorText) *errorText = message;
        else QMessageBox::critical(this, QString::fromUtf8("Помилка при збереженні запчастини"), message);
        return false;
    }

    const QString orderUidSnapshot = currentOrderUid(nullptr);
    const WorkPartsPartEditState partState = PartTableStateMapper::readForSave(m_partsTable, row, m_orderNumber, orderUidSnapshot);
    if (!partState.hasName) {
        PartEditSessionService::clearDirtyEmptyPartRow(m_dirtyPartRows, row);
        return true;
    }

    const int partId = partState.partId;
    const bool importedInvoiceRow = partState.importedInvoiceRow;
    const QString importTraceId = partState.importTraceId;
    const QString name = partState.name;
    const QString orderedAtDisplay = partState.dates.displayOrderedAt;
    const QString orderedAt = partState.dates.storedOrderedAt;
    const QString deliveryDate = partState.dates.deliveryDate;
    const QString installedAt = partState.dates.installedAt;
    const QString store = partState.store;
    const QString oemNumber = partState.oemNumber;
    const QString manufacturer = partState.manufacturer;
    const double safeQty = partState.safeQty;
    const double typedPrice = partState.price.typedPrice;
    const bool okPrice = partState.price.okPrice;
    const double purchasePrice = partState.price.purchasePrice;
    auto* priceItem = m_partsTable->item(row, PartColPrice);

    logWorkPartsDiagnostic(partId > 0 ? QStringLiteral("row_update_begin") : QStringLiteral("row_insert_persist_begin"),
                           QStringLiteral("part"),
                           row,
                           partId,
                           QString(),
                           {{QStringLiteral("imported"), importedInvoiceRow},
                            {QStringLiteral("traceId"), importTraceId},
                            {QStringLiteral("name"), name},
                            {QStringLiteral("store"), store},
                            {QStringLiteral("brand"), manufacturer}});

    logPartCrashContext(QStringLiteral("CRASH_GUARD"), QStringLiteral("savePartRow_enter"), m_partsTable, row, PartColName, m_orderNumber, orderUidSnapshot);
    qCDebug(csWorkPartsTraceLog).noquote() << QStringLiteral("IMPORT_PARTS save_prepare order=") << m_orderNumber
                      << QStringLiteral(" row=") << row
                      << QStringLiteral(" partId=") << partId
                      << QStringLiteral(" imported=") << importedInvoiceRow
                      << QStringLiteral(" trace=") << importTraceId
                      << QStringLiteral(" dateText=") << clippedImportLogText(orderedAtDisplay)
                      << QStringLiteral(" oem=") << clippedImportLogText(oemNumber)
                      << QStringLiteral(" name=") << clippedImportLogText(name)
                      << QStringLiteral(" qtyText=") << clippedImportLogText(m_partsTable->item(row, PartColQty) ? m_partsTable->item(row, PartColQty)->text() : QString())
                      << QStringLiteral(" priceText=") << clippedImportLogText(priceItem ? priceItem->text() : QString())
                      << QStringLiteral(" store=") << clippedImportLogText(store)
                      << QStringLiteral(" brand=") << clippedImportLogText(manufacturer);
    qCDebug(csWorkPartsTraceLog).noquote() << QStringLiteral("PART_PRICING op=save order=%1 partId=%2 trace=%3 name=\"%4\" store=\"%5\" qty=%6 purchase=%7 source=pending override=%8")
        .arg(m_orderNumber)
        .arg(partId)
        .arg(importedInvoiceRow ? QStringLiteral("import") : QStringLiteral("manual_inline"))
        .arg(name)
        .arg(store)
        .arg(safeQty, 0, 'f', 3)
        .arg(purchasePrice, 0, 'f', 2)
        .arg(priceItem && priceItem->data(RoleManualMarkupOverride).toBool() ? 1 : 0);

    PartRowPayloadInput payloadInput;
    payloadInput.orderedAt = orderedAt;
    payloadInput.deliveryDate = deliveryDate;
    payloadInput.installedAt = installedAt;
    payloadInput.store = store;
    payloadInput.partNumber = oemNumber;
    payloadInput.name = name;
    payloadInput.note = QStringLiteral("");
    payloadInput.manufacturer = manufacturer;
    payloadInput.qty = safeQty;
    payloadInput.purchasePrice = purchasePrice;
    // Persist the purchase/store price as the pricing base for both imported and
    // manually entered rows.  The visible price cell is rewritten to the client price
    // after calculation, but PartPricePolicyService resolves it back to RolePurchasePrice
    // when the row is saved.  Saving as "client" here disabled global markup and made
    // individual markup use the wrong base.
    payloadInput.sourceType = importedInvoiceRow ? QStringLiteral("import") : QStringLiteral("manual_inline");
    // Preserve the active work-row association for parts imported from the order editor.
    // Older databases that still had service_id foreign keys rejected service_id=0 during
    // invoice import with a generic "constraint failed Unable to fetch row" error.
    payloadInput.serviceId = m_activeServiceId > 0 ? m_activeServiceId : 0;
    if (auto* nameItem = m_partsTable->item(row, PartColName)) {
        const QString workKey = nameItem->data(HistoryPartsDraftRoles::SourceWorkKey).toString();
        if (!workKey.isEmpty()) {
            payloadInput.serviceId = 0;
            payloadInput.note = nameItem->data(HistoryPartsDraftRoles::SourceNote).toString();
            for (int r = 0; r < m_servicesTable->rowCount(); ++r) {
                auto* title = m_servicesTable->item(r, ServiceColTitle);
                if (title && title->data(HistoryPartsDraftRoles::WorkKey).toString() == workKey) {
                    payloadInput.serviceId = qMax(0, title->data(RoleId).toInt());
                    break;
                }
            }
        }
    }
    payloadInput.preserveEmptyOrderedAt = importedInvoiceRow;
    payloadInput.preserveEmptyDeliveryDate = importedInvoiceRow;
    if (priceItem) {
        bool manualMarkupOk = false;
        const double manualMarkup = priceItem->data(RoleMarkupPercent).toDouble(&manualMarkupOk);
        payloadInput.manualMarkupOverride = priceItem->data(RoleManualMarkupOverride).toBool() && manualMarkupOk && manualMarkup >= 0.0;
        payloadInput.manualMarkupPercent = payloadInput.manualMarkupOverride ? qMax(0.0, manualMarkup) : 0.0;
    }
    WorkOrderPartRow payload = PartRowPayloadBuilderService::build(payloadInput);
    if (payload.manualMarkupOverride) {
        qCDebug(csWorkPartsTraceLog).noquote() << QStringLiteral("PART_PRICING op=save order=%1 partId=%2 trace=manual_override name=\"%3\" store=\"%4\" qty=%5 purchase=%6 client=%7 activePct=%8 source=manual override=1")
            .arg(m_orderNumber)
            .arg(partId)
            .arg(payload.name)
            .arg(payload.store)
            .arg(payload.qty, 0, 'f', 3)
            .arg(payload.purchasePrice, 0, 'f', 2)
            .arg(payload.clientPrice, 0, 'f', 2)
            .arg(payload.markupPercent, 0, 'f', 2);
    }
    qCDebug(csWorkPartsTraceLog).noquote() << QStringLiteral("IMPORT_PARTS payload_ready order=") << m_orderNumber
                      << QStringLiteral(" row=") << row
                      << QStringLiteral(" partId=") << partId
                      << QStringLiteral(" imported=") << importedInvoiceRow
                      << QStringLiteral(" trace=") << importTraceId
                      << QStringLiteral(" orderedAt=") << payload.orderedAt
                      << QStringLiteral(" deliveryDate=") << payload.deliveryDate
                      << QStringLiteral(" purchase=") << payload.purchasePrice
                      << QStringLiteral(" client=") << payload.clientPrice
                      << QStringLiteral(" markup=") << payload.markupPercent;
    const double pct = payload.markupPercent;
    const double clientPrice = payload.clientPrice;

    if (priceItem) {
        const QSignalBlocker blocker(m_partsTable);
        priceItem->setData(RolePurchasePrice, payload.purchasePrice);
        priceItem->setData(RoleMarkupPercent, payload.manualMarkupOverride ? pct : 0.0);
        priceItem->setData(RoleManualMarkupOverride, payload.manualMarkupOverride);
        priceItem->setToolTip(QString::fromUtf8("%1: %2 %. Закупівля: %3 грн. Клієнту: %4 грн.")
            .arg(payload.manualMarkupOverride ? QString::fromUtf8("Індивідуальна націнка") : QString::fromUtf8("Глобальна націнка"))
            .arg(payload.markupPercent, 0, 'f', 1)
            .arg(payload.purchasePrice, 0, 'f', 2)
            .arg(payload.clientPrice, 0, 'f', 2));
    }

    QString saveError;
    int newPartId = 0;
    auto syncPartIdToRow = [&](int id) {
        if (!m_partsTable || id <= 0 || row < 0 || row >= m_partsTable->rowCount()) return;
        const QSignalBlocker blocker(m_partsTable);
        for (int c = 0; c < m_partsTable->columnCount(); ++c) {
            if (auto* cell = m_partsTable->item(row, c)) {
                cell->setData(RoleId, id);
            }
        }
        qCDebug(csWorkPartsTraceLog).noquote() << QStringLiteral("PARTS_TABLE role_id_synced order=") << m_orderNumber
                          << QStringLiteral(" row=") << row
                          << QStringLiteral(" partId=") << id
                          << QStringLiteral(" imported=") << importedInvoiceRow
                          << QStringLiteral(" trace=") << importTraceId;
    };
    const QString snapshotPath = writePartCrashSnapshot(QStringLiteral("before_db_mutation"), m_partsTable, row, PartColName, m_orderNumber, orderUidSnapshot, QStringLiteral("operation=%1").arg(partId > 0 ? QStringLiteral("update") : QStringLiteral("insert")));
    qCDebug(csWorkPartsTraceLog).noquote() << QStringLiteral("DB_MUTATION part_row_begin order=") << m_orderNumber
                      << QStringLiteral(" row=") << row
                      << QStringLiteral(" partId=") << partId
                      << QStringLiteral(" operation=") << (partId > 0 ? QStringLiteral("update") : QStringLiteral("insert"))
                      << QStringLiteral(" imported=") << importedInvoiceRow
                      << QStringLiteral(" trace=") << importTraceId
                      << QStringLiteral(" snapshot=") << snapshotPath;
    const QString connectionName = m_useExternalSaveTransaction ? m_externalSaveConnectionName : QString();
    if (m_useExternalSaveTransaction) {
        QString localError;
        const bool ok = WorkPartsMutationService::runInActiveTransaction(connectionName, [&](auto& db, QString*) {
            if (partId > 0) {
                return WorkPartsPersistenceRepositoryFacade::updatePart(db, m_orderNumber, partId, payload, &saveError);
            }
            return WorkPartsPersistenceRepositoryFacade::insertPart(db, m_orderNumber, payload, &newPartId, &saveError);
        }, &localError);
        if (!ok) {
            logPartCrashContext(QStringLiteral("DB_MUTATION"), QStringLiteral("external_save_failed"), m_partsTable, row, PartColName, m_orderNumber, orderUidSnapshot, !saveError.trimmed().isEmpty() ? saveError : localError);
            const QString detail = !saveError.trimmed().isEmpty() ? saveError : localError;
            const QString message = QString::fromUtf8("Не вдалося зберегти рядок запчастини.\n%1").arg(detail);
            if (errorText) *errorText = message;
            else QMessageBox::critical(this, QString::fromUtf8("Помилка при збереженні запчастини"), message);
            return false;
        }
        if (newPartId > 0) {
            syncPartIdToRow(newPartId);
        }
    } else {
        MutationExecutor::Request request;
        request.sourceModule = QStringLiteral("ui.tabs.orders.WorkPartsPartPersistence");
        request.entityType = QStringLiteral("part");
        request.entityId = partId > 0 ? QString::number(partId) : QString::number(m_orderNumber);
        request.operation = partId > 0 ? QStringLiteral("update_part_row") : QStringLiteral("insert_part_row");
        request.description = partId > 0
            ? QString::fromUtf8("Оновлення рядка запчастини")
            : QString::fromUtf8("Збереження нового рядка запчастини");
        request.changedTables = {QStringLiteral("work_order_parts"), QStringLiteral("work_orders")};
        request.beginErrorFallback = QString::fromUtf8("Не можу відкрити транзакцію БД");
        request.commitErrorFallback = partId > 0
            ? QString::fromUtf8("Не вдалося завершити оновлення запчастини")
            : QString::fromUtf8("Не вдалося завершити збереження запчастини");
        const auto mutationResult = WorkPartsMutationService::execute(request, [&](auto& txDb, QString* txError) {
            if (partId > 0) {
                if (!WorkPartsPersistenceRepositoryFacade::updatePart(txDb, m_orderNumber, partId, payload, &saveError)) {
                    if (txError) *txError = saveError;
                    return false;
                }
            } else {
                if (!WorkPartsPersistenceRepositoryFacade::insertPart(txDb, m_orderNumber, payload, &newPartId, &saveError)) {
                    if (txError) *txError = saveError;
                    return false;
                }
            }
            QString statusError;
            if (!promoteOrderStatusToInProgressIfNeeded(txDb.connectionName(), m_orderNumber, &statusError)) {
                if (txError) *txError = statusError;
                return false;
            }
            return true;
        });
        const QString mutationError = mutationResult.errorText;
        const bool ok = mutationResult.ok;
        if (!ok) {
            logPartCrashContext(QStringLiteral("DB_MUTATION"), QStringLiteral("mutation_executor_failed"), m_partsTable, row, PartColName, m_orderNumber, orderUidSnapshot, mutationError);
            const QString detail = mutationError.trimmed().isEmpty() ? request.commitErrorFallback : mutationError;
            const QString message = QString::fromUtf8("Не вдалося зберегти рядок запчастини.\n%1").arg(detail);
            if (errorText) *errorText = message;
            else QMessageBox::critical(this, QString::fromUtf8("Помилка при збереженні запчастини"), message);
            return false;
        }
        if (newPartId > 0) {
            syncPartIdToRow(newPartId);
        }
    }

    const int effectivePartId = partId > 0 ? partId : newPartId;
    bool deferredPartAttachmentQueued = false;
    QString pendingInvoiceNumber;
    QString pendingInvoiceDate;
    qCDebug(csWorkPartsTraceLog).noquote() << QStringLiteral("DB_MUTATION part_row_done order=") << m_orderNumber
                      << QStringLiteral(" row=") << row
                      << QStringLiteral(" oldPartId=") << partId
                      << QStringLiteral(" effectivePartId=") << effectivePartId
                      << QStringLiteral(" imported=") << importedInvoiceRow
                      << QStringLiteral(" trace=") << importTraceId;
    if (effectivePartId > 0) {
        syncPartIdToRow(effectivePartId);
    } else {
        qCritical().noquote() << QStringLiteral("PARTS_TABLE save_without_effective_part_id order=") << m_orderNumber
                              << QStringLiteral(" row=") << row
                              << QStringLiteral(" oldPartId=") << partId
                              << QStringLiteral(" newPartId=") << newPartId
                              << QStringLiteral(" imported=") << importedInvoiceRow
                              << QStringLiteral(" trace=") << importTraceId;
    }
    if (effectivePartId > 0) {
        if (auto* attachmentItem = m_partsTable->item(row, PartColAttachment)) {
            const QString pendingSource = attachmentItem->data(RoleAttachmentSource).toString().trimmed();
            pendingInvoiceNumber = attachmentItem->data(RoleAttachmentDocumentNumber).toString().trimmed();
            pendingInvoiceDate = attachmentItem->data(RoleAttachmentDocumentDate).toString().trimmed();
            const QString pendingSupplierName = m_partsTable && m_partsTable->item(row, PartColStore)
                ? m_partsTable->item(row, PartColStore)->text().trimmed()
                : QString();
            if (!pendingSource.isEmpty()) {
                if (m_useExternalSaveTransaction) {
                    if (!stagePartAttachmentAfterTransaction(row, effectivePartId, pendingSource,
                                                            pendingInvoiceNumber, pendingInvoiceDate,
                                                            pendingSupplierName, importedInvoiceRow, importTraceId,
                                                            importedInvoiceRow && okPrice ? safeQty*typedPrice : -1)) {
                        const QString message = QString::fromUtf8("Не вдалося поставити вкладення запчастини у чергу після транзакції.");
                        logPartCrashContext(QStringLiteral("ATTACHMENT_STAGE"), QStringLiteral("part_stage_failed_dirty_kept"), m_partsTable, row, PartColAttachment, m_orderNumber, orderUidSnapshot, message);
                        m_dirtyPartRows.insert(row);
                        logWorkPartsDiagnostic(QStringLiteral("attachment_stage_fail"),
                                               QStringLiteral("part"),
                                               row,
                                               effectivePartId,
                                               pendingInvoiceNumber,
                                               {{QStringLiteral("error"), message},
                                                {QStringLiteral("traceId"), importTraceId}});
                        logWorkPartsDiagnostic(QStringLiteral("dirty_state_changed"),
                                               QStringLiteral("part"),
                                               row,
                                               effectivePartId,
                                               pendingInvoiceNumber,
                                               {{QStringLiteral("reason"), QStringLiteral("attachment_stage_fail")}});
                        if (errorText) *errorText = message;
                        return false;
                    }
                    deferredPartAttachmentQueued = true;
                } else {
                    qCDebug(csWorkPartsTraceLog).noquote() << QStringLiteral("ATTACHMENT_SAVE begin order=") << m_orderNumber
                                      << QStringLiteral(" row=") << row
                                      << QStringLiteral(" partId=") << effectivePartId
                                      << QStringLiteral(" imported=") << importedInvoiceRow
                                      << QStringLiteral(" trace=") << importTraceId
                                      << QStringLiteral(" invoiceNumber=") << clippedImportLogText(pendingInvoiceNumber)
                                      << QStringLiteral(" invoiceDate=") << clippedImportLogText(pendingInvoiceDate);
                    QString attachError;
                    QString sourceForStorage = pendingSource;
                    if (!pendingInvoiceNumber.isEmpty() || !pendingInvoiceDate.isEmpty()) {
                        const QString pdfSource = ensureInvoicePdfAttachment(pendingSource, pendingInvoiceNumber, pendingInvoiceDate, pendingSupplierName, &attachError);
                        if (!pdfSource.trimmed().isEmpty()) {
                            sourceForStorage = pdfSource;
                        } else {
                            m_dirtyPartRows.insert(row);
                            if (attachError.trimmed().isEmpty()) attachError = QString::fromUtf8("Вкладення накладної не збережено: спочатку підготуйте PDF через імпорт накладної.");
                            if (errorText) *errorText = attachError;
                            else QMessageBox::warning(this, QString::fromUtf8("Накладна"), attachError);
                            return false;
                        }
                    }
                    const QString storedPath = copiedAttachmentPath(m_orderNumber, QStringLiteral("parts"), effectivePartId, sourceForStorage, &attachError);
                    const bool metadataSaved = !storedPath.trimmed().isEmpty()
                        && WorkPartsMutationService::setPartAttachment(effectivePartId, storedPath, QFileInfo(sourceForStorage).fileName(), pendingInvoiceNumber, pendingInvoiceDate, &attachError, importedInvoiceRow,
                            importedInvoiceRow && okPrice ? safeQty*typedPrice : -1);
                    if (!metadataSaved) {
                        if (!storedPath.trimmed().isEmpty()) removeAttachmentFileIfManaged(storedPath);
                        const QString detail = attachError.trimmed().isEmpty()
                            ? QString::fromUtf8("Не вдалося зберегти вкладення запчастини або metadata вкладення.")
                            : attachError;
                        logPartCrashContext(QStringLiteral("ATTACHMENT_SAVE"), QStringLiteral("attachment_save_failed_dirty_kept"), m_partsTable, row, PartColAttachment, m_orderNumber, orderUidSnapshot, detail);
                        m_dirtyPartRows.insert(row);
                        logWorkPartsDiagnostic(QStringLiteral("attachment_save_fail"),
                                               QStringLiteral("part"),
                                               row,
                                               effectivePartId,
                                               pendingInvoiceNumber,
                                               {{QStringLiteral("error"), detail},
                                                {QStringLiteral("traceId"), importTraceId}});
                        logWorkPartsDiagnostic(QStringLiteral("dirty_state_changed"),
                                               QStringLiteral("part"),
                                               row,
                                               effectivePartId,
                                               pendingInvoiceNumber,
                                               {{QStringLiteral("reason"), QStringLiteral("attachment_save_fail")}});
                        if (errorText) *errorText = detail;
                        else if (!detail.trimmed().isEmpty()) QMessageBox::critical(this, QString::fromUtf8("Помилка"), detail);
                        return false;
                    } else if (attachmentItem) {
                        {
                            const QSignalBlocker blocker(m_partsTable);
                            attachmentItem->setData(RoleAttachmentPath, storedPath);
                            attachmentItem->setData(RoleAttachmentSource, sourceForStorage);
                            attachmentItem->setData(RoleAttachmentName, QFileInfo(sourceForStorage).fileName());
                        }
                        if (!pendingInvoiceNumber.isEmpty() || !pendingInvoiceDate.isEmpty()) {
                            qCDebug(csWorkPartsTraceLog).noquote() << QStringLiteral("PART_DATE_POLICY attachment_invoice_metadata_saved order=") << m_orderNumber
                                              << QStringLiteral(" row=") << row
                                              << QStringLiteral(" partId=") << effectivePartId
                                              << QStringLiteral(" trace=") << importTraceId
                                              << QStringLiteral(" invoiceNumber=") << clippedImportLogText(pendingInvoiceNumber)
                                              << QStringLiteral(" invoiceDate=") << clippedImportLogText(pendingInvoiceDate)
                                              << QStringLiteral(" deliveryDateBackfill=disabled");
                        }
                    }
                }
            }
        }
    }
    if (!m_useExternalSaveTransaction) {
        refreshPartAttachmentCell(row);
        clearRowHintState(m_partsTable, row);
        setRowEditingVisual(m_partsTable, row);
        m_dirtyPartRows.remove(row);
        logWorkPartsDiagnostic(QStringLiteral("dirty_state_changed"), QStringLiteral("part"), row, effectivePartId, pendingInvoiceNumber, {{QStringLiteral("reason"), QStringLiteral("save_row_clear")}});
    } else {
        m_dirtyPartRows.insert(row);
        logWorkPartsDiagnostic(QStringLiteral("dirty_state_changed"), QStringLiteral("part"), row, effectivePartId, pendingInvoiceNumber, {{QStringLiteral("reason"), QStringLiteral("external_transaction_pending")}});
        qCDebug(csWorkPartsTraceLog).noquote() << QStringLiteral("ATTACHMENT_STAGE part_dirty_kept_until_batch_commit order=") << m_orderNumber
                          << QStringLiteral(" row=") << row
                          << QStringLiteral(" partId=") << effectivePartId
                          << QStringLiteral(" deferredAttachment=") << deferredPartAttachmentQueued;
    }
    if (reloadAfterSave) {
        refreshAfterMutation(false, true, true, false);
        focusSection(1);
    }
    logWorkPartsDiagnostic(partId > 0 ? QStringLiteral("row_update_end") : QStringLiteral("row_insert_persist_end"),
                           QStringLiteral("part"),
                           row,
                           effectivePartId,
                           pendingInvoiceNumber,
                           {{QStringLiteral("imported"), importedInvoiceRow},
                            {QStringLiteral("traceId"), importTraceId},
                            {QStringLiteral("reloadAfterSave"), reloadAfterSave}});
    qCDebug(csWorkPartsTraceLog).noquote() << QStringLiteral("IMPORT_PARTS save_done order=") << m_orderNumber
                      << QStringLiteral(" row=") << row
                      << QStringLiteral(" partId=") << effectivePartId
                      << QStringLiteral(" imported=") << importedInvoiceRow
                      << QStringLiteral(" trace=") << importTraceId;
    qCDebug(csWorkPartsTraceLog).noquote() << QStringLiteral("SAVE_DONE")
                      << QStringLiteral(" table=parts")
                      << QStringLiteral(" order=") << m_orderNumber
                      << QStringLiteral(" row=") << row
                      << QStringLiteral(" partId=") << effectivePartId
                      << QStringLiteral(" imported=") << importedInvoiceRow
                      << QStringLiteral(" trace=") << importTraceId;
    if (!m_useExternalSaveTransaction && !m_commitPendingInProgress) {
        publishWorkPartsBackupEvent(QStringLiteral("order.parts"),
                                    QStringLiteral("part"),
                                    QString::number(effectivePartId),
                                    currentOrderUid(),
                                    m_orderNumber,
                                    partId > 0 ? QStringLiteral("update") : QStringLiteral("create"),
                                    partId > 0 ? QString::fromUtf8("Оновлено запчастину") : QString::fromUtf8("Додано запчастину"),
                                    {QStringLiteral("work_order_parts"), QStringLiteral("work_orders")});
    }
    return true;
}

