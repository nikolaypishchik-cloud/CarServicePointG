#include "ui/tabs/orders/CurrentOrdersCoordinator.h"
#include <functional>

#include "app/AppContext.h"
#include "core/backup/BackupPerformanceGuard.h"
#include "core/services/ActiveArchiveRoutingLayer.h"
#include "core/services/orders/OrderBatchMutationService.h"
#include "core/services/orders/PartImportTextSupport.h"
#include "core/services/AppRuntimeService.h"
#include "core/services/PartsQueryService.h"
#include "core/backup/RecoveryDraftStore.h"
#include "ui/common/dialogs/OrderDialog.h"

#include <QSet>

#include <algorithm>


namespace {
void purgeDeletedOrderRecoveryDrafts(const QList<int>& orderNumbers, const QMap<int, QString>& orderUidsByNumber)
{
    if (orderNumbers.isEmpty()) return;

    QSet<int> orderSet(orderNumbers.begin(), orderNumbers.end());
    QSet<QString> uidSet;
    for (auto it = orderUidsByNumber.cbegin(); it != orderUidsByNumber.cend(); ++it) {
        const QString uid = it.value().trimmed();
        if (!uid.isEmpty()) uidSet.insert(uid);
    }

    const auto sessions = RecoveryDraftStore::listSessions();
    for (const auto& session : sessions) {
        const auto drafts = RecoveryDraftStore::listDrafts(session.sessionDir);
        bool mutated = false;
        for (const auto& draft : drafts) {
            bool matches = false;
            bool orderOk = false;
            const int entityOrder = draft.entityId.trimmed().toInt(&orderOk);
            if (orderOk && orderSet.contains(entityOrder)) matches = true;
            if (!matches && !draft.orderUid.trimmed().isEmpty() && uidSet.contains(draft.orderUid.trimmed())) matches = true;
            if (!matches) continue;
            RecoveryDraftStore::removeDraft(draft, nullptr);
            mutated = true;
        }
        if (mutated && RecoveryDraftStore::listDrafts(session.sessionDir).isEmpty()) {
            RecoveryDraftStore::clearSession(session.sessionDir, nullptr);
        }
    }
}
} // namespace

CurrentOrdersCoordinator::RoutedOrderResult CurrentOrdersCoordinator::resolveOrderForOpen(int selectedOrderNumber,
                                                                                          const QString& orderUid)
{
    RoutedOrderResult result;
    int routedNumber = selectedOrderNumber;
    if (!orderUid.trimmed().isEmpty()) {
        QString routingError;
        if (!ActiveArchiveRoutingLayer::prepareOrderForOpen(orderUid, selectedOrderNumber, &routedNumber, &routingError)) {
            result.errorText = routingError;
            return result;
        }
    }
    if (routedNumber <= 0) {
        result.errorText = QString::fromUtf8("Наряд не вибрано.");
        return result;
    }
    result.ok = true;
    result.orderNumber = routedNumber;
    return result;
}

CurrentOrdersCoordinator::DialogRunResult CurrentOrdersCoordinator::openExistingOrderEditor(QWidget* parent,
                                                                                             int selectedOrderNumber,
                                                                                             const QString& orderUid,
                                                                                             const std::function<void()>& beforeDialog)
{
    DialogRunResult result;
    const auto routed = resolveOrderForOpen(selectedOrderNumber, orderUid);
    if (!routed.ok) {
        result.errorText = routed.errorText;
        return result;
    }

    if (beforeDialog) beforeDialog();
    BackupPerformanceGuard::hold(AppContext::instance().backupManager(), 3000);
    OrderDialog dlg(parent, routed.orderNumber);
    dlg.setWindowTitle(QString::fromUtf8("Редагування наряду"));
    dlg.openWorkPartsTab();

    result.launched = true;
    result.orderNumber = routed.orderNumber;
    result.accepted = dlg.exec() == QDialog::Accepted;
    return result;
}

CurrentOrdersCoordinator::DialogRunResult CurrentOrdersCoordinator::openVehicleAsNewOrder(QWidget* parent,
                                                                                           int sourceOrderNumber)
{
    DialogRunResult result;
    if (sourceOrderNumber <= 0) {
        result.errorText = QString::fromUtf8("Наряд не вибрано.");
        return result;
    }

    BackupPerformanceGuard::hold(AppContext::instance().backupManager(), 2500);
    OrderDialog dlg(parent);
    dlg.setWindowTitle(QString::fromUtf8("Новий наряд"));
    dlg.prefillFromExistingOrder(sourceOrderNumber);

    result.launched = true;
    result.orderNumber = sourceOrderNumber;
    result.accepted = dlg.exec() == QDialog::Accepted;
    return result;
}

CurrentOrdersCoordinator::DialogRunResult CurrentOrdersCoordinator::exportOrderPdf(QWidget* parent,
                                                                                    int selectedOrderNumber,
                                                                                    const QString& orderUid,
                                                                                    bool partsReport)
{
    DialogRunResult result;
    const auto routed = resolveOrderForOpen(selectedOrderNumber, orderUid);
    if (!routed.ok) {
        result.errorText = routed.errorText;
        return result;
    }

    BackupPerformanceGuard::hold(AppContext::instance().backupManager(), 2500);
    OrderDialog dlg(parent, routed.orderNumber);
    dlg.setReadOnly(true);
    dlg.exportPdfPreviewAndOpen(partsReport);

    result.launched = true;
    result.orderNumber = routed.orderNumber;
    result.accepted = true;
    return result;
}

