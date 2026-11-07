#include "infrastructure/db/sqlite/Db.h"
#include "infrastructure/db/sqlite/DbFacadeSupport.h"
#include "infrastructure/db/sqlite/DbArchiveInternals.h"
#include "infrastructure/db/sqlite/LegacyCanonicalImportNormalization.h"
#include "infrastructure/db/sqlite/LegacyCanonicalAncillaryImportSupport.h"
#include "infrastructure/db/sqlite/DbSchemaAuditSupport.h"
#include "infrastructure/db/sqlite/DbSettingsCacheSupport.h"
#include "infrastructure/db/sqlite/DbThreadConnectionSupport.h"
#include "infrastructure/db/sqlite/RuntimeDbPreflightValidator.h"
#include "infrastructure/db/sqlite/OrderArchiveShardSqlSupport.h"
#include "core/services/LegacyMutationLogSupport.h"
#include "core/services/OrderIdentityService.h"
#include "core/services/OrderStoragePolicyService.h"
#include "core/services/legacyimport/RuntimeRecoveryCoordinator.h"
#include "core/services/OrderArchiveRotationService.h"
#include "core/services/OrderDetailIntegrityValidator.h"
#include "core/services/legacyimport/LegacyOrderChildMapper.h"
#include "infrastructure/security/EncryptedStorage.h"
#include "runtime/AppRuntime.h"
#include "infrastructure/db/sqlite/DbCatalogImportLocalSupport.h"

#include <QCryptographicHash>
#include <QCoreApplication>
#include <QThread>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QTextStream>
#include <QUuid>
#include <QVariant>
#include <QVector>

#include <algorithm>
#include <limits>

namespace {

bool applyOpenPragmas(QSqlDatabase database, QString* errorText)
{
    if (!database.isValid()) {
        if (errorText) *errorText = QStringLiteral("Invalid database handle");
        return false;
    }
    if (!database.isOpen() && !database.open()) {
        if (errorText) *errorText = database.lastError().text();
        return false;
    }

    QSqlQuery q(database);
    auto execPragma = [&](const QString& sql) -> bool {
        if (q.exec(sql)) return true;
        if (errorText) *errorText = q.lastError().text();
        return false;
    };

    if (!execPragma(QStringLiteral("PRAGMA foreign_keys = ON;"))) return false;
    if (!execPragma(QStringLiteral("PRAGMA busy_timeout = 5000;"))) return false;
    if (!execPragma(QStringLiteral("PRAGMA journal_mode = WAL;"))) return false;
    if (!execPragma(QStringLiteral("PRAGMA synchronous = FULL;"))) return false;
    if (!execPragma(QStringLiteral("PRAGMA temp_store = MEMORY;"))) return false;
    if (!execPragma(QStringLiteral("PRAGMA cache_size = -32000;"))) return false;
    if (!execPragma(QStringLiteral("PRAGMA mmap_size = 268435456;"))) return false;
    if (!execPragma(QStringLiteral("PRAGMA optimize;"))) return false;
    return true;
}


} // namespace

bool Database::prepareRuntime(QString* errorText)
{
    if (QSqlDatabase::contains(currentConnectionName())) {
        const auto current = QSqlDatabase::database(currentConnectionName(), false);
        if (current.isOpen()) {
#ifdef Q_OS_WIN
            const auto pathCase = Qt::CaseInsensitive;
#else
            const auto pathCase = Qt::CaseSensitive;
#endif
            if (QFileInfo(current.databaseName()).absoluteFilePath().compare(QFileInfo(AppRuntime::activeDbFilePath()).absoluteFilePath(), pathCase) != 0) {
                if (errorText) *errorText = QStringLiteral("Close the previous profile before preparing another runtime path: %1 -> %2").arg(current.databaseName(), AppRuntime::activeDbFilePath());
                return false;
            }
            DbThreadConnectionSupport::registerRuntimeConnection(currentConnectionName(),current.databaseName());
            return true;
        }
        // A previously prepared, closed handle can reconnect to the same runtime
        // without recovering files beneath other live profile readers.
        if (current.isValid() && DbThreadConnectionSupport::isRuntimeConnection(currentConnectionName())
            && QFileInfo(current.databaseName())==QFileInfo(AppRuntime::activeDbFilePath())
            && QFileInfo::exists(current.databaseName())) return true;
    }

    // Recovery can replace runtime files. Workers must only open the runtime
    // already prepared by the UI startup, never recover it under live readers.
    if (QCoreApplication::instance() && QThread::currentThread() != QCoreApplication::instance()->thread()) {
        const QString preparedPath = DbThreadConnectionSupport::trackedConnectionPath(mainConnectionName());
        if (!preparedPath.isEmpty() && QFileInfo::exists(preparedPath)
            && QFileInfo(preparedPath).absoluteFilePath() == QFileInfo(AppRuntime::activeDbFilePath()).absoluteFilePath()) return true;
        if (errorText) *errorText = QStringLiteral("Runtime must be prepared on the main thread before opening a worker connection");
        return false;
    }

    if (!RuntimeRecoveryCoordinator::prepareValidatedRuntime(errorText)) {
        if (errorText && errorText->trimmed().isEmpty()) {
            *errorText = QStringLiteral("Encrypted runtime prepare failed");
        }
        qCritical() << "Encrypted runtime prepare failed:" << (errorText ? *errorText : QString());
        return false;
    }

    AppRuntime::ensureDataAtRestProtection();
    AppRuntime::protectPathAtRest(AppRuntime::sealedActiveDbFilePath());
    AppRuntime::protectPathAtRest(AppRuntime::sealedArchiveDbFilePath());
    AppRuntime::protectPathAtRest(AppRuntime::sealedLegacyDbFilePath());
    AppRuntime::protectPathAtRest(AppRuntime::sealedRegistryDbFilePath());
    AppRuntime::protectPathAtRest(AppRuntime::masterKeyFilePath());
    return true;
}

