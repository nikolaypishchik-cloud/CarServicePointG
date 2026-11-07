#include "core/services/orders/PartPricingEngine.h"
#include "core/diagnostics/RuntimeLoggingControl.h"

#include "core/services/orders/PartMarkupPolicyService.h"
#include "core/util/FormatUtils.h"
#include "core/services/WorkshopInventoryService.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QtGlobal>

PartPricingResult PartPricingEngine::calculate(const PartPricingInput& input, const PartMarkupPolicyService::PolicySnapshot& policy)
{
    QElapsedTimer timer;
    timer.start();

    const double safeQty = input.qty > 0.0 ? input.qty : 1.0;
    const bool inventory=PartMarkupPolicyService::isInventoryStore(input.store);
    // Compatibility reads/recalculations retain their previous unit-price rule.
    // New quotes must not round the unit price to cents before multiplication.
    const bool legacy = input.sourceType == "report" || input.sourceType == "recalc"
        || input.sourceType == "integrity_dry_run" || input.sourceType == "legacy_recalc"
        || input.sourceType == "legacy_save";
    const double purchase = inventory || !legacy ? qMax(0.0,input.purchasePrice)
        : FormatUtils::roundMoneyValue(qMax(0.0, input.purchasePrice));

    PartPricingResult result;
    if (PartMarkupPolicyService::isCustomerSupplied(input.store)) {
        result.markupSource = QStringLiteral("customer");
        return result;
    }
    result.purchasePrice = purchase;
    result.purchaseAmount = FormatUtils::roundMoneyValue(safeQty * result.purchasePrice);
    result.manualMarkupOverride = input.manualMarkupOverride;

    if (input.manualMarkupOverride) {
        result.markupPercent = qMax(0.0, input.manualMarkupPercent);
        result.markupSource = QStringLiteral("manual");
    } else {
        result.markupPercent = qMax(0.0, PartMarkupPolicyService::effectivePartMarkupPercent(input.partName, input.store, policy));
        result.markupSource = QStringLiteral("policy");
    }

    result.clientPrice = FormatUtils::roundMoneyValue(result.purchasePrice * (1.0 + result.markupPercent / 100.0));
    if(inventory || !legacy)result.clientPrice=result.purchasePrice*(1.0+result.markupPercent/100.0);
    const auto inventoryPrice=policy.inventoryClientPrices.constFind(WorkshopInventoryService::normalizedMaterialKey(input.partName));
    if (PartMarkupPolicyService::isInventoryStore(input.store)
        && inventoryPrice!=policy.inventoryClientPrices.cend()) {
        const QString key = WorkshopInventoryService::normalizedMaterialKey(input.partName);
        const bool storedSnapshot = input.sourceType == "report" || input.sourceType == "recalc"
            || input.sourceType == "integrity_dry_run" || input.sourceType == "legacy_recalc";
        const double cost = !storedSnapshot && policy.inventoryPurchasePrices.contains(key)
            ? policy.inventoryPurchasePrices.value(key) : input.purchasePrice;
        result.purchasePrice=qMax(0.0,cost);
        result.purchaseAmount=FormatUtils::roundMoneyValue(safeQty*result.purchasePrice);
        result.clientPrice=*inventoryPrice;
        result.markupPercent=result.purchasePrice>0.0 ? qMax(0.0,(result.clientPrice/result.purchasePrice-1.0)*100.0) : 0.0;
        result.markupSource=QStringLiteral("inventory");
        result.manualMarkupOverride=false;
    }
    result.clientAmount = FormatUtils::roundMoneyValue(safeQty * result.clientPrice);

    if (RuntimeLoggingControl::deepDebugEnabled()) qInfo().noquote() << QStringLiteral("PART_PRICING op=calculate order=0 partId=0 trace=%1 name=\"%2\" store=\"%3\" qty=%4 purchase=%5 client=%6 activePct=%7 source=%8 override=%9 elapsedMs=%10")
        .arg(input.sourceType)
        .arg(input.partName)
        .arg(input.store)
        .arg(safeQty, 0, 'f', 3)
        .arg(result.purchasePrice, 0, 'f', 2)
        .arg(result.clientPrice, 0, 'f', 2)
        .arg(result.markupPercent, 0, 'f', 2)
        .arg(result.markupSource)
        .arg(result.manualMarkupOverride ? 1 : 0)
        .arg(timer.elapsed());

    return result;
}
