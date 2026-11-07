// P2 architecture migration: implementation module converted from legacy .ipp implementation-fragment decomposition.
// Compiled as a separate P2 implementation module; include-order dependencies are isolated in the module prelude.

// extracted from WorkPartsPersistenceCoordinator.cpp as part of p.4 decomposition
#include "ui/tabs/orders/WorkPartsDialogModulePrelude.h"
#include "core/services/orders/HistoryPartsAutofillService.h"
#include "ui/tabs/orders/HistoryPartsDraftRoles.h"

bool WorkPartsDialog::saveServiceRow(int row, QString* errorText, bool reloadAfterSave)
{
    if (m_syncingServices || row < 0 || row >= m_servicesTable->rowCount()) return false;

    QString routeError;
    if (!ensureOrderReadyForEditing(m_orderNumber, &routeError)) {
        const QString message = QString::fromUtf8("Наряд не готовий до збереження робіт у робочій БД.\n%1").arg(routeError);
        if (errorText) *errorText = message;
        else QMessageBox::critical(this, QString::fromUtf8("Помилка при збереженні роботи"), message);
        return false;
    }

    auto* titleItem = m_servicesTable->item(row, ServiceColTitle);
    if (!titleItem) return false;

    const QString enteredTitle = canonicalizeServiceTitle(titleItem->text());
    const int serviceId = titleItem->data(RoleId).toInt();
    logWorkPartsDiagnostic(serviceId > 0 ? QStringLiteral("row_update_begin") : QStringLiteral("row_insert_persist_begin"),
                           QStringLiteral("service"),
                           row,
                           serviceId,
                           QString(),
                           {{QStringLiteral("title"), enteredTitle},
                            {QStringLiteral("reloadAfterSave"), reloadAfterSave}});
    if (isServiceHintValue(enteredTitle) || enteredTitle.trimmed().isEmpty()) {
        if (serviceId > 0) {
            QString removeError;
            const QString connectionName = m_useExternalSaveTransaction ? m_externalSaveConnectionName : QString();
            if (m_useExternalSaveTransaction) {
                QString localError;
                const bool ok = WorkPartsMutationService::runInActiveTransaction(connectionName, [&](auto& db, QString*) {
                    return WorkPartsPersistenceRepositoryFacade::removeService(db, m_orderNumber, serviceId, &removeError);
                }, &localError);
                if (!ok) {
                    const QString effectiveError = !removeError.trimmed().isEmpty() ? removeError : localError;
                    if (errorText) *errorText = effectiveError;
                    else QMessageBox::critical(this, QString::fromUtf8("Помилка"), effectiveError);
                    return false;
                }
            } else {
                MutationExecutor::Request request;
                request.sourceModule = QStringLiteral("ui.tabs.orders.WorkPartsServicePersistence");
                request.entityType = QStringLiteral("service");
                request.entityId = QString::number(serviceId);
                request.operation = QStringLiteral("delete_service_row_via_blank_title");
                request.description = QString::fromUtf8("Видалення роботи після очищення назви рядка");
                request.changedTables = {QStringLiteral("work_order_services"), QStringLiteral("service_masters"), QStringLiteral("work_orders")};
                request.beginErrorFallback = QString::fromUtf8("Не можу відкрити транзакцію БД");
                request.commitErrorFallback = QString::fromUtf8("Не вдалося завершити видалення роботи");
                const auto mutationResult = WorkPartsMutationService::execute(request, [&](auto& txDb, QString* txError) {
                    if (!WorkPartsPersistenceRepositoryFacade::removeService(txDb, m_orderNumber, serviceId, &removeError)) {
                        if (txError) *txError = removeError;
                        return false;
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
                    if (errorText) *errorText = mutationError;
                    else if (!mutationError.trimmed().isEmpty()) QMessageBox::critical(this, QString::fromUtf8("Помилка"), mutationError);
                    return false;
                }
            }
            m_activeServiceId = 0;
            if (!m_useExternalSaveTransaction) {
                m_dirtyServiceRows.remove(row);
            } else {
                m_dirtyServiceRows.insert(row);
                qCDebug(csWorkPartsTraceLog).noquote() << QStringLiteral("SAVE_PENDING service_delete_dirty_kept_until_batch_commit order=") << m_orderNumber
                                  << QStringLiteral(" row=") << row
                                  << QStringLiteral(" serviceId=") << serviceId;
            }
            if (!m_useExternalSaveTransaction && !m_commitPendingInProgress) {
                publishWorkPartsBackupEvent(QStringLiteral("order.services"),
                                            QStringLiteral("service"),
                                            QString::number(serviceId),
                                            currentOrderUid(),
                                            m_orderNumber,
                                            QStringLiteral("delete"),
                                            QString::fromUtf8("Видалено роботу після очищення назви"),
                                            {QStringLiteral("work_order_services"), QStringLiteral("service_masters"), QStringLiteral("work_orders")});
            }
        } else {
            m_dirtyServiceRows.remove(row);
        }
        return true;
    }
    if (serviceId <= 0) {
        bool okPrice = false, okQty = false;
        const double price = m_servicesTable->item(row, ServiceColPrice)
                                 ? parseLooseNumber(m_servicesTable->item(row, ServiceColPrice)->text(), &okPrice)
                                 : 0.0;
        const double qty = m_servicesTable->item(row, ServiceColQty)
                               ? parseLooseNumber(m_servicesTable->item(row, ServiceColQty)->text(), &okQty)
                               : 1.0;
        QString masterName = m_servicesTable->item(row, ServiceColMaster)
                                 ? normalizeSentence(m_servicesTable->item(row, ServiceColMaster)->text())
                                 : QString();
        if (masterName.trimmed().isEmpty()) masterName = currentPrimaryMasterName(m_masterCombo);
        QString masterId;
        const auto masters = PersonnelSettingsService::loadPersonnel(true);
        for (const auto& m : masters) {
            if (m.name.trimmed().compare(masterName.trimmed(), Qt::CaseInsensitive) == 0) {
                masterId = m.id;
                masterName = m.name;
                break;
            }
        }

        WorkOrderServiceRow service;
        service.title = enteredTitle;
        service.qty = okQty && qty > 0.0 ? qty : 1.0;
        service.price = okPrice ? qMax(0.0, price) : 0.0;
        service.amount = service.qty * service.price;
        service.masterId = masterId;
        service.masterName = masterName;

        QString repoError;
        int newServiceId = 0;
        bool deferredServiceAttachmentQueued = false;
        const QString connectionName = m_useExternalSaveTransaction ? m_externalSaveConnectionName : QString();
        if (m_useExternalSaveTransaction) {
            QString localError;
            const bool ok = WorkPartsMutationService::runInActiveTransaction(connectionName, [&](auto& db, QString*) {
                return WorkPartsPersistenceRepositoryFacade::insertService(db, m_orderNumber, service, &newServiceId, &repoError);
            }, &localError);
            if (!ok) {
                const QString effectiveError = !repoError.trimmed().isEmpty() ? repoError : localError;
                if (errorText) *errorText = effectiveError;
                else QMessageBox::critical(this, QString::fromUtf8("Помилка"), effectiveError);
                return false;
            }
        } else {
            MutationExecutor::Request request;
            request.sourceModule = QStringLiteral("ui.tabs.orders.WorkPartsServicePersistence");
            request.entityType = QStringLiteral("service");
            request.entityId = QString::number(m_orderNumber);
            request.operation = QStringLiteral("insert_service_row");
            request.description = QString::fromUtf8("Збереження нового рядка роботи");
            request.changedTables = {QStringLiteral("work_order_services"), QStringLiteral("service_masters"),
                                     QStringLiteral("work_order_parts"), QStringLiteral("work_orders")};
            request.beginErrorFallback = QString::fromUtf8("Не можу відкрити транзакцію БД");
            request.commitErrorFallback = QString::fromUtf8("Не вдалося завершити збереження роботи");
            const auto execResult = WorkPartsMutationService::execute(request, [&](auto& txDb, QString* txError) {
                if (!WorkPartsPersistenceRepositoryFacade::insertService(txDb, m_orderNumber, service, &newServiceId, &repoError)) {
                    if (txError) *txError = repoError;
                    return false;
                }
                const bool previewHandled = titleItem->data(HistoryPartsDraftRoles::CheckedTitle).toString()
                    == enteredTitle.simplified().toCaseFolded();
                if (!previewHandled && !HistoryPartsAutofillService::apply(txDb, m_orderNumber, newServiceId, txError)) return false;
                QString statusError;
                if (!promoteOrderStatusToInProgressIfNeeded(txDb.connectionName(), m_orderNumber, &statusError)) {
                    if (txError) *txError = statusError;
                    return false;
                }
                return true;
            });
            if (errorText) *errorText = execResult.errorText;
            if (!execResult.ok) {
                if (!errorText || (errorText && !errorText->trimmed().isEmpty())) {
                    const QString message = errorText ? *errorText : QString();
                    if (!message.trimmed().isEmpty()) QMessageBox::critical(this, QString::fromUtf8("Помилка"), message);
                }
                return false;
            }
        }
        if (!m_useExternalSaveTransaction) clearRowHintState(m_servicesTable, row);
        titleItem->setData(RoleId, newServiceId);
        titleItem->setText(enteredTitle);
        if (auto* priceItem = m_servicesTable->item(row, ServiceColPrice)) priceItem->setText(moneyText(service.price));
        if (auto* qtyItem = m_servicesTable->item(row, ServiceColQty)) qtyItem->setText(FormatUtils::smartQty(service.qty, 2));
        if (auto* masterItem = m_servicesTable->item(row, ServiceColMaster)) masterItem->setText(masterName);
        if (auto* attachmentItem = m_servicesTable->item(row, ServiceColAttachment)) {
            const QString pendingSource = attachmentItem->data(RoleAttachmentSource).toString().trimmed();
            if (!pendingSource.isEmpty()) {
                if (m_useExternalSaveTransaction) {
                    if (!stageServiceAttachmentAfterTransaction(row, newServiceId, pendingSource)) {
                        const QString message = QString::fromUtf8("Не вдалося поставити вкладення роботи у чергу після транзакції.");
                        m_dirtyServiceRows.insert(row);
                        if (errorText) *errorText = message;
                        return false;
                    }
                    deferredServiceAttachmentQueued = true;
                } else {
                    QString attachError;
                    const QString storedPath = copiedAttachmentPath(m_orderNumber, QStringLiteral("services"), newServiceId, pendingSource, &attachError);
                    const bool metadataSaved = !storedPath.trimmed().isEmpty()
                        && WorkPartsMutationService::setServiceAttachment(newServiceId, storedPath, QFileInfo(pendingSource).fileName(), &attachError);
                    if (!metadataSaved) {
                        if (!storedPath.trimmed().isEmpty()) removeAttachmentFileIfManaged(storedPath);
                        const QString detail = attachError.trimmed().isEmpty()
                            ? QString::fromUtf8("Не вдалося зберегти вкладення роботи або metadata вкладення.")
                            : attachError;
                        m_dirtyServiceRows.insert(row);
                        if (errorText) *errorText = detail;
                        else if (!detail.trimmed().isEmpty()) QMessageBox::critical(this, QString::fromUtf8("Помилка"), detail);
                        return false;
                    }
                }
            }
        }
        if (!m_useExternalSaveTransaction) {
            setRowEditingVisual(m_servicesTable, row);
            refreshServiceAttachmentCell(row);
            m_dirtyServiceRows.remove(row);
            logWorkPartsDiagnostic(QStringLiteral("dirty_state_changed"), QStringLiteral("service"), row, serviceId, QString(), {{QStringLiteral("reason"), QStringLiteral("blank_title_delete_clear")}});
        } else {
            m_dirtyServiceRows.insert(row);
            logWorkPartsDiagnostic(QStringLiteral("dirty_state_changed"), QStringLiteral("service"), row, serviceId, QString(), {{QStringLiteral("reason"), QStringLiteral("external_transaction_pending")}});
            qCDebug(csWorkPartsTraceLog).noquote() << QStringLiteral("ATTACHMENT_STAGE service_dirty_kept_until_batch_commit order=") << m_orderNumber
                              << QStringLiteral(" row=") << row
                              << QStringLiteral(" serviceId=") << newServiceId
                              << QStringLiteral(" deferredAttachment=") << deferredServiceAttachmentQueued;
        }
        m_activeServiceId = newServiceId;
        if (reloadAfterSave) {
            refreshAfterMutation(true, true, true, true);
        }
        logWorkPartsDiagnostic(QStringLiteral("row_insert_persist_end"),
                               QStringLiteral("service"),
                               row,
                               newServiceId,
                               QString(),
                               {{QStringLiteral("reloadAfterSave"), reloadAfterSave}});
        if (!m_useExternalSaveTransaction && !m_commitPendingInProgress) {
            publishWorkPartsBackupEvent(QStringLiteral("order.services"),
                                        QStringLiteral("service"),
                                        QString::number(newServiceId),
                                        currentOrderUid(),
                                        m_orderNumber,
                                        QStringLiteral("create"),
                                        QString::fromUtf8("Додано роботу"),
                                        {QStringLiteral("work_order_services"), QStringLiteral("service_masters"),
                                         QStringLiteral("work_order_parts"), QStringLiteral("work_orders")});
        }
        return true;
    }

    bool okPrice = false, okQty = false;
    const double price = m_servicesTable->item(row, ServiceColPrice)
                             ? parseLooseNumber(m_servicesTable->item(row, ServiceColPrice)->text(), &okPrice)
                             : 0.0;
    const double qty = m_servicesTable->item(row, ServiceColQty)
                           ? parseLooseNumber(m_servicesTable->item(row, ServiceColQty)->text(), &okQty)
                           : 1.0;
    QString masterName = m_servicesTable->item(row, ServiceColMaster)
                             ? normalizeSentence(m_servicesTable->item(row, ServiceColMaster)->text())
                             : QString();
    if (masterName.trimmed().isEmpty()) masterName = currentPrimaryMasterName(m_masterCombo);
    QString masterId;
    const auto masters = PersonnelSettingsService::loadPersonnel(true);
    for (const auto& m : masters) {
        if (m.name.trimmed().compare(masterName.trimmed(), Qt::CaseInsensitive) == 0) {
            masterId = m.id;
            masterName = m.name;
            break;
        }
    }

    WorkOrderServiceRow service;
    service.id = serviceId;
    service.orderNumber = m_orderNumber;
    service.title = enteredTitle;
    service.qty = okQty && qty > 0.0 ? qty : 1.0;
    service.price = okPrice ? qMax(0.0, price) : 0.0;
    service.amount = service.qty * service.price;
    service.masterId = masterId;
    service.masterName = masterName;

    QString repoError;
    const QString connectionName = m_useExternalSaveTransaction ? m_externalSaveConnectionName : QString();
    if (m_useExternalSaveTransaction) {
        QString localError;
        const bool ok = WorkPartsMutationService::runInActiveTransaction(connectionName, [&](auto& db, QString*) {
            return WorkPartsPersistenceRepositoryFacade::updateService(db, m_orderNumber, serviceId, service, &repoError);
        }, &localError);
        if (!ok) {
            const QString effectiveError = !repoError.trimmed().isEmpty() ? repoError : localError;
            logWorkPartsDiagnostic(QStringLiteral("row_update_fail"), QStringLiteral("service"), row, serviceId, QString(), {{QStringLiteral("error"), effectiveError}});
            if (errorText) *errorText = effectiveError;
            if (!errorText) QMessageBox::critical(this, QString::fromUtf8("Помилка"), effectiveError);
            return false;
        }
    } else {
        MutationExecutor::Request request;
        request.sourceModule = QStringLiteral("ui.tabs.orders.WorkPartsServicePersistence");
        request.entityType = QStringLiteral("service");
        request.entityId = QString::number(serviceId);
        request.operation = QStringLiteral("update_service_row");
        request.description = QString::fromUtf8("Оновлення рядка роботи");
        request.changedTables = {QStringLiteral("work_order_services"), QStringLiteral("service_masters"), QStringLiteral("work_orders")};
        request.beginErrorFallback = QString::fromUtf8("Не можу відкрити транзакцію БД");
        request.commitErrorFallback = QString::fromUtf8("Не вдалося завершити оновлення роботи");
        const auto mutationResult = WorkPartsMutationService::execute(request, [&](auto& txDb, QString* txError) {
            if (!WorkPartsPersistenceRepositoryFacade::updateService(txDb, m_orderNumber, serviceId, service, &repoError)) {
                if (txError) *txError = repoError;
                return false;
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
            logWorkPartsDiagnostic(QStringLiteral("row_update_fail"), QStringLiteral("service"), row, serviceId, QString(), {{QStringLiteral("error"), mutationError}});
            if (errorText) *errorText = mutationError;
            if (!errorText && !mutationError.trimmed().isEmpty()) QMessageBox::critical(this, QString::fromUtf8("Помилка"), mutationError);
            return false;
        }
    }
    if (!m_useExternalSaveTransaction) {
        m_dirtyServiceRows.remove(row);
        logWorkPartsDiagnostic(QStringLiteral("dirty_state_changed"), QStringLiteral("service"), row, serviceId, QString(), {{QStringLiteral("reason"), QStringLiteral("save_row_clear")}});
    } else {
        m_dirtyServiceRows.insert(row);
        logWorkPartsDiagnostic(QStringLiteral("dirty_state_changed"), QStringLiteral("service"), row, serviceId, QString(), {{QStringLiteral("reason"), QStringLiteral("external_transaction_pending")}});
        qCDebug(csWorkPartsTraceLog).noquote() << QStringLiteral("SAVE_PENDING service_dirty_kept_until_batch_commit order=") << m_orderNumber
                          << QStringLiteral(" row=") << row
                          << QStringLiteral(" serviceId=") << serviceId;
    }
    if (reloadAfterSave) {
        refreshAfterMutation(true, false, true, false);
        focusSection(0);
    }
    logWorkPartsDiagnostic(QStringLiteral("row_update_end"),
                           QStringLiteral("service"),
                           row,
                           serviceId,
                           QString(),
                           {{QStringLiteral("reloadAfterSave"), reloadAfterSave}});
    if (!m_useExternalSaveTransaction && !m_commitPendingInProgress) {
        publishWorkPartsBackupEvent(QStringLiteral("order.services"),
                                    QStringLiteral("service"),
                                    QString::number(serviceId),
                                    currentOrderUid(),
                                    m_orderNumber,
                                    QStringLiteral("update"),
                                    QString::fromUtf8("Оновлено роботу"),
                                    {QStringLiteral("work_order_services"), QStringLiteral("service_masters"), QStringLiteral("work_orders")});
    }
    return true;
}

