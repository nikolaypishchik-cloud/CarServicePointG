#include "infrastructure/db/sqlite/ScopedConnectionManager.h"
#include "infrastructure/db/sqlite/DbThreadConnectionSupport.h"

#include <QDateTime>
#include <QFileInfo>
#include <QSqlError>
#include <QStringList>
#include <QDebug>
#include <QThread>
#include <atomic>

namespace {

std::atomic_int g_safeRemoveBlockedCount{0};
std::atomic<quint64> g_scopedConnectionSequence{0};

QString makeScopedConnectionName(const QString& purpose)
{
    // P0 DB lifecycle hardening: millisecond-only connection names can collide
    // when several worker jobs open SQLite handles in the same millisecond. A
    // collision reuses/removes an active connection and can later produce
    // QSqlDatabasePrivate::removeDatabase warnings. Include an atomic sequence
    // and the owning thread id so every scoped connection is unique.
    const QString safePurpose = purpose.trimmed().isEmpty() ? QStringLiteral("sqlite") : purpose.trimmed();
    const quint64 seq = ++g_scopedConnectionSequence;
    const quintptr threadToken = reinterpret_cast<quintptr>(QThread::currentThread());
    return QStringLiteral("scoped_%1_thread_%2_seq_%3_ts_%4")
        .arg(safePurpose)
        .arg(QString::number(threadToken, 16))
        .arg(seq)
        .arg(QDateTime::currentMSecsSinceEpoch());
}

} // namespace


namespace SqliteConnectionUtils {

void safeCloseAndRemoveDatabase(const QString& connectionName)
{
    const QString nameCopy = connectionName.trimmed();
    if (nameCopy.isEmpty() || !QSqlDatabase::contains(nameCopy)) return;
    QString dbPath = DbThreadConnectionSupport::trackedConnectionPath(nameCopy);
    bool wasOpen = false;
    if (DbThreadConnectionSupport::isConnectionOwnedByCurrentThread(nameCopy)) {
        {
            QSqlDatabase db = QSqlDatabase::database(nameCopy, false);
            if (db.isValid()) {
                dbPath = QFileInfo(db.databaseName()).absoluteFilePath();
                wasOpen = db.isOpen();
                if (db.isOpen()) db.close();
            }
            db = QSqlDatabase();
        }
        QSqlDatabase::removeDatabase(nameCopy);
        DbThreadConnectionSupport::unregisterConnectionName(nameCopy);
        if (QSqlDatabase::contains(nameCopy)) {
            const int hit = ++g_safeRemoveBlockedCount;
            qWarning() << "safeCloseAndRemoveDatabase blocked" << nameCopy << "dbPath" << dbPath << "wasOpen" << wasOpen << "hit" << hit;
        }
        return;
    }

    // Never call QSqlDatabase::database() for a handle that is registered to a
    // different thread. Qt logs noisy warnings and can stall on connection
    // bookkeeping; the owning thread must close that connection.
    const int hit = ++g_safeRemoveBlockedCount;
    qWarning() << "safeCloseAndRemoveDatabase skipped foreign-thread connection" << nameCopy << "dbPath" << dbPath << "hit" << hit;
}

int countOpenConnectionsForPath(const QString& dbPath, const QString& excludeConnectionName)
{
    const QString target = QFileInfo(dbPath).absoluteFilePath();
    if (target.isEmpty()) return 0;
    const QString exclude = excludeConnectionName.trimmed();
    int count = 0;
    const QStringList names = QSqlDatabase::connectionNames();
    for (const QString& name : names) {
        if (!exclude.isEmpty() && name == exclude) continue;
        if (!QSqlDatabase::contains(name)) continue;

        if (DbThreadConnectionSupport::isConnectionOwnedByCurrentThread(name)) {
            QSqlDatabase db = QSqlDatabase::database(name, false);
            if (!db.isValid() || !db.isOpen()) continue;
            const QString absolutePath = QFileInfo(db.databaseName()).absoluteFilePath();
            DbThreadConnectionSupport::updateConnectionPath(name, absolutePath);
            if (absolutePath == target) ++count;
            continue;
        }

        // Cross-thread QSqlDatabase handles must not be opened with
        // QSqlDatabase::database(name). Use the tracked metadata instead.
        const QString rawTrackedPath = DbThreadConnectionSupport::trackedConnectionPath(name);
        if (rawTrackedPath.trimmed().isEmpty()) continue;
        const QString trackedPath = QFileInfo(rawTrackedPath).absoluteFilePath();
        if (!trackedPath.isEmpty() && trackedPath == target) ++count;
    }
    return count;
}

bool hasOtherOpenConnectionsForPath(const QString& dbPath, const QString& excludeConnectionName)
{
    return countOpenConnectionsForPath(dbPath, excludeConnectionName) > 0;
}

} // namespace SqliteConnectionUtils

ScopedSqliteConnection::ScopedSqliteConnection(const QString& dbPath,
                                               const QString& purpose,
                                               QString* errorText, bool readOnly)
    : m_connectionName(makeScopedConnectionName(purpose))
{
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    db.setDatabaseName(dbPath);
    if(readOnly) db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
    DbThreadConnectionSupport::registerConnectionName(m_connectionName, dbPath);
    if (!db.open()) {
        if (errorText) *errorText = db.lastError().text().trimmed();
        db = QSqlDatabase(); // Release the local handle before removing the connection.
        SqliteConnectionUtils::safeCloseAndRemoveDatabase(m_connectionName);
        return;
    }
    m_open = true;
}

ScopedSqliteConnection::~ScopedSqliteConnection()
{
    SqliteConnectionUtils::safeCloseAndRemoveDatabase(m_connectionName);
}

bool ScopedSqliteConnection::isOpen() const
{
    return m_open && QSqlDatabase::contains(m_connectionName) && QSqlDatabase::database(m_connectionName, false).isOpen();
}

QSqlDatabase ScopedSqliteConnection::db() const
{
    return QSqlDatabase::database(m_connectionName, false);
}

QString ScopedSqliteConnection::connectionName() const
{
    return m_connectionName;
}
