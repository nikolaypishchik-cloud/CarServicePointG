#include "core/util/DateFormats.h"
#pragma once

// P2 architecture module boundary for shared part-edit state services.
// These small value objects and stateless mappers are header-defined because
// multiple WorkParts split .cpp modules need the exact same compile-time types.

struct WorkPartsPartDateState {
    QString displayOrderedAt;
    QString storedOrderedAt;
    QString deliveryDate;
    QString installedAt;
};

struct WorkPartsPartPriceState {
    double typedPrice = 0.0;
    bool okPrice = false;
    double purchasePrice = 0.0;
};

struct WorkPartsPartEditState {
    int row = -1;
    int partId = 0;
    bool hasName = false;
    bool importedInvoiceRow = false;
    QString importTraceId;
    QString orderUid;
    QString name;
    QString store;
    QString oemNumber;
    QString manufacturer;
    double qty = 1.0;
    bool okQty = false;
    double safeQty = 1.0;
    WorkPartsPartDateState dates;
    WorkPartsPartPriceState price;
};

class PartEditSessionService final
{
public:
    static bool canPersistPartsRow(QTableWidget* table, bool loadingParts, int row)
    {
        return table && !loadingParts && row >= 0 && row < table->rowCount();
    }

    static void clearDirtyEmptyPartRow(QSet<int>& dirtyRows, int row)
    {
        dirtyRows.remove(row);
    }
};

class PartDatePolicyService final
{
public:
    static WorkPartsPartDateState resolveForSave(QTableWidget* table,
                                                  int row,
                                                  int orderNumber,
                                                  int partId,
                                                  bool importedInvoiceRow,
                                                  const QString& importTraceId)
    {
        WorkPartsPartDateState state;
        if (!table || row < 0 || row >= table->rowCount()) return state;

        state.displayOrderedAt = table->item(row, PartColOrderedAt)
            ? table->item(row, PartColOrderedAt)->text().trimmed()
            : QString();

        if (state.displayOrderedAt.isEmpty() && !importedInvoiceRow) {
            state.displayOrderedAt = currentOrderDateDisplay();
            if (auto* orderedAtItem = table->item(row, PartColOrderedAt)) {
                const QSignalBlocker blocker(table);
                orderedAtItem->setText(state.displayOrderedAt);
            }
        }

        state.storedOrderedAt = (importedInvoiceRow && state.displayOrderedAt.isEmpty())
            ? QString()
            : storedDateFromDisplay(state.displayOrderedAt);

        qCDebug(csWorkPartsTraceLog).noquote() << QStringLiteral("PART_DATE_POLICY resolve_ordered_at order=") << orderNumber
                          << QStringLiteral(" row=") << row
                          << QStringLiteral(" partId=") << partId
                          << QStringLiteral(" imported=") << importedInvoiceRow
                          << QStringLiteral(" trace=") << importTraceId
                          << QStringLiteral(" display=") << clippedImportLogText(state.displayOrderedAt)
                          << QStringLiteral(" stored=") << state.storedOrderedAt;

        if (partId > 0) {
            state.deliveryDate = WorkPartsQueryService::loadPartDeliveryDate(partId);
        }

        QString invoiceNumber;
        QString invoiceDate;
        if (auto* attachmentItem = table->item(row, PartColAttachment)) {
            invoiceNumber = attachmentItem->data(RoleAttachmentDocumentNumber).toString().trimmed();
            invoiceDate = attachmentItem->data(RoleAttachmentDocumentDate).toString().trimmed();
        }

        const QDate orderedDate = DateFormats::parseDate(state.storedOrderedAt);
        const QDate deliveryDate = DateFormats::parseDate(state.deliveryDate);
        const bool manualWithoutInvoiceMeta = !importedInvoiceRow && invoiceNumber.isEmpty() && invoiceDate.isEmpty();
        const bool needsManualDeliveryAnchor = manualWithoutInvoiceMeta
            && orderedDate.isValid()
            && (state.deliveryDate.trimmed().isEmpty() || !deliveryDate.isValid() || orderedDate < deliveryDate);

        if (needsManualDeliveryAnchor) {
            // Manual part rows do not have a separate delivery-date editor. The visible
            // date cell must therefore become the delivery anchor used by overdue
            // highlighting and autosort, otherwise manual parts are saved as "today"
            // and bypass 3/5-day overdue buckets. This also repairs rows saved by
            // older builds where delivery_date was defaulted to the save date.
            state.deliveryDate = state.storedOrderedAt;
            qCInfo(csWorkPartsTraceLog).noquote() << QStringLiteral("PART_DATE_POLICY manual_delivery_date_from_ordered_at order=") << orderNumber
                              << QStringLiteral(" row=") << row
                              << QStringLiteral(" partId=") << partId
                              << QStringLiteral(" orderedAt=") << state.storedOrderedAt
                              << QStringLiteral(" previousDelivery=") << (deliveryDate.isValid() ? deliveryDate.toString(Qt::ISODate) : QString());
        } else if (state.deliveryDate.trimmed().isEmpty()) {
            qCDebug(csWorkPartsTraceLog).noquote() << QStringLiteral("PART_DATE_POLICY preserve_delivery_date_empty order=") << orderNumber
                              << QStringLiteral(" row=") << row
                              << QStringLiteral(" partId=") << partId
                              << QStringLiteral(" imported=") << importedInvoiceRow
                              << QStringLiteral(" trace=") << importTraceId
                              << QStringLiteral(" invoiceNumber=") << clippedImportLogText(invoiceNumber)
                              << QStringLiteral(" invoiceDate=") << clippedImportLogText(invoiceDate);
        }

        if (auto* deliveredItem = table->item(row, PartColDelivered)) {
            state.installedAt = deliveredItem->data(RoleDeliveryDate).toString().trimmed();
            if (state.installedAt.isEmpty() && deliveredItem->checkState() == Qt::Checked) {
                state.installedAt = QDate::currentDate().toString(Qt::ISODate);
            }
        }
        return state;
    }
};

