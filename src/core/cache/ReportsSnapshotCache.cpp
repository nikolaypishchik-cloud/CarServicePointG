#include "core/cache/ReportsSnapshotCache.h"

#include <QHash>
#include <QList>
#include <QMutex>
#include <QMutexLocker>
#include <QDebug>
#include <atomic>

namespace {
constexpr int kMaxEntriesPerFamily = 16;
std::atomic<quint64> g_cacheRevision{1};

struct CacheState {
    QHash<QString, PeriodSnapshot> period;
    QList<QString> periodOrder;
    QHash<QString, OrdersSnapshot> orders;
    QList<QString> ordersOrder;
    QHash<QString, PartsSnapshot> parts;
    QList<QString> partsOrder;
    QHash<QString, OutsourceSnapshot> outsource;
    QList<QString> outsourceOrder;
    QHash<QString, ExpensesSnapshot> expenses;
    QList<QString> expensesOrder;
    QHash<QString, TotalExpensesSnapshot> totalExpenses;
    QList<QString> totalExpensesOrder;
    QHash<QString, PayrollSnapshot> payroll;
    QList<QString> payrollOrder;
    QHash<QString, StatisticsSnapshot> stats;
    QList<QString> statsOrder;
};

QMutex& cacheMutex()
{
    static QMutex m;
    return m;
}

CacheState& cacheState()
{
    static CacheState s;
    return s;
}

template <typename T>
bool tryGetImpl(const QHash<QString, T>& map, const QString& key, T* out)
{
    const auto it = map.constFind(key);
    if (it == map.cend()) return false;
    if (out) *out = it.value();
    return true;
}

template <typename T>
void storeImpl(QHash<QString, T>& map, QList<QString>& order, const QString& key, const T& value)
{
    if (!map.contains(key)) {
        order.append(key);
    }
    map.insert(key, value);
    while (order.size() > kMaxEntriesPerFamily) {
        const QString stale = order.takeFirst();
        map.remove(stale);
    }
}

template <typename T>
void clearFamily(QHash<QString, T>& map, QList<QString>& order)
{
    map.clear();
    order.clear();
}

} // namespace

