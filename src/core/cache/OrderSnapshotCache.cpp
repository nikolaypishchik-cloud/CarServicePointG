#include "core/cache/OrderSnapshotCache.h"

#include "core/diagnostics/RuntimeLoggingControl.h"

#include <QDebug>
#include <QFileInfo>
#include <QHash>
#include <QList>
#include <QMutex>
#include <QMutexLocker>
#include <atomic>

namespace {
constexpr int kMaxCurrentEntries = 8;
constexpr int kMaxHistoryEntries = 12;
std::atomic<quint64> g_cacheRevision{1};

QMutex& cacheMutex()
{
    static QMutex m;
    return m;
}

QHash<QString, OrderSnapshotCache::CurrentOrdersSnapshot>& currentCache()
{
    static QHash<QString, OrderSnapshotCache::CurrentOrdersSnapshot> cache;
    return cache;
}

QList<QString>& currentOrder()
{
    static QList<QString> order;
    return order;
}

QHash<QString, OrderSnapshotCache::HistorySnapshot>& historyCache()
{
    static QHash<QString, OrderSnapshotCache::HistorySnapshot> cache;
    return cache;
}

QList<QString>& historyOrder()
{
    static QList<QString> order;
    return order;
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
void storeImpl(QHash<QString, T>& map, QList<QString>& order, const QString& key, const T& value, int limit)
{
    if (!map.contains(key)) order.append(key);
    map.insert(key, value);
    while (order.size() > limit) {
        const QString stale = order.takeFirst();
        map.remove(stale);
    }
}

QString normalizedCacheDbIdentity(const QString& dbPath)
{
    const QString trimmed = dbPath.trimmed();
    if (trimmed.isEmpty()) return QStringLiteral("<memory>");
    const QFileInfo info(trimmed);
    const QString canonical = info.canonicalFilePath();
    return canonical.isEmpty() ? info.absoluteFilePath() : canonical;
}
} // namespace

namespace OrderSnapshotCache {

quint64 currentRevision()
{
    return g_cacheRevision.load(std::memory_order_acquire);
}

QString makeCurrentOrdersKey(const QString& dbPath, const QString& filter)
{
    return QStringLiteral("orders|current|rev=%1|db=%2|filter=%3")
        .arg(currentRevision())
        .arg(normalizedCacheDbIdentity(dbPath), filter.trimmed().toLower());
}

QString makeHistoryKey(const QString& dbPath, const QDate& from, const QDate& to, const QString& filter)
{
    return QStringLiteral("orders|history|rev=%1|db=%2|from=%3|to=%4|filter=%5")
        .arg(currentRevision())
        .arg(normalizedCacheDbIdentity(dbPath), from.toString(Qt::ISODate), to.toString(Qt::ISODate), filter.trimmed().toLower());
}

bool tryGetCurrentOrders(const QString& key, CurrentOrdersSnapshot* out)
{
    QMutexLocker lock(&cacheMutex());
    return tryGetImpl(currentCache(), key, out);
}

void storeCurrentOrders(const QString& key, const CurrentOrdersSnapshot& snapshot)
{
    QMutexLocker lock(&cacheMutex());
    storeImpl(currentCache(), currentOrder(), key, snapshot, kMaxCurrentEntries);
}

bool tryGetHistory(const QString& key, HistorySnapshot* out)
{
    QMutexLocker lock(&cacheMutex());
    return tryGetImpl(historyCache(), key, out);
}

void storeHistory(const QString& key, const HistorySnapshot& snapshot)
{
    QMutexLocker lock(&cacheMutex());
    storeImpl(historyCache(), historyOrder(), key, snapshot, kMaxHistoryEntries);
}

void invalidateCurrentOrders(const QString& reason)
{
    bumpRevision(QStringLiteral("current_orders:%1").arg(reason));
    QMutexLocker lock(&cacheMutex());
    currentCache().clear();
    currentOrder().clear();
}

void invalidateHistory(const QString& reason)
{
    bumpRevision(QStringLiteral("history:%1").arg(reason));
    QMutexLocker lock(&cacheMutex());
    historyCache().clear();
    historyOrder().clear();
}

void invalidateAll(const QString& reason)
{
    bumpRevision(QStringLiteral("all:%1").arg(reason));
    QMutexLocker lock(&cacheMutex());
    currentCache().clear();
    currentOrder().clear();
    historyCache().clear();
    historyOrder().clear();
}

void bumpRevision(const QString& reason)
{
    const quint64 next = g_cacheRevision.fetch_add(1, std::memory_order_acq_rel) + 1;
    if (RuntimeLoggingControl::deepDebugEnabled()) {
        qInfo().noquote() << QStringLiteral("ORDER_SNAPSHOT_CACHE_REVISION_BUMP revision=") << next
                          << QStringLiteral(" reason=") << reason;
    }
}

} // namespace OrderSnapshotCache
