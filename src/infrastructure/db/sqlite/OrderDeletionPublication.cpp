#include "infrastructure/db/sqlite/OrderDeletionPublication.h"
#include "infrastructure/db/sqlite/DbContext.h"
#include "infrastructure/db/sqlite/ArchiveTransferJournal.h"
#include "infrastructure/db/sqlite/DbArchiveInternals.h"
#include "infrastructure/db/sqlite/DbFacadeSupport.h"
#include "infrastructure/db/sqlite/ScopedConnectionManager.h"
#include "runtime/AppRuntime.h"
#include <QFileInfo>
#include <QScopeGuard>
#include <QSqlQuery>
#include <QSqlError>

namespace
{
bool fail(QString *error, const QString &reason)
{
    if (error)
        *error = reason;
    return false;
}

bool execute(QSqlQuery &query, const QString &sql, const QVariantList &values, QString *error)
{
    query.finish();
    if (!query.prepare(sql))
        return fail(error, query.lastError().text());
    for (const auto &value : values)
        query.addBindValue(value);
    return query.exec() || fail(error, query.lastError().text());
}

bool requireEmpty(QSqlQuery &query, const QString &sql, const QVariantList &values, QString *error)
{
    if (!execute(query, sql, values, error))
        return false;
    if (query.next())
        return fail(error, "Order deletion conflicts with recorded identity or a surviving row");
    return !query.lastError().isValid() || fail(error, query.lastError().text());
}

bool journalPresent(QSqlDatabase source, bool *present, QString *error)
{
    QSqlQuery q(source);
    if (!q.exec("SELECT 1 FROM sqlite_master WHERE type='table' AND name='order_deletion_publications'"))
        return fail(error, q.lastError().text());
    *present = q.next();
    return !q.lastError().isValid() || fail(error, q.lastError().text());
}
} // namespace

bool OrderDeletionPublication::validatePartLocators(int number, const QString &uid, QString *error)
{
    if (!ensurePartRegistrySchemaFile())
        return fail(error, "Cannot prepare deletion registry");
    QSqlQuery q(QSqlDatabase::database(dbRegistryConnectionName(), false));
    return requireEmpty(q,
                        "SELECT 1 FROM part_registry WHERE (order_number=? OR order_uid=?) "
                        "AND (order_number IS NOT ? OR order_uid IS NOT ?)",
                        {number, uid, number, uid}, error);
}

bool OrderDeletionPublication::record(QSqlDatabase source, int number, QString *error)
{
    if (!TransactionContext::hasActiveTransaction(source))
        return fail(error, "Deletion publication requires the owned purge transaction");
    QSqlQuery q(source);
    if (!q.exec("CREATE TABLE IF NOT EXISTS order_deletion_publications(internal_number INTEGER PRIMARY KEY,"
                "order_uid TEXT NOT NULL UNIQUE,pending INTEGER NOT NULL CHECK(pending IN (0,1)))"))
        return fail(error, q.lastError().text());
    if (!execute(q,
                 "INSERT INTO order_deletion_publications SELECT r.internal_number,r.order_uid,1 "
                 "FROM order_creation_reservations r JOIN work_orders w ON w.number=r.internal_number AND "
                 "w.order_uid=r.order_uid "
                 "AND w.order_year=r.order_year AND w.order_number=r.order_number WHERE w.number=?",
                 {number}, error))
        return false;
    if (q.numRowsAffected() != 1)
        return fail(error, "Deletion has no matching reservation");
    if (!execute(q, "SELECT order_uid FROM order_deletion_publications WHERE internal_number=?", {number}, error) ||
        !q.next())
        return fail(error, "Cannot read deletion identity");
    const auto uid = q.value(0).toString();
    q.finish();
    for (const auto &table : {QStringLiteral("order_registry"), QStringLiteral("workspace_order_state")})
    {
        if (!execute(q, "DELETE FROM " + table + " WHERE order_number=? AND order_uid=?", {number, uid}, error) ||
            !requireEmpty(q, "SELECT 1 FROM " + table + " WHERE order_number=? OR order_uid=?", {number, uid}, error))
            return false;
    }
    return true;
}

bool OrderDeletionPublication::alreadyDeleted(QSqlDatabase source, int number, QString *error)
{
    bool present = false;
    if (!journalPresent(source, &present, error))
        return false;
    QSqlQuery q(source);
    if (present && !execute(q,
                            "SELECT 1 FROM order_deletion_publications d JOIN order_creation_reservations r "
                            "ON r.internal_number=d.internal_number AND r.order_uid=d.order_uid WHERE "
                            "d.internal_number=? AND d.pending=0",
                            {number}, error))
        return false;
    if (present && q.next())
        return true;
    return fail(error, "No confirmed deletion exists for this order");
}