CurrentOrdersCoordinator::MutationResult CurrentOrdersCoordinator::setOrdersClosed(const QList<int>& orderNumbers,
                                                                                   bool closed)
{
    MutationResult result;
    result.affectedOrderNumbers = orderNumbers;
    if (orderNumbers.isEmpty()) {
        result.success = true;
        return result;
    }

    QString batchError;
    if (!OrderBatchMutationService::setClosed(orderNumbers, closed, &batchError)) {
        result.errorText = batchError;
        return result;
    }

    result.success = true;
    return result;
}

CurrentOrdersCoordinator::MutationResult CurrentOrdersCoordinator::deleteOrders(const QList<int>& orderNumbers)
{
    MutationResult result;
    result.affectedOrderNumbers = orderNumbers;
    if (orderNumbers.isEmpty()) {
        result.success = true;
        return result;
    }

    for (int orderNumber : orderNumbers) {
        result.orderUidsByNumber.insert(orderNumber, AppRuntimeService::resolveOrderUid(orderNumber));
        QString err;
        if (!AppRuntimeService::hardDeleteOrder(orderNumber, &err)) {
            result.errorText = err.isEmpty()
                ? QString::fromUtf8("Не вдалося повністю видалити наряд")
                : err;
            return result;
        }
    }

    purgeDeletedOrderRecoveryDrafts(orderNumbers, result.orderUidsByNumber);
    PartsQueryService::invalidateCache();
    result.success = true;
    return result;
}

CurrentOrdersCoordinator::ImportValidationResult CurrentOrdersCoordinator::buildInvoiceImportRows(const QList<CurrentOrdersImportDraftRow>& rows)
{
    ImportValidationResult result;
    if (rows.isEmpty()) {
        result.errorText = QString::fromUtf8("Немає рядків для імпорту.");
        return result;
    }

    QSet<int> uniqueOrders;
    result.serviceRows.reserve(rows.size());
    for (int index = 0; index < rows.size(); ++index) {
        const auto& row = rows.at(index);
        const QString orderedAt = row.orderedAt.trimmed();
        const QString deliveryDate = row.deliveryDate.trimmed();
        const QString orderNumberText = row.orderNumberText.trimmed();
        const QString store = row.store.trimmed();
        const QString oem = row.oemNumber.trimmed();
        const QString name = row.name.trimmed();
        const QString brandName = row.brandName.trimmed();
        const QString qtyText = row.qtyText.trimmed();
        const QString priceText = row.priceText.trimmed();

        const bool rowHasData = !orderedAt.isEmpty() || !deliveryDate.isEmpty() || !orderNumberText.isEmpty() || !store.isEmpty() ||
                                !oem.isEmpty() || !name.isEmpty() || !brandName.isEmpty() || !qtyText.isEmpty() || !priceText.isEmpty();
        if (!rowHasData) continue;

        if (orderNumberText.isEmpty()) {
            // Current orders invoice preview: rows without an order number are intentionally skipped.
            // Only rows with a filled № наряду participate in import; this keeps the preview editable
            // without blocking the whole import because of unused invoice lines.
            continue;
        }

        bool orderOk = false;
        const int orderNumber = orderNumberText.toInt(&orderOk);
        if (!orderOk || orderNumber <= 0) {
            result.errorText = QString::fromUtf8("Рядок %1: некоректний № наряду.").arg(index + 1);
            return result;
        }

        if (name.isEmpty() && oem.isEmpty()) {
            result.errorText = QString::fromUtf8("Рядок %1: заповніть ОЕМ номер або Найменування.").arg(index + 1);
            return result;
        }

        bool qtyOk = false;
        const double qtyValue = qtyText.isEmpty() ? 1.0 : PartImportTextSupport::parseLooseNumber(qtyText, &qtyOk);
        if ((!qtyText.isEmpty() && !qtyOk) || qtyValue <= 0.0) {
            result.errorText = QString::fromUtf8("Рядок %1: некоректна кількість.").arg(index + 1);
            return result;
        }

        bool priceOk = false;
        const double priceValue = priceText.isEmpty() ? 0.0 : PartImportTextSupport::parseLooseNumber(priceText, &priceOk);
        if (!priceText.isEmpty() && !priceOk) {
            result.errorText = QString::fromUtf8("Рядок %1: некоректна ціна.").arg(index + 1);
            return result;
        }
        Q_UNUSED(priceValue);

        CurrentOrdersImportServiceRow serviceRow;
        serviceRow.orderedAt = orderedAt;
        serviceRow.deliveryDate = deliveryDate;
        serviceRow.orderNumberText = QString::number(orderNumber);
        serviceRow.store = store;
        serviceRow.oemNumber = oem;
        serviceRow.name = name;
        serviceRow.brandName = brandName;
        serviceRow.qtyText = qtyText;
        serviceRow.priceText = priceText;
        result.serviceRows.append(serviceRow);
        uniqueOrders.insert(orderNumber);
    }

    if (result.serviceRows.isEmpty()) {
        result.errorText = QString::fromUtf8("Немає рядків з указаним № наряду для імпорту.");
        return result;
    }

    result.uniqueOrderNumbers = uniqueOrders.values();
    std::sort(result.uniqueOrderNumbers.begin(), result.uniqueOrderNumbers.end());
    result.success = true;
    return result;
}

bool CurrentOrdersCoordinator::importInvoiceRows(const QList<CurrentOrdersImportServiceRow>& rows,
                                                 const CurrentOrdersInvoiceAttachmentSpec& attachmentSpec,
                                                 CurrentOrdersInvoiceImportResult* result,
                                                 QString* errorText)
{
    return CurrentOrdersInvoiceImportService::importRows(rows, attachmentSpec, result, errorText);
}