class PartPricePolicyService final
{
public:
    static WorkPartsPartPriceState resolveForSave(QTableWidget* table, int row)
    {
        WorkPartsPartPriceState state;
        auto* priceItem = table ? table->item(row, PartColPrice) : nullptr;
        state.typedPrice = priceItem ? parseLooseNumber(priceItem->text(), &state.okPrice) : 0.0;
        state.purchasePrice = state.okPrice && state.typedPrice >= 0.0 ? state.typedPrice : 0.0;
        state.purchasePrice = FormatUtils::roundMoneyValue(qMax(0.0, state.purchasePrice));

        if (priceItem) {
            const double storedPurchase = priceItem->data(RolePurchasePrice).toDouble();
            if (storedPurchase > 0.0) {
                state.purchasePrice = storedPurchase;
            }
        }
        return state;
    }
};

class PartTableStateMapper final
{
public:
    static WorkPartsPartEditState readForSave(QTableWidget* table, int row, int orderNumber, const QString& orderUid)
    {
        WorkPartsPartEditState state;
        state.row = row;
        state.orderUid = orderUid;
        if (!table || row < 0 || row >= table->rowCount()) return state;

        auto* nameItem = table->item(row, PartColName);
        if (!nameItem) return state;

        state.name = normalizePartSentence(nameItem->text());
        if (isPartHintValue(state.name) || state.name.startsWith(QLatin1Char('+'))) state.name.clear();
        state.hasName = !state.name.isEmpty();
        state.partId = nameItem->data(RoleId).toInt();
        state.importedInvoiceRow = partRowHasImportedInvoiceMarker(table, row);
        state.importTraceId = importedPartTraceId(table, row);

        state.qty = table->item(row, PartColQty) ? parseLooseNumber(table->item(row, PartColQty)->text(), &state.okQty) : 1.0;
        state.safeQty = state.okQty && state.qty > 0.0 ? state.qty : 1.0;
        state.store = table->item(row, PartColStore) ? normalizeSentence(table->item(row, PartColStore)->text()) : QString();
        state.oemNumber = table->item(row, PartColOem) ? table->item(row, PartColOem)->text().trimmed() : QString();
        state.manufacturer = partManufacturerText(table, row);
        state.price = PartPricePolicyService::resolveForSave(table, row);
        state.dates = PartDatePolicyService::resolveForSave(table,
                                                            row,
                                                            orderNumber,
                                                            state.partId,
                                                            state.importedInvoiceRow,
                                                            state.importTraceId);
        return state;
    }
};
