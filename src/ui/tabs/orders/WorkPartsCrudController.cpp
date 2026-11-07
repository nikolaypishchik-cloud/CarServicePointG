// P2 architecture migration: implementation module converted from legacy .ipp implementation-fragment decomposition.
// Compiled as a separate P2 implementation module; include-order dependencies are isolated in the module prelude.

#include "ui/tabs/orders/WorkPartsDialogModulePrelude.h"

#include "ui/tabs/orders/WorkPartsDialog.h"

#include "core/services/orders/PartRowPayloadBuilderService.h"
#include "core/services/orders/PartMarkupPolicyService.h"

bool WorkPartsDialog::insertService(const QString& title, const QString& prefix)
{
    const QString finalTitle = normalizeSentence(prefix + title);
    if (finalTitle.isEmpty()) return false;

    WorkOrderServiceRow row;
    row.title = finalTitle;
    row.qty = 1.0;
    row.price = 0.0;
    row.amount = 0.0;

    QString mutationError;
    int newServiceId = 0;
    if (!OrderMutationFacade::insertServiceWithOrderMaster(m_orderNumber, row, &newServiceId, &mutationError)) {
        if (!mutationError.trimmed().isEmpty()) QMessageBox::critical(this, QString::fromUtf8("Помилка"), mutationError);
        return false;
    }
    m_activeServiceId = newServiceId;
    refreshAfterMutation(true, true, true, true);
    publishWorkPartsBackupEvent(QStringLiteral("order.services"),
                                QStringLiteral("service"),
                                QString::number(newServiceId),
                                currentOrderUid(),
                                m_orderNumber,
                                QStringLiteral("create"),
                                QString::fromUtf8("Додано роботу"),
                                {QStringLiteral("work_order_services"), QStringLiteral("service_masters"),
                                 QStringLiteral("work_order_parts"), QStringLiteral("work_orders")});
    return true;
}

bool WorkPartsDialog::setServiceMaster(const QString& masterId, const QString& masterName)
{
    if (activeServiceId() <= 0 || masterId.isEmpty()) return false;
    QString mutationError;
    if (!OrderMutationFacade::setServiceMaster(m_orderNumber, activeServiceId(), masterId, masterName, &mutationError)) {
        if (!mutationError.trimmed().isEmpty()) QMessageBox::critical(this, QString::fromUtf8("Помилка"), mutationError);
        return false;
    }
    refreshAfterMutation(true, false, false, false);
    publishWorkPartsBackupEvent(QStringLiteral("order.services"),
                                QStringLiteral("service"),
                                QString::number(activeServiceId()),
                                currentOrderUid(),
                                m_orderNumber,
                                QStringLiteral("update_master"),
                                QString::fromUtf8("Оновлено майстра роботи"),
                                {QStringLiteral("service_masters"), QStringLiteral("work_order_services")});
    return true;
}

bool WorkPartsDialog::setServicePrice(double price)
{
    if (activeServiceId() <= 0) return false;
    QString mutationError;
    if (!OrderMutationFacade::setServicePrice(m_orderNumber, activeServiceId(), price, &mutationError)) {
        if (!mutationError.trimmed().isEmpty()) QMessageBox::critical(this, QString::fromUtf8("Помилка"), mutationError);
        return false;
    }
    refreshAfterMutation(true, false, true, false);
    publishWorkPartsBackupEvent(QStringLiteral("order.services"),
                                QStringLiteral("service"),
                                QString::number(activeServiceId()),
                                currentOrderUid(),
                                m_orderNumber,
                                QStringLiteral("update_price"),
                                QString::fromUtf8("Оновлено ціну роботи"),
                                {QStringLiteral("work_order_services")});
    return true;
}

bool WorkPartsDialog::clearServiceMaster()
{
    if (activeServiceId() <= 0) return false;
    const bool ok = OrderMutationFacade::clearServiceMaster(m_orderNumber, activeServiceId(), nullptr);
    if (ok) {
        refreshAfterMutation(true, false, false, false);
        publishWorkPartsBackupEvent(QStringLiteral("order.services"),
                                    QStringLiteral("service"),
                                    QString::number(activeServiceId()),
                                    currentOrderUid(),
                                    m_orderNumber,
                                    QStringLiteral("clear_master"),
                                    QString::fromUtf8("Очищено майстра роботи"),
                                    {QStringLiteral("service_masters")});
    }
    return ok;
}

