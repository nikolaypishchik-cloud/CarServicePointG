// P2 architecture migration: implementation module converted from legacy .ipp implementation-fragment decomposition.
// Compiled as a separate P2 implementation module; include-order dependencies are isolated in the module prelude.

// extracted from WorkPartsDialog.cpp as part of p.4 decomposition
#include "ui/tabs/orders/WorkPartsDialogModulePrelude.h"
#include "core/services/orders/PartMarkupPolicyService.h"

void WorkPartsDialog::loadPartsForActiveService()
{
    // P0 freeze closure: synchronous parts reads are allowed only for the
    // explicitly marked draft-restore path. Any accidental UI-thread caller is
    // transparently converted into the coalesced worker refresh pipeline.
    const bool uiThread = qApp && QThread::currentThread() == qApp->thread();
    if (uiThread && !property("csAllowSyncWorkPartsReload").toBool()) {
        RuntimeJob::RuntimeJobDispatcher::warnIfUiThreadBlockingCall(QStringLiteral("WorkPartsDialog::loadPartsForActiveService.sync_fallback_denied"));
        scheduleWorkPartsRefresh(false, true, false, false, QStringLiteral("loadParts_sync_denied"));
        return;
    }
    RuntimeJob::RuntimeJobDispatcher::warnIfUiThreadBlockingCall(QStringLiteral("WorkPartsDialog::loadPartsForActiveService.explicit_sync_restore"));
    const auto rows = WorkPartsReadQueryService::listPartsByInternalNumber(m_orderNumber);
    QList<int> partIds;
    partIds.reserve(rows.size());
    for (const auto& part : rows) {
        if (part.id > 0) partIds.push_back(part.id);
    }
    const auto attachmentsByPartId = WorkPartsReadQueryService::partAttachmentMap(partIds);
    applyPartsSnapshot(rows, attachmentsByPartId);
}

