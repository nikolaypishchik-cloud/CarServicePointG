#include "core/util/DateFormats.h"
#include "infrastructure/db/sqlite/DbFacadeSupport.h"
#include "core/diagnostics/RuntimeLoggingControl.h"

#include "infrastructure/db/sqlite/DbThreadConnectionSupport.h"
#include "infrastructure/db/sqlite/OrderArchiveShardSqlSupport.h"
#include "runtime/RuntimeSettings.h"

#include <QDate>
#include <QDateTime>
#include <QDebug>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace {
constexpr const char* kConnName = "carservice_main";
constexpr const char* kRegistryBaseConnName = "carservice_registry";
}

QString dbRegistryConnectionName()
{
    return DbThreadConnectionSupport::currentConnectionName(QString::fromLatin1(kRegistryBaseConnName));
}

QString mainConnectionName()
{
    return QString::fromLatin1(kConnName);
}

QString currentConnectionName()
{
    return DbThreadConnectionSupport::currentConnectionName(QString::fromLatin1(kConnName));
}

bool shouldMirrorSettingToQSettings(const QString& key)
{
    return !key.trimmed().isEmpty();
}

QVariant qSettingsFallbackValue(const QString& key)
{
    if (!shouldMirrorSettingToQSettings(key)) return QVariant();
    return RuntimeSettings::get(key, QVariant());
}

void mirrorSettingToQSettings(const QString& key, const QVariant& value)
{
    if (!shouldMirrorSettingToQSettings(key)) return;
    RuntimeSettings::set(key, value);
}

QString normalizedMasterDate(QString value)
{
    const QDate parsed = DateFormats::parseDate(value);
    if (parsed.isValid()) return parsed.toString(Qt::ISODate);
    value = value.trimmed();
    if (value.isEmpty()) return QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"));

    const QString raw10 = value.left(10);
    const QStringList formats = {
        QStringLiteral("yyyy-MM-dd"),
        QStringLiteral("dd.MM.yyyy"),
        QStringLiteral("dd.MM.yy"),
        QStringLiteral("yyyy/MM/dd"),
        QStringLiteral("dd/MM/yyyy")
    };
    for (const QString& fmt : formats) {
        const QDate d = QDate::fromString(raw10, fmt);
        if (d.isValid()) return d.toString(QStringLiteral("yyyy-MM-dd"));
    }
    const QDate iso = DateFormats::parseDate(raw10);
    if (iso.isValid()) return iso.toString(QStringLiteral("yyyy-MM-dd"));
    return QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"));
}

QString sqlQuoteIdent(QString name)
{
    name.replace(QStringLiteral("\""), QStringLiteral("\"\""));
    return QStringLiteral("\"") + name + QStringLiteral("\"");
}

QString sqlQuoteText(QString value)
{
    value.replace(QStringLiteral("'"), QStringLiteral("''"));
    return QStringLiteral("'") + value + QStringLiteral("'");
}

QSet<QString> tableColumnsForDb(const QSqlDatabase& database, const QString& tableName, const QString& schemaName)
{
    QSet<QString> out;
    QSqlQuery q(database);
    QString pragma;
    if (schemaName.trimmed().isEmpty()) {
        pragma = QStringLiteral("PRAGMA table_info(%1)").arg(tableName);
    } else {
        pragma = QStringLiteral("PRAGMA %1.table_info(%2)").arg(schemaName, tableName);
    }
    if (!q.exec(pragma)) return out;
    while (q.next()) out.insert(q.value(1).toString());
    return out;
}

bool tableExistsInDb(const QSqlDatabase& database, const QString& tableName, const QString& schemaName)
{
    QSqlQuery q(database);
    const QString masterName = schemaName.trimmed().isEmpty() ? QStringLiteral("sqlite_master") : (schemaName + QStringLiteral(".sqlite_master"));
    q.prepare(QStringLiteral("SELECT 1 FROM %1 WHERE type='table' AND name=? LIMIT 1").arg(masterName));
    q.addBindValue(tableName);
    return q.exec() && q.next();
}

