#pragma once

#include <QString>
#include <QtGlobal>

#include "core/services/ReportsDataLoader.h"
#include "core/services/StatisticsSnapshotModel.h"

namespace ReportsSnapshotCache {

quint64 currentRevision();
QString makeReportsKey(const QString& family, const QString& dbPath, const QString& fromIso, const QString& toIso, const QString& extra = QString());
QString makeStatsKey(const QString& dbPath, const QString& fromIso, const QString& toIso);

bool tryGetPeriod(const QString& key, PeriodSnapshot* out);
void storePeriod(const QString& key, const PeriodSnapshot& snapshot);

bool tryGetOrders(const QString& key, OrdersSnapshot* out);
void storeOrders(const QString& key, const OrdersSnapshot& snapshot);

bool tryGetParts(const QString& key, PartsSnapshot* out);
void storeParts(const QString& key, const PartsSnapshot& snapshot);

bool tryGetOutsource(const QString& key, OutsourceSnapshot* out);
void storeOutsource(const QString& key, const OutsourceSnapshot& snapshot);

bool tryGetExpenses(const QString& key, ExpensesSnapshot* out);
void storeExpenses(const QString& key, const ExpensesSnapshot& snapshot);

bool tryGetTotalExpenses(const QString& key, TotalExpensesSnapshot* out);
void storeTotalExpenses(const QString& key, const TotalExpensesSnapshot& snapshot);

bool tryGetPayroll(const QString& key, PayrollSnapshot* out);
void storePayroll(const QString& key, const PayrollSnapshot& snapshot);

bool tryGetStats(const QString& key, StatisticsSnapshot* out);
void storeStats(const QString& key, const StatisticsSnapshot& snapshot);

void invalidateReports();
void invalidateStats();
void clearAll();
void bumpRevision(const QString& reason = QString());

} // namespace ReportsSnapshotCache
