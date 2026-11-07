#pragma once

#include "core/services/MutationResult.h"

#include <QSqlDatabase>
#include <QString>

namespace CloudSyncOutboxService {

bool appendMutation(QSqlDatabase database, const MutationResult& result, QString* errorText = nullptr);
// Retains old empty audit envelopes without sending/applying them as business mutations.
bool retireLegacyMetadata(QSqlDatabase database, QString* errorText = nullptr);
// Queues complete envelopes for existing work orders after cloud sync is enabled.
bool enqueueInitialWorkOrderSnapshots(QSqlDatabase database = QSqlDatabase(), QString* errorText = nullptr);
bool enqueuePurchasePaymentSnapshots(QSqlDatabase database, QString* errorText = nullptr);
int pendingCount(QSqlDatabase database = QSqlDatabase(), QString* errorText = nullptr);

} // namespace CloudSyncOutboxService
