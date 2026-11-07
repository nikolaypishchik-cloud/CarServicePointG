#include "infrastructure/db/sqlite/DbArchiveSourceSchemaSupport.h"

#include "infrastructure/db/sqlite/DbFacadeSupport.h"

#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>

bool prepareArchiveRouterSourceSchema(QSqlDatabase database,
                                      const QString& archivePath,
                                      const QString& legacyPath,
                                      ArchiveRouterSourceSchema* schema,
                                      QString* errorText)
{
    if (!schema) {
        if (errorText) *errorText = QStringLiteral("Archive router source schema output is null");
        return false;
    }

    *schema = ArchiveRouterSourceSchema{};

    const bool archiveExists = QFileInfo::exists(archivePath);
    const bool legacyExists = QFileInfo::exists(legacyPath);
    const bool activeHasCanonicalOrders = tableExistsInDb(database, QStringLiteral("work_orders"));
    const bool activeHasCanonicalServices = tableExistsInDb(database, QStringLiteral("work_order_services"));
    const bool activeHasCanonicalParts = tableExistsInDb(database, QStringLiteral("work_order_parts"));

    schema->useActiveFallbackOnly = !archiveExists && !legacyExists
        && activeHasCanonicalOrders && activeHasCanonicalServices && activeHasCanonicalParts;

    if (schema->useActiveFallbackOnly) return true;

    const QString attachPath = archiveExists ? archivePath : legacyPath;
    if (attachPath.trimmed().isEmpty()) return true;

    QSqlQuery q(database);
    q.exec(QStringLiteral("DETACH DATABASE legacy_router"));
    if (!q.exec(QStringLiteral("ATTACH DATABASE %1 AS legacy_router").arg(sqlQuoteText(attachPath)))) {
        if (errorText) *errorText = q.lastError().text();
        return false;
    }
    schema->legacyAttached = true;

    if (tableExistsInDb(database, QStringLiteral("masters"), QStringLiteral("legacy_router")))
        schema->srcMasters = tableColumnsForDb(database, QStringLiteral("masters"), QStringLiteral("legacy_router"));
    if (tableExistsInDb(database, QStringLiteral("work_orders"), QStringLiteral("legacy_router")))
        schema->srcOrders = tableColumnsForDb(database, QStringLiteral("work_orders"), QStringLiteral("legacy_router"));
    if (tableExistsInDb(database, QStringLiteral("work_order_services"), QStringLiteral("legacy_router")))
        schema->srcServices = tableColumnsForDb(database, QStringLiteral("work_order_services"), QStringLiteral("legacy_router"));
    if (tableExistsInDb(database, QStringLiteral("service_masters"), QStringLiteral("legacy_router")))
        schema->srcServiceMasters = tableColumnsForDb(database, QStringLiteral("service_masters"), QStringLiteral("legacy_router"));
    if (tableExistsInDb(database, QStringLiteral("work_order_parts"), QStringLiteral("legacy_router")))
        schema->srcParts = tableColumnsForDb(database, QStringLiteral("work_order_parts"), QStringLiteral("legacy_router"));
    if (tableExistsInDb(database, QStringLiteral("outsource_repairs"), QStringLiteral("legacy_router")))
        schema->srcOutsource = tableColumnsForDb(database, QStringLiteral("outsource_repairs"), QStringLiteral("legacy_router"));

    return true;
}

void detachArchiveRouterSource(QSqlDatabase database)
{
    QSqlQuery dq(database);
    dq.exec(QStringLiteral("DETACH DATABASE legacy_router"));
}