bool Database::openPrepared(QString* errorText)
{
    const QString connName = currentConnectionName();
    const QString expectedPath = QFileInfo(AppRuntime::activeDbFilePath()).absoluteFilePath();
    QSqlDatabase d;
    if (QSqlDatabase::contains(connName)) {
        d = QSqlDatabase::database(connName, false);
        if (d.isOpen()) {
#ifdef Q_OS_WIN
            const auto pathCase = Qt::CaseInsensitive;
#else
            const auto pathCase = Qt::CaseSensitive;
#endif
            if (QFileInfo(d.databaseName()).absoluteFilePath().compare(expectedPath, pathCase) != 0) {
                if (errorText) *errorText = QStringLiteral("Close the previous profile before opening another database path");
                return false;
            }
            DbThreadConnectionSupport::registerRuntimeConnection(connName, d.databaseName());
            return true;
        }
    } else {
        d = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
        d.setDatabaseName(AppRuntime::activeDbFilePath());
        DbThreadConnectionSupport::registerRuntimeConnection(connName, AppRuntime::activeDbFilePath());
        if (connName == mainConnectionName()) DbSettingsCacheSupport::clear();
    }

    if (!d.isValid()) {
        if (errorText) *errorText = QStringLiteral("Invalid DB connection handle");
        qCritical() << "DB open failed: invalid connection handle";
        return false;
    }

    d.setDatabaseName(AppRuntime::activeDbFilePath());
    DbThreadConnectionSupport::registerRuntimeConnection(connName, AppRuntime::activeDbFilePath());
    if (connName == mainConnectionName()) DbSettingsCacheSupport::clear();
    if (!d.open()) {
        if (errorText) *errorText = d.lastError().text();
        qCritical() << "DB open failed:" << d.lastError().text();
        return false;
    }
    if (!applyOpenPragmas(d, errorText)) {
        qCritical() << "DB pragma init failed:" << (errorText ? *errorText : QString());
        return false;
    }
    return true;
}

bool Database::open(QString* errorText)
{
    if (!prepareRuntime(errorText)) return false;
    return openPrepared(errorText);
}

bool Database::ensureConnectionAlive(QString* errorText)
{
    const QString connName = currentConnectionName();
    // A caller-supplied DAO connection is not a request to prepare a different
    // application runtime. Only the runtime APIs adopt a handle into a profile.
    if (QSqlDatabase::contains(connName) && !DbThreadConnectionSupport::isRuntimeConnection(connName)) {
        const auto supplied=QSqlDatabase::database(connName,false);
        if (!supplied.isValid()) {
            if (errorText) *errorText=QStringLiteral("Caller-owned database connection is invalid");
            return false;
        }
        if (supplied.isOpen()) return true;
        if (errorText) *errorText=QStringLiteral("Caller-owned database connection must be reopened by its owner");
        return false;
    }
    if (!prepareRuntime(errorText)) return false;
    if (!QSqlDatabase::contains(connName)) {
        return openPrepared(errorText);
    }
    QSqlDatabase d = QSqlDatabase::database(connName, false);
    if (!d.isValid()) {
        close();
        return openPrepared(errorText);
    }
    if (!d.isOpen()) return openPrepared(errorText);
    return true;
}


bool Database::tableExists(const QString& table)
{
    QSqlQuery q(db());
    q.prepare("SELECT name FROM sqlite_master WHERE type='table' AND name=?");
    q.addBindValue(table);
    if (!q.exec()) {
        qWarning() << "tableExists query failed:" << q.lastError().text();
        return false;
    }
    return q.next();
}

bool Database::ensureColumn(const QString& table, const QString& colName, const QString& ddl)
{
    if (!tableExists(table)) return true;

    QSqlQuery q(db());
    if (!q.exec(QString("PRAGMA table_info(%1)").arg(table))) {
        qWarning() << "PRAGMA table_info failed" << table << q.lastError().text();
        return false;
    }
    bool has = false;
    while (q.next()) {
        if (q.value(1).toString() == colName) { has = true; break; }
    }
    if (has) return true;

    QSqlQuery a(db());
    const QString sql = QString("ALTER TABLE %1 ADD COLUMN %2;").arg(table, ddl);
    if (!a.exec(sql)) {
        qWarning() << "ALTER TABLE failed" << sql << a.lastError().text();
        return false;
    }
    return true;
}
