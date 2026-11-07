#pragma once

#include <QString>

namespace PlateUtils {
    QString normalizePlate(const QString& input);
    QString formatPlateDisplay(const QString& plate);
    QString formatPlateProgressive(const QString& input);
    bool isLikelyPlateInput(const QString& input);
}
