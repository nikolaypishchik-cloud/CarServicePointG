#pragma once

#include <QString>

namespace AppRuntimeService {

bool ensureConnectionAlive(QString* errorText = nullptr);
void closeThreadConnection();
// Capture on the caller thread before dispatching a file-based background task.
QString activeDatabasePath();
QString resolveOrderUid(int orderNumber);
bool hardDeleteOrder(int orderNumber, QString* errorText = nullptr);
bool loadSyncAutoPartsEnabled();
bool setPartPaid(int partId, bool paid);
void refreshOrderRegistry(int orderNumber);

} // namespace AppRuntimeService
