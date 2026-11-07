#pragma once

namespace ReportsSettingsService {

struct MarkupDefaults {
    double parts = 0.0;
    double oil = 0.0;
};

MarkupDefaults loadMarkupDefaults();
double loadElectricityTariff(double defaultValue = 0.0);
bool saveElectricityTariff(double tariff, bool* changed = nullptr);

} // namespace ReportsSettingsService
