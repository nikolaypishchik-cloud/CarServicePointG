#pragma once

#include "core/services/StatisticsSnapshotModel.h"

namespace StatisticsSnapshotService {

QString currentDatabasePath();
StatisticsSnapshot collectFromPath(const QString& dbPath, const QDate& from, const QDate& to);
StatisticsSnapshot collectCurrent(const QDate& from, const QDate& to);

} // namespace StatisticsSnapshotService
