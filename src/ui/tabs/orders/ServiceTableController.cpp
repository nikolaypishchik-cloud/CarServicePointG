// P2 architecture migration: implementation module converted from legacy .ipp implementation-fragment decomposition.
// Compiled as a separate P2 implementation module; include-order dependencies are isolated in the module prelude.

// extracted from WorkPartsDialog.cpp as part of p.4 decomposition
#include "ui/tabs/orders/WorkPartsDialogModulePrelude.h"

void WorkPartsDialog::loadServices()
{
    // P0 freeze closure: synchronous service reads are allowed only for the
    // explicitly marked draft-restore path. Any accidental UI-thread caller is
    // transparently converted into the coalesced worker refresh pipeline.
    const bool uiThread = qApp && QThread::currentThread() == qApp->thread();
    if (uiThread && !property("csAllowSyncWorkPartsReload").toBool()) {
        RuntimeJob::RuntimeJobDispatcher::warnIfUiThreadBlockingCall(QStringLiteral("WorkPartsDialog::loadServices.sync_fallback_denied"));
        scheduleWorkPartsRefresh(true, false, false, true, QStringLiteral("loadServices_sync_denied"));
        return;
    }
    RuntimeJob::RuntimeJobDispatcher::warnIfUiThreadBlockingCall(QStringLiteral("WorkPartsDialog::loadServices.explicit_sync_restore"));
    const QString primaryMaster = currentPrimaryMasterName(m_masterCombo);
    const auto outsourceDisplayNames = loadOutsourceServiceDisplayNames(m_orderNumber);
    auto rows = WorkPartsReadQueryService::listServicesByInternalNumber(m_orderNumber);
    std::stable_sort(rows.begin(), rows.end(), [&outsourceDisplayNames](const WorkOrderServiceRow& a, const WorkOrderServiceRow& b) {
        const bool aOutsource = outsourceDisplayNames.contains(a.id);
        const bool bOutsource = outsourceDisplayNames.contains(b.id);
        if (aOutsource != bOutsource) return !aOutsource && bOutsource;
        return a.id < b.id;
    });
    QList<int> serviceIds;
    serviceIds.reserve(rows.size());
    for (const auto& service : rows) {
        if (service.id > 0) serviceIds.push_back(service.id);
    }
    const auto attachmentsByServiceId = WorkPartsReadQueryService::serviceAttachmentMap(serviceIds);
    applyServicesSnapshot(rows, outsourceDisplayNames, attachmentsByServiceId, primaryMaster);
}

