#include "core/services/settings/ReportsSettingsService.h"

#include "core/services/settings/FinanceSettingsService.h"
#include "core/services/settings/UiStateSettingsService.h"

namespace ReportsSettingsService {

MarkupDefaults loadMarkupDefaults()
{
    MarkupDefaults defaults;
    const auto snapshot = FinanceSettingsService::loadMarkupSnapshot();
    defaults.oil = snapshot.oilDefault;
    defaults.parts = snapshot.defaultPartsMarkupPercent;
    return defaults;
}

double loadElectricityTariff(double defaultValue)
{
    return UiStateSettingsService::loadValue(QStringLiteral("reports.expenses.electricity_tariff"), defaultValue).toDouble();
}

bool saveElectricityTariff(double tariff, bool* changed)
{
    return UiStateSettingsService::saveValue(QStringLiteral("reports.expenses.electricity_tariff"), tariff, changed);
}

} // namespace ReportsSettingsService
