#include "core/cache/PartsQueryCache.h"

#include <QHash>
#include <QList>
#include <QMutex>
#include <QMutexLocker>
#include "core/diagnostics/RuntimeLoggingControl.h"

#include <QDebug>
#include <QFileInfo>
#include <atomic>

namespace {
constexpr int kMaxRowsEntries = 12;
std::atomic<quint64> g_cacheRevision{1};

QMutex& cacheMutex()
{
    static QMutex m;
    return m;
}

QHash<QString, QVector<PartRowSnapshot>>& rowsCache()
{
    static QHash<QString, QVector<PartRowSnapshot>> cache;
    return cache;
}

QList<QString>& rowsOrder()
{
    static QList<QString> order;
    return order;
}

QHash<QString, PartsQueryService::PartArchiveDateBounds>& dateBoundsCache()
{
    static QHash<QString, PartsQueryService::PartArchiveDateBounds> cache;
    return cache;
}

QString normalizedCacheDbIdentity(const QString& dbPath)
{
    const QString trimmed = dbPath.trimmed();
    if (trimmed.isEmpty()) return QStringLiteral("<memory>");
    const QFileInfo info(trimmed);
    const QString canonical = info.canonicalFilePath();
    return canonical.isEmpty() ? info.absoluteFilePath() : canonical;
}
}

namespace PartsQueryCache {

quint64 currentRevision()
{
    return g_cacheRevision.load(std::memory_order_acquire);
}

QString makeRowsKey(const QString& dbPath, const QString& filter, const QDate& from, const QDate& to)
{
    return QStringLiteral("parts|rows|rev=%1|db=%2|filter=%3|from=%4|to=%5")
        .arg(currentRevision())
        .arg(normalizedCacheDbIdentity(dbPath), filter.trimmed().toLower(), from.toString(Qt::ISODate), to.toString(Qt::ISODate));
}

QString makeDateBoundsKey(const QString& dbPath)
{
    return QStringLiteral("parts|date-bounds|rev=%1|db=%2")
        .arg(currentRevision())
        .arg(normalizedCacheDbIdentity(dbPath));
}

bool tryGetRows(const QString& key, QVector<PartRowSnapshot>* out)
{
    QMutexLocker lock(&cacheMutex());
    const auto it = rowsCache().constFind(key);
    if (it == rowsCache().cend()) return false;
    if (out) *out = it.value();
    return true;
}

void storeRows(const QString& key, const QVector<PartRowSnapshot>& rows)
{
    QMutexLocker lock(&cacheMutex());
    if (!rowsCache().contains(key)) rowsOrder().append(key);
    rowsCache().insert(key, rows);
    while (rowsOrder().size() > kMaxRowsEntries) {
        const QString stale = rowsOrder().takeFirst();
        rowsCache().remove(stale);
    }
}

bool tryGetDateBounds(const QString& key, PartsQueryService::PartArchiveDateBounds* out)
{
    QMutexLocker lock(&cacheMutex());
    const auto it = dateBoundsCache().constFind(key);
    if (it == dateBoundsCache().cend()) return false;
    if (out) *out = it.value();
    return true;
}

void storeDateBounds(const QString& key, const PartsQueryService::PartArchiveDateBounds& bounds)
{
    QMutexLocker lock(&cacheMutex());
    dateBoundsCache().insert(key, bounds);
}

void invalidate(const QString& reason)
{
    bumpRevision(reason.trimmed().isEmpty() ? QStringLiteral("parts") : reason);
    QMutexLocker lock(&cacheMutex());
    rowsCache().clear();
    rowsOrder().clear();
    dateBoundsCache().clear();
}

void bumpRevision(const QString& reason)
{
    const quint64 next = g_cacheRevision.fetch_add(1, std::memory_order_acq_rel) + 1;
    if (RuntimeLoggingControl::deepDebugEnabled()) {
        qInfo().noquote() << QStringLiteral("PARTS_QUERY_CACHE_REVISION_BUMP revision=") << next
                          << QStringLiteral(" reason=") << reason;
    }
}

} // namespace PartsQueryCache
