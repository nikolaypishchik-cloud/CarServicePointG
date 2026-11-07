#pragma once

#include <QString>

class QSqlDatabase;

namespace PartMutationService {

bool updateInstalledAt(QSqlDatabase db,
                       int orderNumber,
                       int partId,
                       const QString& installedAtIso,
                       QString* errorText = nullptr);

bool ensureDeliveryDateIfMissing(QSqlDatabase db,
                                 int partId,
                                 const QString& deliveryDateIso,
                                 QString* errorText = nullptr);

bool updateInstalledAt(int orderNumber,
                       int partId,
                       const QString& installedAtIso,
                       QString* errorText = nullptr);

bool ensureDeliveryDateIfMissing(int partId,
                                 const QString& deliveryDateIso,
                                 QString* errorText = nullptr);

} // namespace PartMutationService