bool OrderDeletionPublication::replay(QSqlDatabase source, QString *error)
{
    bool present = false;
    if (!journalPresent(source, &present, error))
        return false;
    if (!present)
        return true;
    QSqlQuery pending(source);
    if (!pending.exec(
            "SELECT internal_number FROM order_deletion_publications WHERE pending=1 ORDER BY internal_number"))
        return fail(error, pending.lastError().text());
    QList<int> numbers;
    while (pending.next())
        numbers.append(pending.value(0).toInt());
    if (pending.lastError().isValid())
        return fail(error, pending.lastError().text());
    pending.finish();
    if (numbers.isEmpty())
        return true;
    ArchiveTransferJournal::crashPoint("deletion_source_committed");
    const auto path = AppRuntime::registryDbFilePath();
    if (!QFileInfo(path).isFile())
        return fail(error, "Recorded deletion registry is unavailable");
    ScopedSqliteConnection external(path, "order_deletion_publication", error);
    if (!external.isOpen())
        return false;
    auto registry = external.db();
    for (int number : numbers)
    {
        if (!source.transaction())
            return fail(error, source.lastError().text());
        auto undoSource = qScopeGuard([&] { source.rollback(); });
        QSqlQuery q(source);
        if (!execute(q, "UPDATE order_deletion_publications SET pending=1 WHERE internal_number=? AND pending=1",
                     {number}, error))
            return false;
        if (q.numRowsAffected() == 0)
            continue; // Another publisher already acknowledged this entry.
        if (!execute(
                q,
                "SELECT d.order_uid,r.order_year,r.order_number FROM order_deletion_publications d "
                "JOIN order_creation_reservations r ON d.internal_number=r.internal_number AND d.order_uid=r.order_uid "
                "WHERE d.internal_number=?",
                {number}, error) ||
            !q.next())
            return fail(error, "Deletion reservation differs from its journal");
        auto uid = q.value(0).toString();
        const int year = q.value(1).toInt(), visible = q.value(2).toInt();
        q.finish();
        if (!requireEmpty(
                q, "SELECT 1 FROM work_orders WHERE number=? OR order_uid=? OR (order_year=? AND order_number=?)",
                {number, uid, year, visible}, error))
            return false;
        for (const auto &table : {QStringLiteral("order_registry"), QStringLiteral("workspace_order_state")})
            if (!requireEmpty(q, "SELECT 1 FROM " + table + " WHERE order_number=? OR order_uid=?", {number, uid},
                              error))
                return false;
        if (!registry.transaction())
            return fail(error, registry.lastError().text());
        auto undoRegistry = qScopeGuard([&] { registry.rollback(); });
        QSqlQuery projection(registry);
        if (!execute(projection,
                     "DELETE FROM order_registry WHERE order_number=? AND order_uid=? AND order_year=? AND "
                     "visible_order_number=?",
                     {number, uid, year, visible}, error) ||
            !requireEmpty(projection,
                          "SELECT 1 FROM order_registry WHERE order_number=? OR order_uid=? OR (order_year=? AND "
                          "visible_order_number=?)",
                          {number, uid, year, visible}, error) ||
            !execute(projection, "DELETE FROM part_registry WHERE order_number=? AND order_uid=?", {number, uid},
                     error) ||
            !requireEmpty(projection, "SELECT 1 FROM part_registry WHERE order_number=? OR order_uid=?", {number, uid},
                          error))
            return false;
        if (!registry.commit())
            return fail(error, registry.lastError().text());
        undoRegistry.dismiss();
        ArchiveTransferJournal::crashPoint("deletion_registry_committed");
        if (!execute(q,
                     "UPDATE order_deletion_publications SET pending=0 WHERE internal_number=? AND order_uid=? AND "
                     "pending=1",
                     {number, uid}, error))
            return false;
        if (q.numRowsAffected() != 1)
            return fail(error, "Deletion publication acknowledgement failed");
        if (!execute(q, "SELECT pending FROM order_deletion_publications WHERE internal_number=? AND order_uid=?",
                     {number, uid}, error))
            return false;
        if (!q.next() || q.value(0).toInt() != 0)
            return fail(error, "Deletion publication acknowledgement was not retained");
        q.finish();
        if (!source.commit())
            return fail(error, source.lastError().text());
        undoSource.dismiss();
    }
    return true;
}
