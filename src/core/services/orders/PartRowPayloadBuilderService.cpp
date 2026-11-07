#include "core/util/DateFormats.h"
#include "core/services/orders/PartRowPayloadBuilderService.h"
#include "core/diagnostics/RuntimeLoggingControl.h"

#include "core/services/orders/PartImportTextSupport.h"
#include "core/services/orders/PartMarkupPolicyService.h"
#include "core/services/orders/PartPricingEngine.h"
#include "core/services/WorkshopInventoryService.h"
#include "core/util/FormatUtils.h"

#include <QDate>
#include <QDebug>
#include <QStringList>
#include <QtGlobal>

namespace {
QString normalizeToIsoDate(const QString& raw, bool preserveEmptyOrInvalid = false, const char* fieldName = "date")
{
    const QString display = PartImportTextSupport::normalizeImportedDateDisplay(raw).trimmed();
    if (display.isEmpty()) {
        if (preserveEmptyOrInvalid) {
            if (RuntimeLoggingControl::deepDebugEnabled()) qInfo().noquote() << QStringLiteral("PART_DATE_POLICY keep_empty field=") << QString::fromLatin1(fieldName);
            return QString();
        }
        return QDate::currentDate().toString(Qt::ISODate);
    }

    QDate d = DateFormats::parseDate(display);
    if (!d.isValid()) d = QDate::fromString(display, QStringLiteral("dd.MM.yyyy"));
    if (!d.isValid()) {
        const QStringList parts = display.split(QLatin1Char('.'));
        if (parts.size() == 3) {
            bool okDay = false;
            bool okMonth = false;
            bool okYear = false;
            const int day = parts.at(0).toInt(&okDay);
            const int month = parts.at(1).toInt(&okMonth);
            int year = parts.at(2).toInt(&okYear);
            if (okDay && okMonth && okYear) {
                if (parts.at(2).size() <= 2) year += (year >= 70 ? 1900 : 2000);
                const QDate custom(year, month, day);
                if (custom.isValid()) d = custom;
            }
        }
    }
    if (d.isValid()) return d.toString(Qt::ISODate);
    if (preserveEmptyOrInvalid) {
        qWarning().noquote() << QStringLiteral("PART_DATE_POLICY invalid_import_date_preserved_empty field=")
                             << QString::fromLatin1(fieldName)
                             << QStringLiteral(" raw=") << raw;
        return QString();
    }
    return QDate::currentDate().toString(Qt::ISODate);
}

QString safeText(const QString& value)
{
    return value.isNull() ? QStringLiteral("") : value.trimmed();
}
}

WorkOrderPartRow PartRowPayloadBuilderService::build(const PartRowPayloadInput& input)
{
    WorkOrderPartRow row;
    row.pricingCalcVersion = 3;
    row.orderedAt = normalizeToIsoDate(input.orderedAt, input.preserveEmptyOrderedAt, "ordered_at");
    QString deliverySource = input.deliveryDate;
    if (!input.preserveEmptyDeliveryDate && deliverySource.trimmed().isEmpty() && !row.orderedAt.trimmed().isEmpty()) {
        deliverySource = row.orderedAt;
        if (RuntimeLoggingControl::deepDebugEnabled()) qInfo().noquote() << QStringLiteral("PART_DATE_POLICY payload_delivery_date_from_ordered_at orderedAt=") << row.orderedAt;
    }
    row.deliveryDate = normalizeToIsoDate(deliverySource, input.preserveEmptyDeliveryDate, "delivery_date");
    row.installedAt = safeText(input.installedAt);
    row.store = safeText(input.store);
    row.partNumber = safeText(input.partNumber);
    row.name = safeText(input.name);
    row.note = input.note.isNull() ? QStringLiteral("") : input.note;
    row.manufacturer = safeText(input.manufacturer);
    if (row.manufacturer.trimmed().isEmpty()) {
        row.manufacturer = PartMarkupPolicyService::effectiveOilBrand(row.name, row.store);
    }
    row.qty = input.qty; // Invalid input must reach validation, never become one unit.

    // Imported invoice rows created from the order editor do not expose a separate
    // delivery-date editor.  The overdue engine uses delivery_date, therefore an
    // imported row with only the visible invoice/order date looked fresh and never
    // entered the 3/5-day overdue highlight buckets.  For import/sync/scan rows the
    // visible ordered_at date is the delivery/received anchor unless an explicit
    // delivery date was supplied.
    {
        const QString src = input.sourceType.trimmed().toLower();
        const bool importLike = src == QStringLiteral("import")
            || src == QStringLiteral("invoice")
            || src == QStringLiteral("scan")
            || src == QStringLiteral("sync");
        if (importLike && row.deliveryDate.trimmed().isEmpty() && !row.orderedAt.trimmed().isEmpty()) {
            row.deliveryDate = row.orderedAt;
            if (RuntimeLoggingControl::deepDebugEnabled()) qInfo().noquote() << QStringLiteral("PART_DATE_POLICY import_delivery_date_from_ordered_at orderedAt=") << row.orderedAt
                              << QStringLiteral(" sourceType=") << input.sourceType;
        }
    }

    const auto policySnapshot = PartMarkupPolicyService::loadPolicySnapshot();
    const bool inventoryMaterial = policySnapshot.inventoryClientPrices.contains(WorkshopInventoryService::normalizedMaterialKey(row.name));
    if (inventoryMaterial && row.store.isEmpty() && input.sourceType == "manual_inline")
        row.store = QString::fromUtf8("База");
    if (inventoryMaterial && PartMarkupPolicyService::isInventoryStore(row.store) && input.sourceType == "manual_inline")
        row.name = WorkshopInventoryService::canonicalMaterialName(row.name);
    if (inventoryMaterial && PartMarkupPolicyService::isInventoryStore(row.store) && row.installedAt.isEmpty())
        row.installedAt = QDate::currentDate().toString(Qt::ISODate);
    PartPricingInput pricingInput;
    pricingInput.partName = row.name;
    pricingInput.store = row.store;
    pricingInput.qty = row.qty;
    pricingInput.purchasePrice = input.purchasePrice;
    pricingInput.manualMarkupOverride = input.manualMarkupOverride;
    pricingInput.manualMarkupPercent = input.manualMarkupOverride ? qMax(0.0, input.manualMarkupPercent) : 0.0;
    pricingInput.sourceType = input.sourceType.trimmed().isEmpty() ? QStringLiteral("manual_inline") : input.sourceType;
    const PartPricingResult pricing = PartPricingEngine::calculate(pricingInput, policySnapshot);

    row.purchasePrice = pricing.purchasePrice;
    row.purchaseAmount = pricing.purchaseAmount;
    row.clientPrice = pricing.clientPrice;
    row.clientAmount = pricing.clientAmount;
    row.markupPercent = pricing.markupPercent;
    row.manualMarkupOverride = pricing.manualMarkupOverride;
    row.markupSource = pricing.markupSource;
    row.serviceId = input.serviceId;
    return row;
}