QString tableCreateSqlForDb(const QSqlDatabase& database, const QString& tableName, const QString& schemaName)
{
    QSqlQuery q(database);
    const QString masterName = schemaName.trimmed().isEmpty() ? QStringLiteral("sqlite_master") : (schemaName + QStringLiteral(".sqlite_master"));
    q.prepare(QStringLiteral("SELECT COALESCE(sql,'') FROM %1 WHERE type='table' AND name=? LIMIT 1").arg(masterName));
    q.addBindValue(tableName);
    return (q.exec() && q.next()) ? q.value(0).toString() : QString();
}

bool tableCreateSqlContains(const QSqlDatabase& database, const QString& tableName, const QString& needle, const QString& schemaName)
{
    return tableCreateSqlForDb(database, tableName, schemaName).contains(needle, Qt::CaseInsensitive);
}

bool ensureColumnForDb(QSqlDatabase database, const QString& tableName, const QString& columnName, const QString& ddl, QString* errorText)
{
    if (!tableExistsInDb(database, tableName)) return true;
    const QSet<QString> cols = tableColumnsForDb(database, tableName);
    if (cols.contains(columnName)) return true;

    QSqlQuery alter(database);
    const QString sql = QStringLiteral("ALTER TABLE %1 ADD COLUMN %2;").arg(sqlQuoteIdent(tableName), ddl);
    if (alter.exec(sql)) return true;

    if (errorText) {
        *errorText = QStringLiteral("ensure %1.%2 failed: %3").arg(tableName, columnName, alter.lastError().text());
    }
    return false;
}

QString firstAvailableExpr(const QSet<QString>& cols, const QStringList& names, const QString& fallbackSql)
{
    for (const QString& name : names) {
        if (cols.contains(name)) return sqlQuoteIdent(name);
    }
    return fallbackSql;
}

QString nonNullTextSql(const QString& expr)
{
    return QStringLiteral("TRIM(COALESCE(%1,''))").arg(expr);
}

QString nonNullIntSql(const QString& expr, const QString& fallback)
{
    return QStringLiteral("COALESCE(%1,%2)").arg(expr, fallback);
}

QString nonNullRealSql(const QString& expr, const QString& fallback)
{
    return QStringLiteral("COALESCE(%1,%2)").arg(expr, fallback);
}

bool dropLegacyCompatibilityViews(QSqlDatabase database)
{
    if (!database.isValid()) return false;
    if (!database.isOpen() && !database.open()) return false;
    QSqlQuery q(database);
    q.exec(QStringLiteral("DROP VIEW IF EXISTS orders"));
    q.exec(QStringLiteral("DROP VIEW IF EXISTS works"));
    q.exec(QStringLiteral("DROP VIEW IF EXISTS parts"));
    return true;
}

bool dropViewsReferencingTable(QSqlDatabase database, const QString& tableName)
{
    if (!database.isValid()) return false;
    if (!database.isOpen() && !database.open()) return false;
    const QString needle = tableName.trimmed().toLower();
    if (needle.isEmpty()) return true;

    QSqlQuery scan(database);
    if (!scan.exec(QStringLiteral("SELECT name, COALESCE(sql,'') FROM sqlite_master WHERE type='view'"))) {
        return false;
    }

    QStringList viewsToDrop;
    while (scan.next()) {
        const QString viewName = scan.value(0).toString().trimmed();
        const QString sql = scan.value(1).toString().toLower();
        if (viewName.isEmpty()) continue;
        if (sql.contains(needle)) viewsToDrop << viewName;
    }

    QSqlQuery drop(database);
    for (const QString& viewName : viewsToDrop) {
        if (!drop.exec(QStringLiteral("DROP VIEW IF EXISTS %1").arg(sqlQuoteIdent(viewName)))) {
            qWarning() << "dropViewsReferencingTable failed for" << viewName << drop.lastError().text();
            return false;
        }
    }
    return true;
}

