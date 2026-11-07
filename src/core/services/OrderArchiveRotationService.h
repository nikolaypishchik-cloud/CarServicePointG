#pragma once

#include <QString>

namespace OrderArchiveRotationService {

int activeClosedOrderLimit();
bool enforcePolicy(QString* errorText = nullptr);
bool archiveSingleClosedOrder(int internalOrderNumber, QString* errorText = nullptr);
bool cleanupHydratedArchiveCopies(QString* errorText = nullptr);

} // namespace OrderArchiveRotationService