void WorkPartsDialog::applyPartsSnapshot(const QList<WorkOrderPartRow>& rows,
                                         const QHash<int, OrderAttachmentService::AttachmentInfo>& attachmentsByPartId)
{
    if (!m_partsTable) return;
    PerfWatchdog::ScopedTimer perfTimer(QStringLiteral("WorkPartsDialog::applyPartsSnapshot"), 80);
    bumpWorkPartsTableRevision(QStringLiteral("apply_parts_snapshot_begin"));
    logWorkPartsDiagnostic(QStringLiteral("model_rebuild_begin"), QStringLiteral("part"), -1, -1, QString(), {{QStringLiteral("scope"), QStringLiteral("parts")}, {QStringLiteral("source"), QStringLiteral("snapshot")}});
    resetInlineEditorStateForTable(m_partsTable, "apply_parts_snapshot_begin");
    const bool previousUpdatesEnabled = m_partsTable->updatesEnabled();
    const QSignalBlocker partsBlocker(m_partsTable);
    m_loadingParts = true;
    m_partsTable->setProperty("csBulkModelRebuild", true);
    m_partsTable->setUpdatesEnabled(false);
    auto restoreTableState = qScopeGuard([this, previousUpdatesEnabled]() {
        m_loadingParts = false;
        if (!m_partsTable) return;
        m_partsTable->setProperty("csBulkModelRebuild", false);
        m_partsTable->setUpdatesEnabled(previousUpdatesEnabled);
        if (m_partsTable->viewport()) m_partsTable->viewport()->update();
        m_partsTable->update();
    });
    m_partsTable->setRowCount(0);
    m_dirtyPartRows.clear();
    m_partRowById.clear();

    // P0/P1 performance: SQL and attachment IO are outside the UI path. UI only
    // materializes a ready snapshot and grows the backing table once.
    m_partsTable->setRowCount(rows.size());
    int visiblePartNumber = 1;
    int row = 0;
    for (const auto& part : rows) {
        indexPartRow(part.id, row);
        m_partsTable->setItem(row, PartColIndex, makePartIndexItem(QString::number(visiblePartNumber++)));

        auto* deliveredItem = makePartDeliveredItem(!part.installedAt.trimmed().isEmpty(), part.id, true);
        deliveredItem->setData(RoleDeliveryDate, part.installedAt);
        m_partsTable->setItem(row, PartColDelivered, deliveredItem);

        auto* dateItem = makeTextItem(displayDateFromStored(part.orderedAt));
        dateItem->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        dateItem->setData(RoleId, part.id);
        m_partsTable->setItem(row, PartColOrderedAt, dateItem);

        auto* oemItem = makeTextItem(part.partNumber);
        oemItem->setData(RoleId, part.id);
        m_partsTable->setItem(row, PartColOem, oemItem);

        auto* nameItem = makeTextItem(part.name);
        nameItem->setData(RoleId, part.id);
        nameItem->setData(RolePartManufacturer, part.manufacturer);
        m_partsTable->setItem(row, PartColName, nameItem);

        auto* qtyItem = makeTextItem(FormatUtils::smartQty(part.qty, 2));
        qtyItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        qtyItem->setData(RoleId, part.id);
        m_partsTable->setItem(row, PartColQty, qtyItem);

        auto* priceItem = makeTextItem(FormatUtils::money(part.clientPrice, 2));
        priceItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        priceItem->setData(RoleId, part.id);
        const double displayPrice = part.clientPrice;
        const double purchasePrice = part.purchasePrice > 0.0 || part.markupSource=="inventory" ? part.purchasePrice : displayPrice;
        priceItem->setData(RolePurchasePrice, purchasePrice);
        priceItem->setData(RoleMarkupPercent, part.manualMarkupOverride ? part.markupPercent : 0.0);
        priceItem->setData(RoleManualMarkupOverride, part.manualMarkupOverride);
        priceItem->setToolTip(QString::fromUtf8("%1: %2 %. Закупівля: %3 грн. Клієнту: %4 грн.")
            .arg(part.manualMarkupOverride ? QString::fromUtf8("Індивідуальна націнка") : QString::fromUtf8("Глобальна націнка"))
            .arg(part.markupPercent, 0, 'f', 1)
            .arg(purchasePrice, 0, 'f', 2)
            .arg(displayPrice, 0, 'f', 2));
        m_partsTable->setItem(row, PartColPrice, priceItem);

        auto* amountItem = makeReadOnlyTextItem(FormatUtils::money(part.clientAmount, 2), Qt::AlignRight | Qt::AlignVCenter);
        amountItem->setData(RoleId, part.id);
        amountItem->setFlags(amountItem->flags() & ~Qt::ItemIsEditable);
        m_partsTable->setItem(row, PartColAmount, amountItem);

        auto* storeItem = makeTextItem(part.store);
        storeItem->setData(RoleId, part.id);
        m_partsTable->setItem(row, PartColStore, storeItem);
        if (PartMarkupPolicyService::isCustomerSupplied(part.store)) {
            priceItem->setText(QString());
            amountItem->setText(QString());
            priceItem->setData(RolePurchasePrice, 0.0);
            priceItem->setToolTip(QString::fromUtf8("Запчастина клієнта. Без вартості."));
        }

        auto* brandItem = makeTextItem(part.manufacturer);
        brandItem->setData(RoleId, part.id);
        m_partsTable->setItem(row, PartColBrand, brandItem);

        const auto attachment = attachmentsByPartId.value(part.id);
        const QString invoiceNumber = normalizeInvoiceDisplayNumber(attachment.invoiceNumber);
        const QString invoiceDate = attachment.invoiceDate.trimmed();
        const QString attachmentPath = attachment.filePath.trimmed();

        auto* invoiceItem = makeInvoiceNumberItem(invoiceNumber, attachmentPath, invoiceDate);
        invoiceItem->setData(RoleId, part.id);
        invoiceItem->setData(RoleAttachmentPath, attachmentPath);
        invoiceItem->setData(RoleAttachmentDocumentNumber, invoiceNumber);
        invoiceItem->setData(RoleAttachmentDocumentDate, invoiceDate);
        invoiceItem->setToolTip(attachmentToolTipText(attachmentPath, invoiceNumber, invoiceDate));
        m_partsTable->setItem(row, PartColInvoiceNumber, invoiceItem);

        auto* attachmentItem = makeAttachmentItem(attachmentPath, attachment.displayName);
        const bool hasAttachment = !attachmentPath.isEmpty() || !invoiceNumber.isEmpty();
        attachmentItem->setText(hasAttachment ? QString::fromUtf8("📎") : QString());
        attachmentItem->setData(RoleId, part.id);
        attachmentItem->setData(RoleAttachmentPath, attachmentPath);
        attachmentItem->setData(RoleAttachmentName, attachment.displayName);
        attachmentItem->setData(RoleAttachmentSource, QString());
        attachmentItem->setData(RoleAttachmentDocumentNumber, invoiceNumber);
        attachmentItem->setData(RoleAttachmentDocumentDate, invoiceDate);
        attachmentItem->setToolTip(attachmentToolTipText(attachmentPath, invoiceNumber, invoiceDate));
        attachmentItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        attachmentItem->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        m_partsTable->setItem(row, PartColAttachment, attachmentItem);

        syncPartManufacturerRoleFromRow(m_partsTable, row);
        syncPartDeliveredCellState(m_partsTable, row);
        ++row;
    }
    restoreTableState.dismiss();
    m_loadingParts = false;
    m_partsTable->setProperty("csBulkModelRebuild", false);
    m_partsTable->setUpdatesEnabled(previousUpdatesEnabled);
    if (m_partsTable->viewport()) m_partsTable->viewport()->update();
    synchronizeAllPairedRowHeights();
    repositionInputRows();
    logWorkPartsDiagnostic(QStringLiteral("model_rebuild_end"), QStringLiteral("part"), -1, -1, QString(), {{QStringLiteral("scope"), QStringLiteral("parts")}, {QStringLiteral("rowCount"), m_partsTable ? m_partsTable->rowCount() : 0}, {QStringLiteral("source"), QStringLiteral("snapshot")}});
}
