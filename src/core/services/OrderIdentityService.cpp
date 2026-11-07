#include "core/services/OrderIdentityService.h"
#include <functional>

#include <QDate>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QScopeGuard>
#include <QTimeZone>
#include <QSet>
#include <limits>
#include <QDebug>

namespace {

bool isUsableDatabase(QSqlDatabase database)
{
    return database.isValid() && (database.isOpen() || database.open());
}


bool objectExists(QSqlDatabase database, const QString& name, const QString& type)
{
    if (!isUsableDatabase(database) || name.trimmed().isEmpty() || type.trimmed().isEmpty()) return false;
    QSqlQuery q(database);
    q.prepare(QStringLiteral("SELECT 1 FROM sqlite_master WHERE type=? AND name=? LIMIT 1"));
    q.addBindValue(type.trimmed());
    q.addBindValue(name.trimmed());
    return q.exec() && q.next();
}

bool columnExists(QSqlDatabase database, const QString& tableName, const QString& columnName)
{
    if (!isUsableDatabase(database) || tableName.trimmed().isEmpty() || columnName.trimmed().isEmpty()) return false;
    QSqlQuery q(database);
    if (!q.exec(QStringLiteral("PRAGMA table_info(%1)").arg(tableName.trimmed()))) return false;
    while (q.next()) {
        if (q.value(1).toString().compare(columnName.trimmed(), Qt::CaseInsensitive) == 0) return true;
    }
    return false;
}

bool execStatement(QSqlDatabase database, const QString& sql, QString* errorText);

bool syncChildOrderUidByOrderNumber(QSqlDatabase database, const QString& tableName, QString* errorText,
                                    const QString& parentTable = QStringLiteral("work_orders"))
{
    if (!objectExists(database, tableName, QStringLiteral("table")) ||
        !objectExists(database, parentTable, QStringLiteral("table"))) return true;
    if (!columnExists(database, tableName, QStringLiteral("order_uid")) ||
        !columnExists(database, tableName, QStringLiteral("order_number"))) {
        return true;
    }

    QSqlQuery conflict(database);
    if (!conflict.exec(QStringLiteral(
            "SELECT c.order_number FROM %1 c JOIN %2 w ON w.number=c.order_number "
            "WHERE TRIM(COALESCE(c.order_uid,''))<>'' AND c.order_uid<>w.order_uid LIMIT 1")
            .arg(tableName.trimmed(), parentTable))) {
        if (errorText) *errorText = conflict.lastError().text();
        return false;
    }
    if (conflict.next()) {
        if (errorText) *errorText = QStringLiteral(
            "Order identity conflict in %1 at internal order %2: child UID differs from parent; migration stopped")
            .arg(tableName).arg(conflict.value(0).toLongLong());
        return false;
    }
    conflict.finish();

    const QString sql = QStringLiteral(
        "UPDATE %1 "
        "SET order_uid=("
        "  SELECT wo.order_uid FROM %2 wo "
        "  WHERE wo.number=%1.order_number"
        ") "
        "WHERE COALESCE(order_number,0)>0 "
        "AND TRIM(COALESCE(order_uid,''))='' "
        "AND EXISTS (SELECT 1 FROM %2 wo WHERE wo.number=%1.order_number)").arg(tableName.trimmed(), parentTable);
    return execStatement(database, sql, errorText);
}

bool setForeignKeys(QSqlDatabase database, bool enabled, QString* errorText)
{
    QSqlQuery q(database);
    const QString sql = enabled ? QStringLiteral("PRAGMA foreign_keys=ON")
                                : QStringLiteral("PRAGMA foreign_keys=OFF");
    if (q.exec(sql)) return true;
    if (errorText) *errorText = q.lastError().text();
    return false;
}

bool foreignKeysEnabled(QSqlDatabase database)
{
    QSqlQuery q(database);
    if (!q.exec(QStringLiteral("PRAGMA foreign_keys")) || !q.next()) return true;
    return q.value(0).toInt() != 0;
}

bool execStatement(QSqlDatabase database, const QString& sql, QString* errorText)
{
    QSqlQuery q(database);
    if (q.exec(sql)) return true;
    if (errorText) *errorText = q.lastError().text();
    return false;
}

bool backfillOrderYears(QSqlDatabase database, QString* errorText)
{
    QSqlQuery rows(database);
    if (!rows.exec(QStringLiteral("SELECT number,opened_at FROM work_orders WHERE COALESCE(order_year,0)=0"))) {
        if (errorText) *errorText = rows.lastError().text();
        return false;
    }
    QList<QPair<qint64, int>> years;
    while (rows.next()) {
        const auto text = rows.value(1).toString().trimmed();
        QDate date = QDate::fromString(text.left(10), QStringLiteral("yyyy-MM-dd"));
        if (!date.isValid()) date = QDate::fromString(text.left(10), QStringLiteral("dd.MM.yyyy"));
        if (!date.isValid() && (text.size() == 8 || (text.size() > 8 && !text.at(8).isDigit()))) {
            const auto candidate = QDate(2000 + text.mid(6,2).toInt(), text.mid(3,2).toInt(), text.left(2).toInt());
            if (candidate.isValid() && candidate.toString(QStringLiteral("dd.MM.yy")) == text.left(8)) date = candidate;
        }
        if (!date.isValid()) {
            if (errorText) *errorText = QStringLiteral("Cannot determine historical year for internal order %1: invalid opening date; migration stopped")
                .arg(rows.value(0).toLongLong());
            return false;
        }
        years.append(qMakePair(rows.value(0).toLongLong(), date.year()));
    }
    if (rows.lastError().isValid()) {
        if (errorText) *errorText = rows.lastError().text();
        return false;
    }
    rows.finish();
    QSqlQuery update(database);
    for (const auto& year : years) {
        update.prepare(QStringLiteral("UPDATE work_orders SET order_year=? WHERE number=? AND COALESCE(order_year,0)=0"));
        update.addBindValue(year.second);
        update.addBindValue(year.first);
        if (!update.exec()) {
            if (errorText) *errorText = update.lastError().text();
            return false;
        }
    }
    return true;
}

bool repairCanonicalIdentityData(QSqlDatabase database, QString* errorText)
{
    if (!objectExists(database, QStringLiteral("work_orders"), QStringLiteral("table"))) return true;

    // Existing identities are historical data, not values to repair by renumbering.
    // Check before any parent/child changes so an ambiguous profile stays intact.
    QSqlQuery conflict(database);
    if (!conflict.exec(QStringLiteral(
            "SELECT number FROM work_orders w WHERE "
            "(TRIM(COALESCE(order_uid,''))<>'' AND EXISTS (SELECT 1 FROM work_orders p "
            "WHERE p.number<>w.number AND TRIM(p.order_uid)=TRIM(w.order_uid))) OR "
            "(COALESCE(order_year,0)>0 AND COALESCE(order_number,0)>0 AND EXISTS "
            "(SELECT 1 FROM work_orders p WHERE p.number<>w.number "
            "AND p.order_year=w.order_year AND p.order_number=w.order_number)) LIMIT 1"))) {
        if (errorText) *errorText = conflict.lastError().text();
        return false;
    }
    if (conflict.next()) {
        if (errorText) *errorText = QStringLiteral(
            "Order identity conflict at internal order %1: duplicate UID or year/number; migration stopped")
            .arg(conflict.value(0).toInt());
        return false;
    }
    conflict.finish();

    if (!execStatement(database, QStringLiteral(
            "UPDATE work_orders "
            "SET order_uid=TRIM(COALESCE(order_uid,'')) "
            "WHERE COALESCE(order_uid,'')<>TRIM(COALESCE(order_uid,''))"), errorText)) return false;

    if (!execStatement(database, QStringLiteral(
            "UPDATE work_orders "
            "SET order_uid='wo_' || COALESCE(NULLIF(CAST(order_year AS TEXT),'0'), strftime('%Y','now')) || '_' || CAST(number AS TEXT) || '_' || lower(hex(randomblob(8))) "
            "WHERE TRIM(COALESCE(order_uid,''))=''"), errorText)) return false;

    if (!backfillOrderYears(database, errorText)) return false;

    if (!execStatement(database, QStringLiteral(
            "UPDATE work_orders "
            "SET order_number=COALESCE(NULLIF(order_number,0), number) "
            "WHERE COALESCE(order_number,0)=0"), errorText)) return false;

    if (objectExists(database, QStringLiteral("router_cache_work_orders"), QStringLiteral("table"))) {
        QSqlQuery cacheConflict(database);
        if (!cacheConflict.exec(QStringLiteral(
                "SELECT c.number FROM router_cache_work_orders c JOIN work_orders w ON w.number=c.number "
                "WHERE TRIM(COALESCE(c.order_uid,''))<>'' AND c.order_uid<>w.order_uid LIMIT 1"))) {
            if (errorText) *errorText = cacheConflict.lastError().text();
            return false;
        }
        if (cacheConflict.next()) {
            if (errorText) *errorText = QStringLiteral(
                "Order identity conflict in router_cache_work_orders at internal order %1: cached UID differs from parent; migration stopped")
                .arg(cacheConflict.value(0).toLongLong());
            return false;
        }
        cacheConflict.finish();
        if (!execStatement(database, QStringLiteral(
                "UPDATE router_cache_work_orders "
                "SET order_uid=(SELECT COALESCE(w.order_uid,'') FROM work_orders w WHERE w.number=router_cache_work_orders.number) "
                "WHERE EXISTS (SELECT 1 FROM work_orders w WHERE w.number=router_cache_work_orders.number) "
                "AND TRIM(COALESCE(order_uid,''))=''"), errorText)) return false;

        if (!execStatement(database, QStringLiteral(
                "UPDATE router_cache_work_orders "
                "SET order_uid='rcwo_' || CAST(number AS TEXT) || '_' || lower(hex(randomblob(8))) "
                "WHERE TRIM(COALESCE(order_uid,''))=''"), errorText)) return false;
    }

    // Fill missing child links using the internal order key, not the annual public number,
    // before foreign keys are re-enabled. This is schema repair only; rows without a matching work order are
    // left untouched to avoid deleting user data during startup.
    const QStringList activeChildren = {
        QStringLiteral("work_order_services"),
        QStringLiteral("work_order_parts"),
        QStringLiteral("outsource_repairs"),
        QStringLiteral("outsource_jobs")
    };
    for (const QString& tableName : activeChildren) {
        if (!syncChildOrderUidByOrderNumber(database, tableName, errorText)) return false;
    }

    const QStringList routerChildren = {
        QStringLiteral("router_cache_work_order_services"),
        QStringLiteral("router_cache_work_order_parts"),
        QStringLiteral("router_cache_outsource_repairs"),
        QStringLiteral("router_cache_outsource_jobs")
    };
    for (const QString& tableName : routerChildren) {
        if (!syncChildOrderUidByOrderNumber(database, tableName, errorText)) return false;
        // Archived orders may exist only in the router cache. Their children
        // still require the same conflict check and missing-link backfill.
        if (!syncChildOrderUidByOrderNumber(database, tableName, errorText,
                QStringLiteral("router_cache_work_orders"))) return false;
    }

    return true;
}

int maxVisibleNumberInWorkOrders(QSqlDatabase database, int year)
{
    if (!isUsableDatabase(database)) return 0;
    QSqlQuery q(database);
    q.prepare(QStringLiteral(
        "SELECT COALESCE(MAX(COALESCE(order_number, number)),0) "
        "FROM work_orders "
        "WHERE COALESCE(NULLIF(order_year,0), CAST(strftime('%Y', (CASE WHEN substr(trim(COALESCE(opened_at,'')),3,1)='.' AND substr(trim(COALESCE(opened_at,'')),6,1)='.' THEN CASE WHEN substr(trim(COALESCE(opened_at,'')),7,4) GLOB '[0-9][0-9][0-9][0-9]' THEN substr(trim(COALESCE(opened_at,'')),7,4) ELSE '20'||substr(trim(COALESCE(opened_at,'')),7,2) END||'-'||substr(trim(COALESCE(opened_at,'')),4,2)||'-'||substr(trim(COALESCE(opened_at,'')),1,2) ELSE COALESCE(opened_at,'') END)) AS INTEGER), ?) = ?"));
    q.addBindValue(year);
    q.addBindValue(year);
    if (q.exec() && q.next()) return q.value(0).toInt();
    return 0;
}

int maxVisibleNumberInRegistry(QSqlDatabase database, int year)
{
    if (!isUsableDatabase(database)) return 0;
    QSqlQuery q(database);
    q.prepare(QStringLiteral(
        "SELECT COALESCE(MAX(COALESCE(NULLIF(visible_order_number,0), 0)),0) "
        "FROM order_registry WHERE COALESCE(order_year,0)=?"));
    q.addBindValue(year);
    if (q.exec() && q.next()) return q.value(0).toInt();
    return 0;
}

} // namespace