namespace ReportsSnapshotCache {

quint64 currentRevision()
{
    return g_cacheRevision.load(std::memory_order_acquire);
}

QString makeReportsKey(const QString& family, const QString& dbPath, const QString& fromIso, const QString& toIso, const QString& extra)
{
    return QStringLiteral("reports|rev=%1|family=%2|db=%3|from=%4|to=%5|scope=%6")
        .arg(currentRevision())
        .arg(family.trimmed(), dbPath.trimmed(), fromIso.trimmed(), toIso.trimmed(), extra.trimmed());
}

QString makeStatsKey(const QString& dbPath, const QString& fromIso, const QString& toIso)
{
    return QStringLiteral("stats|rev=%1|db=%2|from=%3|to=%4")
        .arg(currentRevision())
        .arg(dbPath.trimmed(), fromIso.trimmed(), toIso.trimmed());
}

bool tryGetPeriod(const QString& key, PeriodSnapshot* out)
{
    QMutexLocker lock(&cacheMutex());
    return tryGetImpl(cacheState().period, key, out);
}

void storePeriod(const QString& key, const PeriodSnapshot& snapshot)
{
    QMutexLocker lock(&cacheMutex());
    storeImpl(cacheState().period, cacheState().periodOrder, key, snapshot);
}

bool tryGetOrders(const QString& key, OrdersSnapshot* out)
{
    QMutexLocker lock(&cacheMutex());
    return tryGetImpl(cacheState().orders, key, out);
}

void storeOrders(const QString& key, const OrdersSnapshot& snapshot)
{
    QMutexLocker lock(&cacheMutex());
    storeImpl(cacheState().orders, cacheState().ordersOrder, key, snapshot);
}

bool tryGetParts(const QString& key, PartsSnapshot* out)
{
    QMutexLocker lock(&cacheMutex());
    return tryGetImpl(cacheState().parts, key, out);
}

void storeParts(const QString& key, const PartsSnapshot& snapshot)
{
    QMutexLocker lock(&cacheMutex());
    if(!snapshot.error.isEmpty()){cacheState().parts.remove(key);cacheState().partsOrder.removeAll(key);return;}
    storeImpl(cacheState().parts, cacheState().partsOrder, key, snapshot);
}

bool tryGetOutsource(const QString& key, OutsourceSnapshot* out)
{
    QMutexLocker lock(&cacheMutex());
    return tryGetImpl(cacheState().outsource, key, out);
}

void storeOutsource(const QString& key, const OutsourceSnapshot& snapshot)
{
    if (!snapshot.error.isEmpty()) return;
    QMutexLocker lock(&cacheMutex());
    storeImpl(cacheState().outsource, cacheState().outsourceOrder, key, snapshot);
}

bool tryGetExpenses(const QString& key, ExpensesSnapshot* out)
{
    QMutexLocker lock(&cacheMutex());
    return tryGetImpl(cacheState().expenses, key, out);
}

void storeExpenses(const QString& key, const ExpensesSnapshot& snapshot)
{
    if (!snapshot.error.isEmpty()) return;
    QMutexLocker lock(&cacheMutex());
    storeImpl(cacheState().expenses, cacheState().expensesOrder, key, snapshot);
}

bool tryGetTotalExpenses(const QString& key, TotalExpensesSnapshot* out)
{
    QMutexLocker lock(&cacheMutex());
    return tryGetImpl(cacheState().totalExpenses, key, out);
}

void storeTotalExpenses(const QString& key, const TotalExpensesSnapshot& snapshot)
{
    if (!snapshot.error.isEmpty()) return;
    QMutexLocker lock(&cacheMutex());
    storeImpl(cacheState().totalExpenses, cacheState().totalExpensesOrder, key, snapshot);
}

bool tryGetPayroll(const QString& key, PayrollSnapshot* out)
{
    QMutexLocker lock(&cacheMutex());
    return tryGetImpl(cacheState().payroll, key, out);
}

void storePayroll(const QString& key, const PayrollSnapshot& snapshot)
{
    QMutexLocker lock(&cacheMutex());
    if(!snapshot.error.isEmpty()){cacheState().payroll.remove(key);cacheState().payrollOrder.removeAll(key);return;}
    storeImpl(cacheState().payroll, cacheState().payrollOrder, key, snapshot);
}

bool tryGetStats(const QString& key, StatisticsSnapshot* out)
{
    QMutexLocker lock(&cacheMutex());
    return tryGetImpl(cacheState().stats, key, out);
}

void storeStats(const QString& key, const StatisticsSnapshot& snapshot)
{
    QMutexLocker lock(&cacheMutex());
    storeImpl(cacheState().stats, cacheState().statsOrder, key, snapshot);
}

void invalidateReports()
{
    bumpRevision(QStringLiteral("reports"));
    QMutexLocker lock(&cacheMutex());
    clearFamily(cacheState().period, cacheState().periodOrder);
    clearFamily(cacheState().orders, cacheState().ordersOrder);
    clearFamily(cacheState().parts, cacheState().partsOrder);
    clearFamily(cacheState().outsource, cacheState().outsourceOrder);
    clearFamily(cacheState().expenses, cacheState().expensesOrder);
    clearFamily(cacheState().totalExpenses, cacheState().totalExpensesOrder);
    clearFamily(cacheState().payroll, cacheState().payrollOrder);
}

void invalidateStats()
{
    bumpRevision(QStringLiteral("stats"));
    QMutexLocker lock(&cacheMutex());
    clearFamily(cacheState().stats, cacheState().statsOrder);
}

void clearAll()
{
    bumpRevision(QStringLiteral("all"));
    QMutexLocker lock(&cacheMutex());
    clearFamily(cacheState().period, cacheState().periodOrder);
    clearFamily(cacheState().orders, cacheState().ordersOrder);
    clearFamily(cacheState().parts, cacheState().partsOrder);
    clearFamily(cacheState().outsource, cacheState().outsourceOrder);
    clearFamily(cacheState().expenses, cacheState().expensesOrder);
    clearFamily(cacheState().totalExpenses, cacheState().totalExpensesOrder);
    clearFamily(cacheState().payroll, cacheState().payrollOrder);
    clearFamily(cacheState().stats, cacheState().statsOrder);
}

void bumpRevision(const QString& reason)
{
    const quint64 next = g_cacheRevision.fetch_add(1, std::memory_order_acq_rel) + 1;
    qInfo().noquote() << QStringLiteral("REPORTS_SNAPSHOT_CACHE_REVISION_BUMP revision=") << next
                      << QStringLiteral(" reason=") << reason;
}

} // namespace ReportsSnapshotCache
