#include "infrastructure/db/sqlite/DbSettingsCacheSupport.h"

#include <QHash>
#include <QMutex>
#include <QMutexLocker>

namespace {
QMutex& cacheMutex()
{
    static QMutex m;
    return m;
}

QHash<QString, QVariant>& cacheStore()
{
    static QHash<QString, QVariant> cache;
    return cache;
}
}

namespace DbSettingsCacheSupport {

QVariant tryGet(const QString& key, bool* found)
{
    QMutexLocker lock(&cacheMutex());
    const auto it = cacheStore().constFind(key);
    const bool has = (it != cacheStore().cend());
    if (found) *found = has;
    return has ? it.value() : QVariant();
}

void store(const QString& key, const QVariant& value)
{
    QMutexLocker lock(&cacheMutex());
    cacheStore().insert(key, value);
}

void remove(const QString& key)
{
    QMutexLocker lock(&cacheMutex());
    cacheStore().remove(key);
}

void clear()
{
    QMutexLocker lock(&cacheMutex());
    cacheStore().clear();
}

} // namespace DbSettingsCacheSupport