bool dropFtsTriggersReferencingTable(QSqlDatabase database, const QString& tableName)
{
    if (!database.isValid()) return false;
    if (!database.isOpen() && !database.open()) return false;
    const QString needle = tableName.trimmed().toLower();
    if (needle.isEmpty()) return true;

    QSqlQuery scan(database);
    if (!scan.exec(QStringLiteral("SELECT name, tbl_name, COALESCE(sql,'') FROM sqlite_master WHERE type='trigger'"))) {
        qWarning() << "dropFtsTriggersReferencingTable scan failed" << scan.lastError().text();
        return false;
    }

    QStringList triggersToDrop;
    while (scan.next()) {
        const QString triggerName = scan.value(0).toString().trimmed();
        const QString triggerTable = scan.value(1).toString().trimmed().toLower();
        const QString sql = scan.value(2).toString().toLower();
        if (triggerName.isEmpty()) continue;

        const QString triggerNameLower = triggerName.toLower();
        const bool referencesTable = (triggerTable == needle) || sql.contains(needle);
        const bool isFtsMaintenance = triggerNameLower.contains(QStringLiteral("fts"))
            || sql.contains(QStringLiteral("fts"))
            || triggerNameLower.contains(QStringLiteral("refresh"));
        if (referencesTable && isFtsMaintenance) {
            triggersToDrop << triggerName;
        }
    }

    triggersToDrop.removeDuplicates();
    QSqlQuery drop(database);
    for (const QString& triggerName : triggersToDrop) {
        if (!drop.exec(QStringLiteral("DROP TRIGGER IF EXISTS %1").arg(sqlQuoteIdent(triggerName)))) {
            qWarning() << "dropFtsTriggersReferencingTable failed for" << triggerName << drop.lastError().text();
            return false;
        }
        if (RuntimeLoggingControl::deepDebugEnabled()) qInfo() << "DB_SCHEMA dropped stale FTS trigger" << triggerName << "for" << tableName;
    }
    return true;
}

QString nonNullText(const QString& value)
{
    return value.isNull() ? QStringLiteral("") : value;
}

QString nonNullVariantText(const QVariant& value)
{
    const QString text = value.toString();
    return text.isNull() ? QStringLiteral("") : text;
}

int orderYearFromStoredDateTime(QString value)
{
    const QDate parsed = DateFormats::parseDate(value);
    if (parsed.isValid()) return parsed.year();
    value = value.trimmed();
    if (value.isEmpty()) return QDate::currentDate().year();

    const QStringList formats = {
        QStringLiteral("yyyy-MM-dd"),
        QStringLiteral("yyyy-MM-ddTHH:mm:ss"),
        QStringLiteral("yyyy-MM-dd HH:mm:ss"),
        QStringLiteral("dd.MM.yy"),
        QStringLiteral("dd.MM.yy/HH.mm"),
        QStringLiteral("dd.MM.yyyy"),
        QStringLiteral("dd.MM.yyyy/HH.mm")
    };
    for (const QString& fmt : formats) {
        const QDateTime dt = QDateTime::fromString(value, fmt);
        if (dt.isValid()) return dt.date().year();
        const QDate d = QDate::fromString(value.left(10), fmt);
        if (d.isValid()) return d.year();
    }
    const QDateTime isoDt = DateFormats::parseDateTime(value);
    if (isoDt.isValid()) return isoDt.date().year();
    const QDate isoDate = DateFormats::parseDate(value.left(10));
    if (isoDate.isValid()) return isoDate.year();
    return QDate::currentDate().year();
}

bool archiveSingleActiveOrderToShard(QSqlDatabase database, int orderNumber, QString* errorText)
{
    return OrderArchiveShardSqlSupport::archiveSingleActiveOrderToShard(database, orderNumber, errorText);
}
