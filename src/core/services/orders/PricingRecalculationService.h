#pragma once

#include "core/services/MutationResult.h"

#include <QString>
#include <QStringList>

class QSqlDatabase;

namespace PricingRecalculationService {

enum class RecalculationScope {
    OpenOrdersOnly,
    AllOrders
};

struct PricingIntegrityCheckResult {
    int checkedRows = 0;
    int mismatchRows = 0;
    int manualRows = 0;
    int policyRows = 0;
    QStringList samples;
};

MutationResult recalcPartMarkupsFromSettingsDetailed(QSqlDatabase db, RecalculationScope scope);
bool recalcPartMarkupsFromSettings(QSqlDatabase db, RecalculationScope scope, QString* errorText = nullptr);
bool recalcPartMarkupsFromSettings(RecalculationScope scope, QString* errorText = nullptr);

MutationResult recalcAllPartMarkupsFromSettingsDetailed(QSqlDatabase db);
bool recalcAllPartMarkupsFromSettings(QSqlDatabase db, QString* errorText = nullptr);

MutationResult repairLegacyPricingMetadataAndFinancialSumsDetailed(QSqlDatabase db);
bool repairLegacyPricingMetadataAndFinancialSums(QSqlDatabase db, QString* errorText = nullptr);

bool checkPartPricingIntegrity(QSqlDatabase db,
                               PricingIntegrityCheckResult* result,
                               QString* errorText = nullptr);
bool checkPartPricingIntegrity(PricingIntegrityCheckResult* result,
                               QString* errorText = nullptr);

} // namespace PricingRecalculationService
