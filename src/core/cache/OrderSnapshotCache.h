#pragma once

#include "core/readmodel/TabularReadModel.h"
#include "core/services/HistoryQueryService.h"
#include "core/services/orders/CurrentOrdersQueryService.h"

#include <QDate>
#include <QString>
#include <QtGlobal>

namespace OrderSnapshotCache {

struct CurrentOrdersSnapshot {
    CurrentOrdersQueryResult queryResult;
    TabularReadModel readModel;
};

struct HistorySnapshot {
    HistoryQueryResult queryResult;
    TabularReadModel readModel;
};

quint64 currentRevision();
QString makeCurrentOrdersKey(const QString& dbPath, const QString& filter);
QString makeHistoryKey(const QString& dbPath, const QDate& from, const QDate& to, const QString& filter);

bool tryGetCurrentOrders(const QString& key, CurrentOrdersSnapshot* out);
void storeCurrentOrders(const QString& key, const CurrentOrdersSnapshot& snapshot);

bool tryGetHistory(const QString& key, HistorySnapshot* out);
void storeHistory(const QString& key, const HistorySnapshot& snapshot);

void invalidateCurrentOrders(const QString& reason = QString());
void invalidateHistory(const QString& reason = QString());
void invalidateAll(const QString& reason = QString());
void bumpRevision(const QString& reason = QString());

} // namespace OrderSnapshotCache
