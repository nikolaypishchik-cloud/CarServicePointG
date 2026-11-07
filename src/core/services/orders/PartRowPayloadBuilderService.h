#pragma once

#include "core/repository/PartRepository.h"

#include <QString>

struct PartRowPayloadInput {
    QString orderedAt;
    QString deliveryDate;
    QString installedAt;
    QString store;
    QString partNumber;
    QString name;
    QString note;
    QString manufacturer;
    double qty = 1.0;
    double purchasePrice = 0.0;
    bool manualMarkupOverride = false;
    double manualMarkupPercent = 0.0;
    QString sourceType = QStringLiteral("manual_inline");
    int serviceId = 0;
    bool preserveEmptyOrderedAt = false;
    bool preserveEmptyDeliveryDate = false;
};

class PartRowPayloadBuilderService
{
public:
    static WorkOrderPartRow build(const PartRowPayloadInput& input);
};