void WorkPartsDialog::applyServicesSnapshot(const QList<WorkOrderServiceRow>& rows,
                                            const QHash<int, QString>& outsourceDisplayNames,
                                            const QHash<int, OrderAttachmentService::AttachmentInfo>& attachmentsByServiceId,
                                            const QString& primaryMaster)
{
    if (!m_servicesTable) return;
    bumpWorkPartsTableRevision(QStringLiteral("apply_services_snapshot_begin"));
    logWorkPartsDiagnostic(QStringLiteral("model_rebuild_begin"), QStringLiteral("service"), -1, -1, QString(), {{QStringLiteral("scope"), QStringLiteral("services")}, {QStringLiteral("source"), QStringLiteral("snapshot")}});
    resetInlineEditorStateForTable(m_servicesTable, "apply_services_snapshot_begin");
    m_syncingServices = true;
    const bool previousUpdatesEnabled = m_servicesTable->updatesEnabled();
    const QSignalBlocker serviceBlocker(m_servicesTable);
    m_servicesTable->setUpdatesEnabled(false);
    auto restoreServiceTableState = qScopeGuard([this, previousUpdatesEnabled]() {
        m_syncingServices = false;
        if (!m_servicesTable) return;
        m_servicesTable->setUpdatesEnabled(previousUpdatesEnabled);
        if (m_servicesTable->viewport()) m_servicesTable->viewport()->update();
        m_servicesTable->update();
    });

    m_servicesTable->setRowCount(0);
    m_dirtyServiceRows.clear();
    m_serviceRowById.clear();

    // P0/P1 performance: the expensive SQL and attachment lookup are already
    // finished by the worker. UI only materializes the ready snapshot and grows
    // the table model once instead of insertRow() per service.
    m_servicesTable->setRowCount(rows.size());
    int visibleServiceNumber = 1;
    int row = 0;
    for (const auto& service : rows) {
        QString master = outsourceDisplayNames.value(service.id).trimmed();
        if (master.isEmpty()) master = service.masterName.trimmed();
        if (master.isEmpty()) master = primaryMaster;
        auto* titleItem = makeTextItem(service.title);
        titleItem->setData(RoleId, service.id);
        indexServiceRow(service.id, row);
        titleItem->setData(RoleIsHint, false);
        auto* priceItem = makeTextItem(moneyText(service.price));
        priceItem->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        auto* qtyItem = makeTextItem(FormatUtils::smartQty(service.qty > 0.0 ? service.qty : 1.0, 2));
        qtyItem->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        auto* masterItem = makeTextItem(master);
        m_servicesTable->setItem(row, ServiceColIndex, makePartIndexItem(QString::number(visibleServiceNumber++)));
        m_servicesTable->setItem(row, ServiceColTitle, titleItem);
        m_servicesTable->setItem(row, ServiceColPrice, priceItem);
        m_servicesTable->setItem(row, ServiceColQty, qtyItem);
        m_servicesTable->setItem(row, ServiceColMaster, masterItem);

        const auto attachment = attachmentsByServiceId.value(service.id);
        auto* attachmentItem = makeAttachmentItem(attachment.filePath, attachment.displayName);
        const bool hasAttachment = !attachment.filePath.trimmed().isEmpty();
        const bool showAttachmentShortcut = hasAttachment || rowHasMeaningfulServiceData(m_servicesTable, row);
        attachmentItem->setText(showAttachmentShortcut ? QString::fromUtf8("📎") : QString());
        attachmentItem->setData(RoleAttachmentPath, attachment.filePath);
        attachmentItem->setData(RoleAttachmentName, attachment.displayName);
        attachmentItem->setData(RoleAttachmentSource, QString());
        attachmentItem->setToolTip(hasAttachment ? QString::fromUtf8("Натисніть, щоб переглянути фото")
                                                : QString::fromUtf8("Натисніть, щоб додати фото"));
        attachmentItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        attachmentItem->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        m_servicesTable->setItem(row, ServiceColAttachment, attachmentItem);

        applyLeftAlignmentToServiceRow(m_servicesTable, row);
        ++row;
    }
    restoreServiceTableState.dismiss();
    m_syncingServices = false;
    m_servicesTable->setUpdatesEnabled(previousUpdatesEnabled);
    if (m_servicesTable->viewport()) m_servicesTable->viewport()->update();
    repositionInputRows();
    synchronizeAllPairedRowHeights();
    syncServiceVisualSelection();
    logWorkPartsDiagnostic(QStringLiteral("model_rebuild_end"), QStringLiteral("service"), -1, -1, QString(), {{QStringLiteral("scope"), QStringLiteral("services")}, {QStringLiteral("rowCount"), m_servicesTable ? m_servicesTable->rowCount() : 0}, {QStringLiteral("source"), QStringLiteral("snapshot")}});
}

void WorkPartsDialog::ensureServiceSelected()
{
    if (m_activeServiceId > 0) {
        syncServiceVisualSelection();
        return;
    }
    if (!m_serviceRowById.isEmpty()) {
        int firstRow = m_servicesTable ? m_servicesTable->rowCount() : 2147483647;
        int firstId = 0;
        for (auto it = m_serviceRowById.constBegin(); it != m_serviceRowById.constEnd(); ++it) {
            if (it.value() >= 0 && it.value() < firstRow) {
                firstRow = it.value();
                firstId = it.key();
            }
        }
        if (firstId > 0) m_activeServiceId = firstId;
    }
    if (m_activeServiceId <= 0) {
        for (int row = 0; row < m_servicesTable->rowCount(); ++row) {
            auto* item = m_servicesTable->item(row, ServiceColTitle);
            if (!item || item->data(RoleIsHint).toBool()) continue;
            m_activeServiceId = item->data(RoleId).toInt();
            break;
        }
    }
    syncServiceVisualSelection();
}

void WorkPartsDialog::setActiveServiceId(int serviceId)
{
    m_activeServiceId = serviceId;
    syncServiceVisualSelection();
}

void WorkPartsDialog::syncServiceVisualSelection()
{
    if (!m_servicesTable) return;

    const QVector<int> navColumns = logicalNavigationColumns(m_servicesTable);
    int preferredColumn = m_servicesTable->currentColumn();
    if (!navColumns.contains(preferredColumn)) {
        preferredColumn = ServiceColTitle;
    }

    int row = serviceRowById(m_activeServiceId);
    if (row < 0) {
        for (int candidate = 0; candidate < m_servicesTable->rowCount(); ++candidate) {
            auto* item = m_servicesTable->item(candidate, ServiceColTitle);
            if (!item || item->data(RoleIsHint).toBool()) continue;
            if (item->data(RoleId).toInt() == m_activeServiceId) {
                row = candidate;
                indexServiceRow(m_activeServiceId, row);
                break;
            }
        }
    }
    if (row >= 0) {
        if (m_servicesTable->currentRow() != row || m_servicesTable->currentColumn() != preferredColumn) {
            const QSignalBlocker blocker(m_servicesTable);
            m_servicesTable->setCurrentCell(row, preferredColumn);
        }
        if (auto* item = m_servicesTable->item(row, ServiceColTitle)) {
            m_servicesTable->scrollToItem(item, QAbstractItemView::EnsureVisible);
        }
    }
}
