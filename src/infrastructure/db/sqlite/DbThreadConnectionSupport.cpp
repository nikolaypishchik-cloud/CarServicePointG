#include "infrastructure/db/sqlite/DbThreadConnectionSupport.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QThread>

namespace {
QMutex& registryMutex()
{
    static QMutex mutex;
    return mutex;
}

struct ConnectionRecord {
    QString databasePath;
    QString ownerThreadToken;
    bool runtimeManaged = false;
};

QHash<QString, ConnectionRecord>& registry()
{
    static QHash<QString, ConnectionRecord> records;
    return records;
}

QString currentThreadToken()
{
    return QString::number(reinterpret_cast<quintptr>(QThread::currentThread()), 16);
}

QString mainThreadToken()
{
    if (auto* app = QCoreApplication::instance()) {
        return QString::number(reinterpret_cast<quintptr>(app->thread()), 16);
    }
    return QStringLiteral("main");
}

QString normalizePath(const QString& databasePath)
{
    const QString trimmed = databasePath.trimmed();
    if (trimmed.isEmpty()) return QString();
    return QFileInfo(trimmed).absoluteFilePath();
}

bool nameLooksOwnedByCurrentThread(const QString& name)
{
    const QString token = currentThreadToken();
    return name.contains(QStringLiteral("_thread_%1").arg(token));
}
} // namespace

namespace DbThreadConnectionSupport {

QString currentConnectionName(const QString& baseName)
{
    const QString safeBase = baseName.trimmed().isEmpty() ? QStringLiteral("carservice") : baseName.trimmed();
    const QString currentToken = currentThreadToken();
    if (currentToken == mainThreadToken()) return safeBase;
    return safeBase + QStringLiteral("_thread_") + currentToken;
}

QStringList trackedConnectionNames(const QString& baseName)
{
    const QString prefix = baseName.trimmed();
    QMutexLocker lock(&registryMutex());
    QStringList out;
    for (auto it = registry().constBegin(); it != registry().constEnd(); ++it) {
        const QString& name = it.key();
        if (name == prefix || name.startsWith(prefix + QStringLiteral("_thread_"))) out.push_back(name);
    }
    return out;
}

void registerConnectionName(const QString& name)
{
    registerConnectionName(name, QString());
}

void registerConnectionName(const QString& name, const QString& databasePath)
{
    const QString safeName = name.trimmed();
    if (safeName.isEmpty()) return;
    QMutexLocker lock(&registryMutex());
    ConnectionRecord record = registry().value(safeName);
    record.ownerThreadToken = currentThreadToken();
    const QString normalized = normalizePath(databasePath);
    if (!normalized.isEmpty()) record.databasePath = normalized;
    registry().insert(safeName, record);
}

void registerRuntimeConnection(const QString& name, const QString& databasePath)
{
    const QString safeName=name.trimmed();
    if (safeName.isEmpty()) return;
    QMutexLocker lock(&registryMutex());
    auto record=registry().value(safeName);
    record.ownerThreadToken=currentThreadToken();
    const QString normalized=normalizePath(databasePath);
    if (!normalized.isEmpty()) record.databasePath=normalized;
    record.runtimeManaged=true;
    registry().insert(safeName,record);
}

bool isRuntimeConnection(const QString& name)
{
    QMutexLocker lock(&registryMutex());
    return registry().value(name.trimmed()).runtimeManaged;
}

void updateConnectionPath(const QString& name, const QString& databasePath)
{
    const QString safeName = name.trimmed();
    if (safeName.isEmpty()) return;
    const QString normalized = normalizePath(databasePath);
    QMutexLocker lock(&registryMutex());
    ConnectionRecord record = registry().value(safeName);
    if (record.ownerThreadToken.isEmpty()) record.ownerThreadToken = currentThreadToken();
    if (!normalized.isEmpty()) record.databasePath = normalized;
    registry().insert(safeName, record);
}

void unregisterConnectionName(const QString& name)
{
    const QString safeName = name.trimmed();
    if (safeName.isEmpty()) return;
    QMutexLocker lock(&registryMutex());
    registry().remove(safeName);
}

void clear()
{
    QMutexLocker lock(&registryMutex());
    registry().clear();
}

bool isConnectionOwnedByCurrentThread(const QString& name)
{
    const QString safeName = name.trimmed();
    if (safeName.isEmpty()) return false;
    QMutexLocker lock(&registryMutex());
    const auto it = registry().constFind(safeName);
    if (it != registry().constEnd()) {
        return it.value().ownerThreadToken == currentThreadToken();
    }
    if (safeName.contains(QStringLiteral("_thread_"))) return nameLooksOwnedByCurrentThread(safeName);
    // Legacy one-off connection names do not encode the owner thread. Treat
    // them as local so existing scoped QtSql helpers can still close/remove
    // their own handles; long-lived app connections are registered with an
    // explicit owner before cross-thread audits run.
    return true;
}

QString trackedConnectionPath(const QString& name)
{
    const QString safeName = name.trimmed();
    if (safeName.isEmpty()) return QString();
    QMutexLocker lock(&registryMutex());
    const auto it = registry().constFind(safeName);
    if (it == registry().constEnd()) return QString();
    return it.value().databasePath;
}

} // namespace DbThreadConnectionSupport
