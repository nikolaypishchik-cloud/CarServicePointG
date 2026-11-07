#pragma once

#include "core/services/orders/PartMarkupPolicyService.h"

#include <QString>

struct PartPricingInput {
    QString partName;
    QString store;
    double qty = 1.0;
    double purchasePrice = 0.0;
    bool manualMarkupOverride = false;
    double manualMarkupPercent = 0.0;
    QString sourceType = QStringLiteral("manual_inline");
};

struct PartPricingResult {
    double purchasePrice = 0.0;
    double purchaseAmount = 0.0;
    double clientPrice = 0.0;
    double clientAmount = 0.0;
    double markupPercent = 0.0;
    bool manualMarkupOverride = false;
    QString markupSource = QStringLiteral("policy");
};

class PartPricingEngine
{
public:
    static PartPricingResult calculate(const PartPricingInput& input, const PartMarkupPolicyService::PolicySnapshot& policy);
};