bool WorkPartsDialog::deleteActiveService()
{
    if (!m_servicesTable) return false;
    const int originalRow = m_servicesTable->currentRow();
    if (originalRow < 0) return false;
    auto* originalItem = m_servicesTable->item(originalRow, ServiceColTitle);
    if (!originalItem || originalItem->data(RoleIsHint).toBool()) return false;
    const int serviceId = originalItem->data(RoleId).toInt();
    const QString title = originalItem->text().trimmed();

    logWorkPartsDiagnostic(QStringLiteral("row_delete_begin"),
                           QStringLiteral("service"),
                           originalRow,
                           serviceId,
                           QString(),
                           {{QStringLiteral("title"), title}});

    QString pendingError;
    if (!commitPendingEdits(&pendingError)) {
        QMessageBox::critical(this, QString::fromUtf8("Помилка"), pendingError);
        return false;
    }

    const auto r = QMessageBox::question(this, QString::fromUtf8("Підтвердження"),
                                         QString::fromUtf8("Видалити роботу?\n%1").arg(title),
                                         QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (r != QMessageBox::Yes) return false;

    if (serviceId <= 0) {
        if (originalRow >= 0 && originalRow < m_servicesTable->rowCount()) {
            m_servicesTable->removeRow(originalRow);
            bumpWorkPartsTableRevision(QStringLiteral("delete_unsaved_service_row"));
            logWorkPartsDiagnostic(QStringLiteral("row_delete_end"), QStringLiteral("service"), originalRow, serviceId, QString(), {{QStringLiteral("scope"), QStringLiteral("unsaved")}});
            QSet<int> shifted;
            const auto dirtyRows = m_dirtyServiceRows;
            for (int dirtyRow : dirtyRows) {
                if (dirtyRow == originalRow) continue;
                shifted.insert(dirtyRow > originalRow ? dirtyRow - 1 : dirtyRow);
            }
            m_dirtyServiceRows = shifted;
            renumberServiceItems();
            repositionInputRows();
            m_userTouchedPendingContent = true;
            notifyDraftDirtyChanged();
            return true;
        }
        return false;
    }

    QString mutationError;
    if (!OrderMutationFacade::deleteService(m_orderNumber, serviceId, &mutationError)) {
        if (!mutationError.trimmed().isEmpty()) QMessageBox::critical(this, QString::fromUtf8("Помилка"), mutationError);
        return false;
    }

    m_activeServiceId = 0;
    autoAssignPartsToServices();
    refreshAfterMutation(true, true, true, true);
    logWorkPartsDiagnostic(QStringLiteral("row_delete_end"), QStringLiteral("service"), originalRow, serviceId, QString(), {{QStringLiteral("scope"), QStringLiteral("persisted")}});
    publishWorkPartsBackupEvent(QStringLiteral("order.services"),
                                QStringLiteral("service"),
                                QString::number(serviceId),
                                currentOrderUid(),
                                m_orderNumber,
                                QStringLiteral("delete"),
                                QString::fromUtf8("Видалено роботу"),
                                {QStringLiteral("work_order_services"), QStringLiteral("service_masters"), QStringLiteral("work_orders")});
    return true;
}

bool WorkPartsDialog::deleteActivePart()
{
    if (!m_partsTable) return false;
    BackupPerformanceGuard::hold(AppContext::instance().backupManager(), 2200);
    flushInlineEditorValueToModel(m_partsTable);
    if (QWidget* fw = focusWidget()) fw->clearFocus();
    flushInlineEditorValueToModel(m_partsTable);
    const int currentRow = m_partsTable->currentRow();
    if (currentRow < 0) return false;
    auto* currentNameItem = m_partsTable->item(currentRow, PartColName);
    if (!currentNameItem) return false;
    const int targetPartId = currentNameItem->data(RoleId).toInt();
    const QString targetName = currentNameItem->text().trimmed();

    logWorkPartsDiagnostic(QStringLiteral("row_delete_begin"),
                           QStringLiteral("part"),
                           currentRow,
                           targetPartId,
                           QString(),
                           {{QStringLiteral("name"), targetName}});

    const auto r = QMessageBox::question(this,
                                         QString::fromUtf8("Підтвердження"),
                                         QString::fromUtf8("Видалити запчастину?\n%1").arg(targetName),
                                         QMessageBox::Yes | QMessageBox::No,
                                         QMessageBox::No);
    if (r != QMessageBox::Yes) return false;

    if (targetPartId <= 0) {
        QPointer<WorkPartsDialog> guard(this);
        const int rowToRemove = currentRow;
        QTimer::singleShot(0, this, [guard, rowToRemove]() {
            if (!guard || !guard->m_partsTable) return;
            if (rowToRemove < 0 || rowToRemove >= guard->m_partsTable->rowCount()) return;

            guard->m_partsTable->setProperty("inlineEditorActive", false);
        clearInlineEditingIndex(guard->m_partsTable);

            QSignalBlocker tableBlocker(guard->m_partsTable);
            const QSignalBlocker modelBlocker(guard->m_partsTable->model());
            if (auto* selectionModel = guard->m_partsTable->selectionModel()) {
                selectionModel->clearSelection();
                selectionModel->setCurrentIndex(QModelIndex(), QItemSelectionModel::NoUpdate);
            }
            guard->m_partsTable->clearFocus();
            guard->m_partsTable->removeRow(rowToRemove);
            guard->bumpWorkPartsTableRevision(QStringLiteral("delete_unsaved_part_row"));
            guard->logWorkPartsDiagnostic(QStringLiteral("row_delete_end"), QStringLiteral("part"), rowToRemove, 0, QString(), {{QStringLiteral("scope"), QStringLiteral("unsaved")}});
            QSet<int> shifted;
            const auto dirtyRows = guard->m_dirtyPartRows;
            for (int dirtyRow : dirtyRows) {
                if (dirtyRow == rowToRemove) continue;
                shifted.insert(dirtyRow > rowToRemove ? dirtyRow - 1 : dirtyRow);
            }
            guard->m_dirtyPartRows = shifted;
            guard->repositionInputRows();
            guard->renumberPartItems();
            guard->loadHeader();
            guard->m_userTouchedPendingContent = true;
            guard->notifyDraftDirtyChanged();
        });
        return true;
    }

    QString mutationError;
    if (!OrderMutationFacade::deletePart(m_orderNumber, targetPartId, &mutationError)) {
        if (!mutationError.trimmed().isEmpty()) QMessageBox::critical(this, QString::fromUtf8("Помилка"), mutationError);
        return false;
    }

    const QString orderUidSnapshot = currentOrderUid();
    QPointer<WorkPartsDialog> guard(this);
    QTimer::singleShot(0, this, [guard, targetPartId, orderUidSnapshot]() {
        if (!guard || !guard->m_partsTable) return;

        guard->m_partsTable->setProperty("inlineEditorActive", false);
        clearInlineEditingIndex(guard->m_partsTable);
        if (auto* selectionModel = guard->m_partsTable->selectionModel()) {
            selectionModel->clearSelection();
            selectionModel->setCurrentIndex(QModelIndex(), QItemSelectionModel::NoUpdate);
        }
        guard->m_partsTable->clearFocus();
        guard->refreshAfterMutation(false, true, true, false);
        guard->bumpWorkPartsTableRevision(QStringLiteral("delete_persisted_part_row"));
        guard->logWorkPartsDiagnostic(QStringLiteral("row_delete_end"), QStringLiteral("part"), -1, targetPartId, QString(), {{QStringLiteral("scope"), QStringLiteral("persisted")}});
        publishWorkPartsBackupEvent(QStringLiteral("order.parts"),
                                           QStringLiteral("part"),
                                           QString::number(targetPartId),
                                           orderUidSnapshot,
                                           guard->m_orderNumber,
                                           QStringLiteral("delete"),
                                           QString::fromUtf8("Видалено запчастину"),
                                           {QStringLiteral("work_order_parts"), QStringLiteral("work_orders")});
    });
    return true;
}

bool WorkPartsDialog::deleteSelectedServices()
{
    QString pendingError;
    if (!commitPendingEdits(&pendingError)) {
        QMessageBox::critical(this, QString::fromUtf8("Помилка"), pendingError);
        return false;
    }
    if (!m_servicesTable || !m_servicesTable->selectionModel()) return false;
    const QModelIndexList rows = m_servicesTable->selectionModel()->selectedRows();
    QList<int> serviceIds;
    QStringList titles;
    for (const QModelIndex& idx : rows) {
        auto* item = m_servicesTable->item(idx.row(), ServiceColTitle);
        if (!item || item->data(RoleIsHint).toBool()) continue;
        const int serviceId = item->data(RoleId).toInt();
        if (serviceId <= 0 || serviceIds.contains(serviceId)) continue;
        serviceIds.append(serviceId);
        titles.append(item->text().trimmed());
    }
    if (serviceIds.isEmpty()) return deleteActiveService();

    logWorkPartsDiagnostic(QStringLiteral("row_delete_batch_begin"),
                           QStringLiteral("service"),
                           -1,
                           -1,
                           QString(),
                           {{QStringLiteral("count"), serviceIds.size()}});
    const QString question = serviceIds.size() > 1
        ? QString::fromUtf8("Видалити вибрані роботи (%1)?").arg(serviceIds.size())
        : QString::fromUtf8("Видалити роботу?\n%1").arg(titles.value(0));
    if (QMessageBox::question(this, QString::fromUtf8("Підтвердження"), question,
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) {
        return false;
    }

    QString mutationError;
    if (!OrderMutationFacade::deleteServices(m_orderNumber, serviceIds, &mutationError)) {
        if (!mutationError.trimmed().isEmpty()) {
            QMessageBox::critical(this, QString::fromUtf8("Помилка"), mutationError);
        }
        return false;
    }
    m_activeServiceId = 0;
    autoAssignPartsToServices();
    refreshAfterMutation(true, true, true, true);
    logWorkPartsDiagnostic(QStringLiteral("row_delete_batch_end"),
                           QStringLiteral("service"),
                           -1,
                           -1,
                           QString(),
                           {{QStringLiteral("count"), serviceIds.size()}});
    publishWorkPartsBackupEvent(QStringLiteral("order.services"),
                                QStringLiteral("service_batch"),
                                QString::number(serviceIds.size()),
                                currentOrderUid(),
                                m_orderNumber,
                                QStringLiteral("delete_batch"),
                                QString::fromUtf8("Видалено вибрані роботи"),
                                {QStringLiteral("work_order_services"), QStringLiteral("service_masters"), QStringLiteral("work_orders")});
    return true;
}

bool WorkPartsDialog::deleteSelectedParts()
{
    if (!m_partsTable || !m_partsTable->selectionModel()) return false;
    flushInlineEditorValueToModel(m_partsTable);

    const QModelIndexList selectedRows = m_partsTable->selectionModel()->selectedRows();
    QList<int> partIds;
    QStringList names;
    for (const QModelIndex& idx : selectedRows) {
        auto* item = m_partsTable->item(idx.row(), PartColName);
        if (!item) continue;
        const int partId = item->data(RoleId).toInt();
        if (partId <= 0 || partIds.contains(partId)) continue;
        partIds.append(partId);
        names.append(item->text().trimmed());
    }

    if (partIds.isEmpty()) {
        const int currentRow = m_partsTable->currentRow();
        if (currentRow >= 0) {
            auto* item = m_partsTable->item(currentRow, PartColName);
            if (item) {
                const int partId = item->data(RoleId).toInt();
                if (partId > 0) {
                    partIds.append(partId);
                    names.append(item->text().trimmed());
                }
            }
        }
    }
    if (partIds.isEmpty()) return false;

    logWorkPartsDiagnostic(QStringLiteral("row_delete_batch_begin"),
                           QStringLiteral("part"),
                           -1,
                           -1,
                           QString(),
                           {{QStringLiteral("count"), partIds.size()}});
    flushInlineEditorValueToModel(m_partsTable);

    const QString question = partIds.size() > 1
        ? QString::fromUtf8("Видалити вибрані запчастини (%1)?").arg(partIds.size())
        : QString::fromUtf8("Видалити запчастину?\n%1").arg(names.value(0));
    if (QMessageBox::question(this, QString::fromUtf8("Підтвердження"), question,
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) {
        return false;
    }

    QString repoError;
    for (int partId : partIds) {
        if (!WorkPartsMutationService::removePart(m_orderNumber, partId, &repoError)) {
            if (!repoError.trimmed().isEmpty()) {
                QMessageBox::critical(this, QString::fromUtf8("Помилка"), repoError);
            }
            return false;
        }
    }
    refreshAfterMutation(false, true, true, false);
    logWorkPartsDiagnostic(QStringLiteral("row_delete_batch_end"),
                           QStringLiteral("part"),
                           -1,
                           -1,
                           QString(),
                           {{QStringLiteral("count"), partIds.size()}});
    publishWorkPartsBackupEvent(QStringLiteral("order.parts"),
                                QStringLiteral("part_batch"),
                                QString::number(partIds.size()),
                                currentOrderUid(),
                                m_orderNumber,
                                QStringLiteral("delete_batch"),
                                QString::fromUtf8("Видалено вибрані запчастини"),
                                {QStringLiteral("work_order_parts"), QStringLiteral("work_orders")});
    return true;
}

void WorkPartsDialog::openAddServicesDialog()
{
    QString pendingError;
    if (!commitPendingEdits(&pendingError)) {
        QMessageBox::critical(this, QString::fromUtf8("Помилка"), pendingError);
        return;
    }
    QList<QStringList> existing;
    const QString existingOrderUid = currentOrderUid();
    if (existingOrderUid.isEmpty()) return;
    for (const auto& row : WorkPartsQueryService::loadServiceRowsForOrder(existingOrderUid)) {
        existing << (QStringList() << row.title << FormatUtils::smartQty(row.price, 2) << FormatUtils::smartQty(row.qty, 2) << row.masterName);
    }

    const auto rows = runBatchEditor(this, existing.isEmpty() ? QString::fromUtf8("Додати нову роботу")
                                                            : QString::fromUtf8("Додати / редагувати роботи"),
                                     {QString::fromUtf8("Робота"), QString::fromUtf8("Вартість виконання робіт"), QString::fromUtf8("Кіл-ть"), QString::fromUtf8("Майстер")},
                                     existing, QStringLiteral("orders.batch.services.add.header"));
    if (rows.isEmpty() && !existing.isEmpty()) return;
    if (rows.isEmpty()) return;

    QList<WorkOrderServiceRow> serviceRows;
    for (const auto& row : rows) {
        const QString title = canonicalizeServiceTitle(row.value(0));
        if (title.trimmed().isEmpty()) continue;

        bool okPrice = false, okQty = false;
        const double price = parseLooseNumber(row.value(1).trimmed(), &okPrice);
        const double qty = parseLooseNumber(row.value(2).trimmed(), &okQty);

        WorkOrderServiceRow serviceRow;
        serviceRow.title = title;
        serviceRow.qty = okQty && qty > 0.0 ? qty : 1.0;
        serviceRow.price = okPrice ? FormatUtils::roundMoneyValue(qMax(0.0, price)) : 0.0;
        serviceRow.amount = FormatUtils::roundMoneyValue(serviceRow.qty * serviceRow.price);

        const auto masters = PersonnelSettingsService::loadPersonnel(true);
        QMap<QString, MasterRow> mastersByName;
        for (const auto& m : masters) mastersByName.insert(m.name.trimmed().toLower(), m);
        const QString primaryMasterId = m_masterCombo ? m_masterCombo->currentData().toString() : QString();
        const QString primaryMasterName = currentPrimaryMasterName(m_masterCombo);

        serviceRow.masterId = primaryMasterId;
        serviceRow.masterName = primaryMasterName;
        const QString enteredMasterName = row.value(3).trimmed();
        if (!enteredMasterName.isEmpty()) {
            const auto it = mastersByName.constFind(enteredMasterName.toLower());
            if (it != mastersByName.cend()) {
                serviceRow.masterId = it->id;
                serviceRow.masterName = it->name;
            } else {
                serviceRow.masterId.clear();
                serviceRow.masterName = enteredMasterName;
            }
        }
        serviceRows << serviceRow;
    }

    const auto replaceResult = ServiceBatchMutationService::replaceRows(m_orderNumber, serviceRows);
    if (!replaceResult.ok) {
        QMessageBox::critical(this, QString::fromUtf8("Помилка"), replaceResult.errorText);
        return;
    }
    m_activeServiceId = replaceResult.lastServiceId;
    refreshAfterMutation(true, false, true, true);
    publishWorkPartsBackupEvent(QStringLiteral("order.services"),
                                QStringLiteral("service_batch"),
                                QString::number(rows.size()),
                                currentOrderUid(),
                                m_orderNumber,
                                existing.isEmpty() ? QStringLiteral("create_batch") : QStringLiteral("replace_batch"),
                                existing.isEmpty()
                                    ? QString::fromUtf8("Додано пакет робіт")
                                    : QString::fromUtf8("Оновлено пакет робіт"),
                                {QStringLiteral("work_order_services"), QStringLiteral("service_masters"), QStringLiteral("work_orders")});
}

void WorkPartsDialog::openEditServicesDialog()
{
    QString pendingError;
    if (!commitPendingEdits(&pendingError)) {
        QMessageBox::critical(this, QString::fromUtf8("Помилка"), pendingError);
        return;
    }
    QList<QStringList> existing;
    const QString existingOrderUid = currentOrderUid();
    if (existingOrderUid.isEmpty()) return;
    for (const auto& row : WorkPartsQueryService::loadServiceRowsForOrder(existingOrderUid)) {
        existing << (QStringList() << row.title << FormatUtils::smartQty(row.price, 2) << FormatUtils::smartQty(row.qty, 2) << row.masterName);
    }

    const auto rows = runBatchEditor(this, QString::fromUtf8("Редагувати роботи"),
                                     {QString::fromUtf8("Робота"), QString::fromUtf8("Вартість виконання робіт"), QString::fromUtf8("Кіл-ть"), QString::fromUtf8("Майстер")}, existing, QStringLiteral("orders.batch.services.edit.header"));
    if (rows.isEmpty() && !existing.isEmpty()) return;

    QList<WorkOrderServiceRow> serviceRows;
    for (const auto& row : rows) {
        const QString title = canonicalizeServiceTitle(row.value(0));
        if (title.trimmed().isEmpty()) continue;

        bool okPrice = false, okQty = false;
        const double price = parseLooseNumber(row.value(1).trimmed(), &okPrice);
        const double qty = parseLooseNumber(row.value(2).trimmed(), &okQty);

        WorkOrderServiceRow serviceRow;
        serviceRow.title = title;
        serviceRow.qty = okQty && qty > 0.0 ? qty : 1.0;
        serviceRow.price = okPrice ? FormatUtils::roundMoneyValue(qMax(0.0, price)) : 0.0;
        serviceRow.amount = FormatUtils::roundMoneyValue(serviceRow.qty * serviceRow.price);

        const auto masters = PersonnelSettingsService::loadPersonnel(true);
        QMap<QString, MasterRow> mastersByName;
        for (const auto& m : masters) mastersByName.insert(m.name.trimmed().toLower(), m);
        const QString primaryMasterId = m_masterCombo ? m_masterCombo->currentData().toString() : QString();
        const QString primaryMasterName = currentPrimaryMasterName(m_masterCombo);

        serviceRow.masterId = primaryMasterId;
        serviceRow.masterName = primaryMasterName;
        const QString enteredMasterName = row.value(3).trimmed();
        if (!enteredMasterName.isEmpty()) {
            const auto it = mastersByName.constFind(enteredMasterName.toLower());
            if (it != mastersByName.cend()) {
                serviceRow.masterId = it->id;
                serviceRow.masterName = it->name;
            } else {
                serviceRow.masterId.clear();
                serviceRow.masterName = enteredMasterName;
            }
        }
        serviceRows << serviceRow;
    }

    const auto replaceResult = ServiceBatchMutationService::replaceRows(m_orderNumber, serviceRows);
    if (!replaceResult.ok) {
        QMessageBox::critical(this, QString::fromUtf8("Помилка"), replaceResult.errorText);
        return;
    }
    m_activeServiceId = replaceResult.lastServiceId;
    refreshAfterMutation(true, false, true, true);
    publishWorkPartsBackupEvent(QStringLiteral("order.services"),
                                QStringLiteral("service_batch"),
                                QString::number(rows.size()),
                                currentOrderUid(),
                                m_orderNumber,
                                QStringLiteral("replace_batch"),
                                QString::fromUtf8("Замінено список робіт"),
                                {QStringLiteral("work_order_services"), QStringLiteral("service_masters"), QStringLiteral("work_orders")});
}

static bool confirmExplicitZeroPartMarkup(QWidget* parent, const QList<QStringList>& rows)
{
    for (const auto& row : rows) {
        const QString raw = row.value(5).trimmed();
        if (raw.isEmpty()) continue;
        bool ok = false;
        const double value = parseLooseNumber(raw, &ok);
        if (ok && qFuzzyIsNull(value)) {
            return QMessageBox::question(parent,
                QString::fromUtf8("Індивідуальна націнка 0%"),
                QString::fromUtf8("У колонці «Націнка %» вказано 0. Це буде збережено як індивідуальна 0%, а не як глобальна націнка. Продовжити?"),
                QMessageBox::Yes | QMessageBox::No,
                QMessageBox::No) == QMessageBox::Yes;
        }
    }
    return true;
}

void WorkPartsDialog::openAddPartsDialog()
{
    QString pendingError;
    if (!commitPendingEdits(&pendingError)) {
        QMessageBox::critical(this, QString::fromUtf8("Помилка"), pendingError);
        return;
    }
    QString routeError;
    if (!ensureOrderReadyForEditing(m_orderNumber, &routeError)) {
        QMessageBox::critical(this,
                              QString::fromUtf8("Помилка при збереженні запчастини"),
                              QString::fromUtf8("Наряд не готовий до редагування запчастин.\n%1").arg(routeError));
        return;
    }

    const auto rows = runBatchEditor(this, QString::fromUtf8("Додати запчастину"),
                                     {QString::fromUtf8("Дата замовлення"), QString::fromUtf8("OEM номер"), QString::fromUtf8("Запчастина"), QString::fromUtf8("Кіл-ть"), QString::fromUtf8("Закупівельна ціна"), QString::fromUtf8("Націнка %"), QString::fromUtf8("Вартість"), QString::fromUtf8("Магазин"), QString::fromUtf8("Замовник")}, {}, QStringLiteral("orders.batch.parts.add.header"));
    if (rows.isEmpty()) return;
    if (!confirmExplicitZeroPartMarkup(this, rows)) return;

    MutationExecutor::Request request;
    request.sourceModule = QStringLiteral("ui.tabs.orders.WorkPartsCrudController");
    request.entityType = QStringLiteral("part_batch");
    request.entityId = QString::number(m_orderNumber);
    request.operation = QStringLiteral("create_batch_parts");
    request.description = QString::fromUtf8("Пакетне додавання запчастин");
    request.changedTables = {QStringLiteral("work_order_parts"), QStringLiteral("work_orders")};
    request.beginErrorFallback = QString::fromUtf8("Не можу відкрити транзакцію БД");
    request.commitErrorFallback = QString::fromUtf8("Не вдалося завершити пакетне додавання запчастин");
    const auto execResult = WorkPartsMutationService::execute(request, [&](auto& db, QString* txError) {
        for (const auto& row : rows) {
            bool okQty = false, okPrice = false;
            const QString orderedAt = storedDateFromDisplay(row.value(0));
            const QString oemNumber = row.value(1).trimmed();
            const QString partName = normalizeSentence(row.value(2));
            const double qty = parseLooseNumber(row.value(3).trimmed(), &okQty);
            const double price = parseLooseNumber(row.value(4).trimmed(), &okPrice);
            const QString markupCellRaw = row.value(5).trimmed();
            bool okMarkup = false;
            const double manualMarkup = markupCellRaw.isEmpty() ? 0.0 : parseLooseNumber(markupCellRaw, &okMarkup);
            const bool hasManualMarkup = !markupCellRaw.isEmpty() && okMarkup && manualMarkup >= 0.0;
            const QString store = normalizeSentence(row.value(7));
            const double safeQty = okQty && qty > 0 ? qty : 1.0;
            const double purchasePrice = okPrice && price >= 0 ? FormatUtils::roundMoneyValue(price) : 0.0;

            PartRowPayloadInput payloadInput;
            payloadInput.orderedAt = orderedAt;
            payloadInput.store = store;
            payloadInput.partNumber = oemNumber;
            payloadInput.name = partName;
            payloadInput.note = QStringLiteral("");
            payloadInput.manufacturer = effectiveOilBrand(partName, store);
            payloadInput.qty = safeQty;
            payloadInput.purchasePrice = purchasePrice;
            payloadInput.sourceType = QStringLiteral("manual_batch");
            payloadInput.manualMarkupOverride = hasManualMarkup;
            payloadInput.manualMarkupPercent = payloadInput.manualMarkupOverride ? qMax(0.0, manualMarkup) : 0.0;
            payloadInput.serviceId = 0;
            if (m_btnCustomerParts->isChecked()) payloadInput.store = PartMarkupPolicyService::customerPartsStore();
            WorkOrderPartRow payload = PartRowPayloadBuilderService::build(payloadInput);
            qCDebug(csWorkPartsTraceLog).noquote() << QStringLiteral("PART_PRICING op=save order=%1 partId=0 trace=manual_batch name=\"%2\" store=\"%3\" qty=%4 purchase=%5 client=%6 activePct=%7 source=%8 override=%9")
                .arg(m_orderNumber)
                .arg(payload.name)
                .arg(payload.store)
                .arg(payload.qty, 0, 'f', 3)
                .arg(payload.purchasePrice, 0, 'f', 2)
                .arg(payload.clientPrice, 0, 'f', 2)
                .arg(payload.markupPercent, 0, 'f', 2)
                .arg(payload.markupSource)
                .arg(payload.manualMarkupOverride ? 1 : 0);

            QString saveError;
            if (!WorkPartsPersistenceRepositoryFacade::insertPart(db, m_orderNumber, payload, nullptr, &saveError)) {
                if (txError) *txError = QString::fromUtf8("Не вдалося додати рядок запчастини.\n%1").arg(saveError);
                return false;
            }
        }
        QString statusError;
        if (!promoteOrderStatusToInProgressIfNeeded(db.connectionName(), m_orderNumber, &statusError)) {
            if (txError) *txError = statusError;
            return false;
        }
        return true;
    });
    if (!execResult.ok) {
        QMessageBox::critical(this,
                              QString::fromUtf8("Помилка при збереженні запчастини"),
                              execResult.errorText);
        return;
    }

    autoAssignPartsToServices();
    refreshAfterMutation(true, true, true, false);
    publishWorkPartsBackupEvent(QStringLiteral("order.parts"),
                                QStringLiteral("part_batch"),
                                QString::number(rows.size()),
                                currentOrderUid(),
                                m_orderNumber,
                                QStringLiteral("create_batch"),
                                QString::fromUtf8("Додано пакет запчастин"),
                                {QStringLiteral("work_order_parts"), QStringLiteral("work_orders")});
}

void WorkPartsDialog::openEditPartsDialog()
{
    QString pendingError;
    if (!commitPendingEdits(&pendingError)) {
        QMessageBox::critical(this, QString::fromUtf8("Помилка"), pendingError);
        return;
    }
    QString routeError;
    if (!ensureOrderReadyForEditing(m_orderNumber, &routeError)) {
        QMessageBox::critical(this,
                              QString::fromUtf8("Помилка при збереженні запчастин"),
                              QString::fromUtf8("Наряд не готовий до редагування запчастин.\n%1").arg(routeError));
        return;
    }

    struct ExistingEditPartSnapshot {
        double purchasePrice = 0.0;
        double clientPrice = 0.0;
        double markupPercent = 0.0;
        bool manualMarkupOverride = false;
        QString markupSource = QStringLiteral("policy");
        QString manufacturer;
    };

    QList<QStringList> existing;
    QList<ExistingEditPartSnapshot> existingSnapshots;
    const QString orderUid = currentOrderUid();
    if (orderUid.isEmpty()) return;
    for (const auto& row : WorkPartsQueryService::loadPartRowsForOrder(orderUid)) {
        ExistingEditPartSnapshot snapshot;
        snapshot.clientPrice = row.price;
        snapshot.purchasePrice = row.purchasePrice > 0.0 ? row.purchasePrice : snapshot.clientPrice;
        snapshot.markupPercent = row.markupPercent;
        snapshot.manualMarkupOverride = row.manualMarkupOverride;
        snapshot.markupSource = row.markupSource;
        snapshot.manufacturer = row.manufacturer.trimmed();
        existingSnapshots << snapshot;
        existing << (QStringList()
                     << displayDateFromStored(row.orderedAt)
                     << row.partNumber
                     << row.name
                     << FormatUtils::smartQty(row.qty, 2)
                     << moneyText(snapshot.purchasePrice)
                     << (snapshot.manualMarkupOverride ? QString::number(snapshot.markupPercent, 'f', 1) : QString())
                     << moneyText(row.amount)
                     << row.store
                     << QString());
    }
    const auto rows = runBatchEditor(this, QString::fromUtf8("Редагувати запчастини"),
                                     {QString::fromUtf8("Дата замовлення"), QString::fromUtf8("OEM номер"), QString::fromUtf8("Запчастина"), QString::fromUtf8("Кіл-ть"), QString::fromUtf8("Закупівельна ціна"), QString::fromUtf8("Націнка %"), QString::fromUtf8("Вартість"), QString::fromUtf8("Магазин"), QString::fromUtf8("Замовник")},
                                     existing, QStringLiteral("orders.batch.parts.edit.header"));
    if (rows.isEmpty() && !existing.isEmpty()) return;
    if (!confirmExplicitZeroPartMarkup(this, rows)) return;

    QList<int> existingIds = WorkPartsQueryService::loadPartIdsForOrder(orderUid);

    const int sharedCount = qMin(existingIds.size(), rows.size());

    MutationExecutor::Request request;
    request.sourceModule = QStringLiteral("ui.tabs.orders.WorkPartsCrudController");
    request.entityType = QStringLiteral("part_batch");
    request.entityId = QString::number(m_orderNumber);
    request.operation = QStringLiteral("replace_batch_parts");
    request.description = QString::fromUtf8("Пакетне оновлення списку запчастин");
    request.changedTables = {QStringLiteral("work_order_parts"), QStringLiteral("work_orders")};
    request.beginErrorFallback = QString::fromUtf8("Не можу відкрити транзакцію БД");
    request.commitErrorFallback = QString::fromUtf8("Не вдалося завершити оновлення списку запчастин");
    const auto execResult = WorkPartsMutationService::execute(request, [&](auto& db, QString* txError) {
        for (int index = 0; index < sharedCount; ++index) {
            const auto& row = rows.at(index);
            bool okQty = false, okPrice = false;
            const QString orderedAt = storedDateFromDisplay(row.value(0));
            const QString oemNumber = row.value(1).trimmed();
            const QString partName = normalizeSentence(row.value(2));
            const double qty = parseLooseNumber(row.value(3).trimmed(), &okQty);
            const double price = parseLooseNumber(row.value(4).trimmed(), &okPrice);
            const QString markupCellRaw = row.value(5).trimmed();
            bool okMarkup = false;
            const double manualMarkup = markupCellRaw.isEmpty() ? 0.0 : parseLooseNumber(markupCellRaw, &okMarkup);
            const bool hasManualMarkup = !markupCellRaw.isEmpty() && okMarkup && manualMarkup >= 0.0;
            const QString store = normalizeSentence(row.value(7));
            const double safeQty = okQty && qty > 0 ? qty : 1.0;
            const double purchasePrice = okPrice && price >= 0 ? FormatUtils::roundMoneyValue(price) : 0.0;
            const ExistingEditPartSnapshot snapshot = index < existingSnapshots.size() ? existingSnapshots.at(index) : ExistingEditPartSnapshot{};

            PartRowPayloadInput payloadInput;
            payloadInput.orderedAt = orderedAt;
            payloadInput.store = store;
            payloadInput.partNumber = oemNumber;
            payloadInput.name = partName;
            payloadInput.note = QStringLiteral("");
            payloadInput.manufacturer = snapshot.manufacturer.trimmed().isEmpty()
                ? effectiveOilBrand(partName, store)
                : snapshot.manufacturer;
            payloadInput.qty = safeQty;
            payloadInput.purchasePrice = purchasePrice;
            payloadInput.sourceType = QStringLiteral("manual_batch");
            if (hasManualMarkup) {
                payloadInput.manualMarkupOverride = true;
                payloadInput.manualMarkupPercent = qMax(0.0, manualMarkup);
            } else if (snapshot.manualMarkupOverride && snapshot.markupPercent > 0.0) {
                payloadInput.manualMarkupOverride = true;
                payloadInput.manualMarkupPercent = qMax(0.0, snapshot.markupPercent);
            } else {
                payloadInput.manualMarkupOverride = false;
                payloadInput.manualMarkupPercent = 0.0;
            }
            payloadInput.serviceId = 0;
            WorkOrderPartRow payload = PartRowPayloadBuilderService::build(payloadInput);
            qCDebug(csWorkPartsTraceLog).noquote() << QStringLiteral("PART_PRICING op=save order=%1 partId=%2 trace=manual_batch name=\"%3\" store=\"%4\" qty=%5 purchase=%6 client=%7 activePct=%8 source=%9 override=%10")
                .arg(m_orderNumber)
                .arg(existingIds.at(index))
                .arg(payload.name)
                .arg(payload.store)
                .arg(payload.qty, 0, 'f', 3)
                .arg(payload.purchasePrice, 0, 'f', 2)
                .arg(payload.clientPrice, 0, 'f', 2)
                .arg(payload.markupPercent, 0, 'f', 2)
                .arg(payload.markupSource)
                .arg(payload.manualMarkupOverride ? 1 : 0);

            QString saveError;
            if (!WorkPartsPersistenceRepositoryFacade::updatePart(db, m_orderNumber, existingIds.at(index), payload, &saveError)) {
                if (txError) *txError = QString::fromUtf8("Не вдалося оновити відредагований список запчастин.\n%1").arg(saveError);
                return false;
            }
        }

        for (int index = sharedCount; index < rows.size(); ++index) {
            const auto& row = rows.at(index);
            bool okQty = false, okPrice = false;
            const QString orderedAt = storedDateFromDisplay(row.value(0));
            const QString oemNumber = row.value(1).trimmed();
            const QString partName = normalizeSentence(row.value(2));
            const double qty = parseLooseNumber(row.value(3).trimmed(), &okQty);
            const double price = parseLooseNumber(row.value(4).trimmed(), &okPrice);
            const QString markupCellRaw = row.value(5).trimmed();
            bool okMarkup = false;
            const double manualMarkup = markupCellRaw.isEmpty() ? 0.0 : parseLooseNumber(markupCellRaw, &okMarkup);
            const bool hasManualMarkup = !markupCellRaw.isEmpty() && okMarkup && manualMarkup >= 0.0;
            const QString store = normalizeSentence(row.value(7));
            const double safeQty = okQty && qty > 0 ? qty : 1.0;
            const double purchasePrice = okPrice && price >= 0 ? FormatUtils::roundMoneyValue(price) : 0.0;

            PartRowPayloadInput payloadInput;
            payloadInput.orderedAt = orderedAt;
            payloadInput.store = store;
            payloadInput.partNumber = oemNumber;
            payloadInput.name = partName;
            payloadInput.note = QStringLiteral("");
            payloadInput.manufacturer = effectiveOilBrand(partName, store);
            payloadInput.qty = safeQty;
            payloadInput.purchasePrice = purchasePrice;
            payloadInput.sourceType = QStringLiteral("manual_batch");
            payloadInput.manualMarkupOverride = hasManualMarkup;
            payloadInput.manualMarkupPercent = payloadInput.manualMarkupOverride ? qMax(0.0, manualMarkup) : 0.0;
            payloadInput.serviceId = 0;
            if (m_btnCustomerParts->isChecked()) payloadInput.store = PartMarkupPolicyService::customerPartsStore();
            WorkOrderPartRow payload = PartRowPayloadBuilderService::build(payloadInput);
            qCDebug(csWorkPartsTraceLog).noquote() << QStringLiteral("PART_PRICING op=save order=%1 partId=0 trace=manual_batch name=\"%2\" store=\"%3\" qty=%4 purchase=%5 client=%6 activePct=%7 source=%8 override=%9")
                .arg(m_orderNumber)
                .arg(payload.name)
                .arg(payload.store)
                .arg(payload.qty, 0, 'f', 3)
                .arg(payload.purchasePrice, 0, 'f', 2)
                .arg(payload.clientPrice, 0, 'f', 2)
                .arg(payload.markupPercent, 0, 'f', 2)
                .arg(payload.markupSource)
                .arg(payload.manualMarkupOverride ? 1 : 0);

            QString saveError;
            if (!WorkPartsPersistenceRepositoryFacade::insertPart(db, m_orderNumber, payload, nullptr, &saveError)) {
                if (txError) *txError = QString::fromUtf8("Не вдалося зберегти відредагований список запчастин.\n%1").arg(saveError);
                return false;
            }
        }

        for (int index = sharedCount; index < existingIds.size(); ++index) {
            QString removeError;
            if (!WorkPartsPersistenceRepositoryFacade::removePart(db, m_orderNumber, existingIds.at(index), &removeError)) {
                if (txError) *txError = QString::fromUtf8("Не вдалося видалити зайві рядки запчастин.\n%1").arg(removeError);
                return false;
            }
        }

        QString statusError;
        if (!promoteOrderStatusToInProgressIfNeeded(db.connectionName(), m_orderNumber, &statusError)) {
            if (txError) *txError = statusError;
            return false;
        }
        return true;
    });
    if (!execResult.ok) {
        QMessageBox::critical(this,
                              QString::fromUtf8("Помилка при збереженні запчастин"),
                              execResult.errorText);
        scheduleWorkPartsRefresh(false,
                                 true,
                                 true,
                                 false,
                                 QStringLiteral("part_batch_save_failed"));
        return;
    }

    autoAssignPartsToServices();
    refreshAfterMutation(true, true, true, false);
    publishWorkPartsBackupEvent(QStringLiteral("order.parts"),
                                QStringLiteral("part_batch"),
                                QString::number(rows.size()),
                                currentOrderUid(),
                                m_orderNumber,
                                QStringLiteral("replace_batch"),
                                QString::fromUtf8("Оновлено список запчастин"),
                                {QStringLiteral("work_order_parts"), QStringLiteral("work_orders")});
}