namespace OrderIdentityService {

int orderYearForDate(const QDate& date)
{
    return (date.isValid() ? date : QDate::currentDate()).year();
}

QString createOrderUid()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

int nextVisibleOrderNumber(QSqlDatabase activeDb,
                           const std::function<QSqlDatabase()>& registryResolver,
                           const QDate& openedDate)
{
    const int year = orderYearForDate(openedDate);
    int maxVisibleNumber = 0;
    maxVisibleNumber = qMax(maxVisibleNumber, maxVisibleNumberInWorkOrders(activeDb, year));
    maxVisibleNumber = qMax(maxVisibleNumber, maxVisibleNumberInRegistry(activeDb, year));
    if (registryResolver) {
        maxVisibleNumber = qMax(maxVisibleNumber, maxVisibleNumberInRegistry(registryResolver(), year));
    }
    if (objectExists(activeDb, QStringLiteral("annual_order_counters"), QStringLiteral("table"))) {
        QSqlQuery counter(activeDb);
        counter.prepare(QStringLiteral("SELECT last_number FROM annual_order_counters WHERE order_year=?")); counter.addBindValue(year);
        if (!counter.exec()) {qWarning() << "Annual preview counter read failed" << counter.lastError().text();return 0;}
        if (counter.next()) maxVisibleNumber=qMax(maxVisibleNumber,counter.value(0).toInt());
        if (counter.lastError().isValid()) {qWarning() << "Annual preview counter fetch failed" << counter.lastError().text();return 0;}
    }
    if (maxVisibleNumber==std::numeric_limits<int>::max()) return 0;
    return qMax(1, maxVisibleNumber + 1);
}

bool allocateAnnualIdentity(QSqlDatabase database, const QString& requestUid,
    const QDateTime& createdAt, int preferredInternalNumber, Allocation* allocation,
    QString* errorText, const std::function<QSqlDatabase()>& registryResolver)
{
    const auto fail = [&](const QString& message) {
        if (errorText) *errorText = QStringLiteral("Annual order allocation: ") + message;
        return false;
    };
    const QTimeZone zone(QByteArrayLiteral("Europe/Kyiv"));
    if (!database.isOpen() || !allocation || requestUid.trimmed().isEmpty()
        || requestUid.size() > 256 || !createdAt.isValid() || !zone.isValid())
        return fail(QStringLiteral("open database, stable request ID and valid Kyiv creation time required"));
    const int year = createdAt.toTimeZone(zone).date().year();
    auto run = [&](const QString& sql, const QVariantList& values = QVariantList()) {
        QSqlQuery q(database);
        if (!q.prepare(sql)) return fail(q.lastError().text());
        for (const auto& value : values) q.addBindValue(value);
        return q.exec() || fail(q.lastError().text());
    };
    if (!run(QStringLiteral("SAVEPOINT annual_identity_allocation"))) return false;
    auto rollback = qScopeGuard([&] {
        for (const auto& sql : {QStringLiteral("ROLLBACK TO annual_identity_allocation"), QStringLiteral("RELEASE annual_identity_allocation")}) {
            QSqlQuery q(database);
            if (!q.exec(sql)) {
                const auto detail = QStringLiteral("Annual allocation rollback failed: ") + q.lastError().text();
                if (errorText) *errorText += QStringLiteral("; ") + detail;
                qWarning().noquote() << detail;
            }
        }
    });
    if (!run(QStringLiteral("CREATE TABLE IF NOT EXISTS annual_order_counters ("
            "order_year INTEGER PRIMARY KEY CHECK(order_year BETWEEN 1 AND 9999),"
            "last_number INTEGER NOT NULL CHECK(last_number BETWEEN 0 AND 2147483647))"))
        || !run(QStringLiteral("CREATE TABLE IF NOT EXISTS order_creation_reservations ("
            "request_uid TEXT PRIMARY KEY,order_uid TEXT NOT NULL UNIQUE,"
            "internal_number INTEGER NOT NULL UNIQUE CHECK(internal_number>0),"
            "order_year INTEGER NOT NULL CHECK(order_year BETWEEN 1 AND 9999),"
            "order_number INTEGER NOT NULL CHECK(order_number>0),created_at TEXT NOT NULL,"
            "UNIQUE(order_year,order_number))"))) return false;
    // A write statement acquires SQLite's writer reservation before any maxima
    // or replay lookup, including when this year has no counter row yet.
    if (!run(QStringLiteral("UPDATE annual_order_counters SET last_number=last_number WHERE order_year=?"), {year})) return false;
    Allocation result;
    QSqlQuery replay(database);
    if (!replay.prepare(QStringLiteral("SELECT order_uid,internal_number,order_number,order_year FROM order_creation_reservations WHERE request_uid=?")))
        return fail(replay.lastError().text());
    replay.addBindValue(requestUid);
    if (!replay.exec()) return fail(replay.lastError().text());
    if (replay.next()) {
        result.orderUid = replay.value(0).toString(); result.internalNumber = replay.value(1).toInt();
        result.visibleNumber = replay.value(2).toInt(); result.year = replay.value(3).toInt(); result.replayed = true;
        replay.finish();
        if (result.orderUid.isEmpty() || result.internalNumber <= 0 || result.visibleNumber <= 0 || result.year <= 0)
            return fail(QStringLiteral("stored request identity is invalid"));
        if (!run(QStringLiteral("RELEASE annual_identity_allocation"))) return false;
        rollback.dismiss(); *allocation = result; return true;
    }
    if (replay.lastError().isValid()) return fail(replay.lastError().text());
    replay.finish();
    qint64 maxInternal = 0, maxVisible = 0;
    auto maxima = [&](QSqlDatabase source, const QString& table, const QString& internal, const QString& visible, bool required) {
        if (!source.isOpen()) return fail(QStringLiteral("identity source is not open"));
        QSqlQuery exists(source);
        if (!exists.prepare(QStringLiteral("SELECT 1 FROM sqlite_master WHERE name=? AND type IN ('table','view')"))) return fail(exists.lastError().text());
        exists.addBindValue(table);
        if (!exists.exec()) return fail(exists.lastError().text());
        if (!exists.next()) {
            if (exists.lastError().isValid()) return fail(exists.lastError().text());
            return !required || fail(QStringLiteral("missing identity source: ") + table);
        }
        exists.finish();
        QSqlQuery q(source);
        if (!q.exec(QStringLiteral("SELECT %1 FROM %2 WHERE COALESCE(%1,0)<=0 OR COALESCE(order_year,0)<=0 "
                                  "OR COALESCE(%3,0)<=0 OR TRIM(COALESCE(order_uid,''))='' LIMIT 1").arg(internal, table, visible)))
            return fail(q.lastError().text());
        if (q.next()) return fail(QStringLiteral("unresolved historical identity in ") + table);
        if (q.lastError().isValid()) return fail(q.lastError().text());
        q.finish();
        if (!q.prepare(QStringLiteral("SELECT COALESCE(MAX(%1),0),COALESCE(MAX(CASE WHEN order_year=? THEN %2 ELSE 0 END),0) FROM %3")
                       .arg(internal, visible, table))) return fail(q.lastError().text());
        q.addBindValue(year);
        if (!q.exec() || !q.next()) return fail(q.lastError().text());
        maxInternal = qMax(maxInternal, q.value(0).toLongLong()); maxVisible = qMax(maxVisible, q.value(1).toLongLong());
        return true;
    };
    if (!maxima(database, QStringLiteral("work_orders"), QStringLiteral("number"), QStringLiteral("order_number"), true)
        || !maxima(database, QStringLiteral("router_cache_work_orders"), QStringLiteral("number"), QStringLiteral("order_number"), false)
        || !maxima(database, QStringLiteral("order_registry"), QStringLiteral("order_number"), QStringLiteral("visible_order_number"), false)
        || !maxima(database, QStringLiteral("order_creation_reservations"), QStringLiteral("internal_number"), QStringLiteral("order_number"), true)) return false;
    if (registryResolver && !maxima(registryResolver(), QStringLiteral("order_registry"), QStringLiteral("order_number"), QStringLiteral("visible_order_number"), true)) return false;
    if (maxInternal >= std::numeric_limits<int>::max() || maxVisible >= std::numeric_limits<int>::max())
        return fail(QStringLiteral("order number range exhausted"));
    if (!run(QStringLiteral("INSERT INTO annual_order_counters(order_year,last_number) VALUES(?,?) "
                            "ON CONFLICT(order_year) DO UPDATE SET last_number=MAX(last_number,excluded.last_number)"), {year, maxVisible})) return false;
    QSqlQuery advance(database);
    if (!advance.prepare(QStringLiteral("UPDATE annual_order_counters SET last_number=last_number+1 WHERE order_year=? AND last_number<2147483647")))
        return fail(advance.lastError().text());
    advance.addBindValue(year);
    if (!advance.exec()) return fail(advance.lastError().text());
    if (advance.numRowsAffected() != 1) return fail(QStringLiteral("annual number range exhausted"));
    QSqlQuery read(database);
    if (!read.prepare(QStringLiteral("SELECT last_number FROM annual_order_counters WHERE order_year=?"))) return fail(read.lastError().text());
    read.addBindValue(year);
    if (!read.exec() || !read.next()) return fail(read.lastError().text());
    result.visibleNumber = read.value(0).toInt(); result.year = year;
    result.internalNumber = int(qMax(qint64(preferredInternalNumber), maxInternal + 1));
    result.orderUid = createOrderUid(); read.finish();
    if (!run(QStringLiteral("INSERT INTO order_creation_reservations(request_uid,order_uid,internal_number,order_year,order_number,created_at) VALUES(?,?,?,?,?,?)"),
             {requestUid, result.orderUid, result.internalNumber, result.year, result.visibleNumber, createdAt.toUTC().toString(Qt::ISODateWithMs)})) return false;
    if (!run(QStringLiteral("RELEASE annual_identity_allocation"))) return false;
    rollback.dismiss(); *allocation = result; return true;
}

bool ensureCanonicalIdentityIndexes(QSqlDatabase database, QString* errorText)
{
    if (!isUsableDatabase(database)) {
        if (errorText) *errorText = database.lastError().text();
        return false;
    }

    // Startup schema migration must be able to fill missing parent keys before
    // SQLite validates child-table foreign keys that reference work_orders(order_uid).
    // Older databases can have outsource_repairs/work_order_services created with an
    // FK to order_uid while the unique parent index was still missing or blocked by
    // duplicate data. Updating work_orders with foreign_keys=ON in that state raises
    // "foreign key mismatch ... referencing work_orders" and aborts startup.
    const bool restoreForeignKeys = foreignKeysEnabled(database);
    if (!setForeignKeys(database, false, errorText)) return false;

    // A savepoint also works when the caller already owns a transaction.
    // Repair and all indexes must either survive together or leave no changes.
    if (!execStatement(database, QStringLiteral("SAVEPOINT canonical_identity_repair"), errorText)) {
        QString ignored;
        if (restoreForeignKeys) setForeignKeys(database, true, &ignored);
        return false;
    }
    auto restoreAndFail = [&](const QString& text) -> bool {
        QString ignored;
        execStatement(database, QStringLiteral("ROLLBACK TO canonical_identity_repair"), &ignored);
        execStatement(database, QStringLiteral("RELEASE canonical_identity_repair"), &ignored);
        if (restoreForeignKeys) setForeignKeys(database, true, &ignored);
        if (errorText && errorText->trimmed().isEmpty()) *errorText = text;
        return false;
    };

    if (!repairCanonicalIdentityData(database, errorText)) {
        const QString err = errorText ? *errorText : QString();
        return restoreAndFail(err);
    }

    QSqlQuery q(database);
    QStringList ddl = {
        QStringLiteral("CREATE UNIQUE INDEX IF NOT EXISTS idx_work_orders_uid_unique ON work_orders(order_uid)"),
        QStringLiteral("CREATE UNIQUE INDEX IF NOT EXISTS idx_work_orders_order_year_number_unique ON work_orders(order_year, order_number)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_work_orders_visible_lookup ON work_orders(order_number, order_year DESC, number DESC)")
    };
    if (objectExists(database, QStringLiteral("router_cache_work_orders"), QStringLiteral("table"))) {
        ddl << QStringLiteral("CREATE UNIQUE INDEX IF NOT EXISTS idx_router_cache_orders_uid_unique ON router_cache_work_orders(order_uid)")
            << QStringLiteral("CREATE UNIQUE INDEX IF NOT EXISTS idx_router_cache_orders_year_number_unique ON router_cache_work_orders(order_year, order_number)")
            << QStringLiteral("CREATE INDEX IF NOT EXISTS idx_router_cache_orders_visible_lookup ON router_cache_work_orders(order_number, order_year DESC, number DESC)");
    }
    for (const QString& sql : ddl) {
        if (!q.exec(sql)) {
            const QString err = q.lastError().text();
            if (errorText) *errorText = err;
            return restoreAndFail(err);
        }
    }

    if (!execStatement(database, QStringLiteral("RELEASE canonical_identity_repair"), errorText)) {
        return restoreAndFail(errorText ? *errorText : QString());
    }
    if (restoreForeignKeys && !setForeignKeys(database, true, errorText)) return false;
    return true;
}

} // namespace OrderIdentityService