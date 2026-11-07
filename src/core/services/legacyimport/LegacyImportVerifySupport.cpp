#include "core/io/NativeSnapshotMetadata.h"
#include "infrastructure/db/sqlite/MigrationOrderKeys.h"
#include "infrastructure/db/sqlite/DbSourceFingerprintSupport.h"
#include <QTemporaryDir>
#include <memory>
#include "infrastructure/db/sqlite/MigrationDestinationBaseline.h"
#include "infrastructure/db/sqlite/SqliteSchemaInspection.h"
#include "core/services/legacyimport/LegacyImportVerifySupport.h"
#include "core/services/legacyimport/LegacyImportAncillaryVerifySupport.h"
#include "core/services/legacyimport/LegacyImportEntityVerifySupport.h"
#include "core/services/legacyimport/LegacyImportPostCheckSupport.h"

#include "runtime/AppRuntime.h"
#include "infrastructure/db/sqlite/Db.h"

#include <QFileInfo>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

namespace {
int scalarCount(QSqlDatabase db, const QString& sql, QString* errorText = nullptr)
{
    QSqlQuery q(db);
    if (!q.exec(sql) || !q.next()) {
        if (errorText) {
            *errorText = q.lastError().text().trimmed().isEmpty()
                ? QStringLiteral("SQL count query failed")
                : q.lastError().text().trimmed();
        }
        return -1;
    }
    if (errorText) errorText->clear();
    return q.value(0).toInt();
}

bool tableExists(QSqlDatabase db, const QString& tableName, const QString& schemaName = QString())
{
    QSqlQuery q(db);
    const QString qualified = schemaName.trimmed().isEmpty()
        ? QStringLiteral("table_info(%1)").arg(tableName)
        : QStringLiteral("%1.table_info(%2)").arg(schemaName, tableName);
    if (!q.exec(QStringLiteral("PRAGMA %1").arg(qualified))) {
        return false;
    }
    return q.next();
}

QSet<QString> tableColumns(QSqlDatabase db, const QString& schemaName, const QString& tableName)
{
    QSet<QString> cols;
    QSqlQuery q(db);
    const QString pragmaName = schemaName.trimmed().isEmpty()
        ? tableName
        : QStringLiteral("%1.table_info(%2)").arg(schemaName, tableName);
    if (!q.exec(QStringLiteral("PRAGMA %1").arg(pragmaName))) {
        return cols;
    }
    while (q.next()) {
        cols.insert(q.value(1).toString().trimmed());
    }
    return cols;
}

LegacyImportVerifyEntityResult exactEntityAgainstSchema(QSqlDatabase db,
                                                        const QString& entityName,
                                                        const QString& sourceTable,
                                                        const QString& targetSchema,
                                                        const QString& targetTable,
                                                        const QString& missingSql = QString(),
                                                        const QString& invalidLinkSql = QString())
{
    LegacyImportVerifyEntityResult result;
    result.entityName = entityName;
    QString sourceError;
    QString targetError;
    QString missingError;
    QString invalidError;
    const bool optionalAbsent = sourceTable == QStringLiteral("outsource_repairs")
        && SqliteSchemaInspection::tableAbsent(db, "verify_src", "outsource_repairs");
    result.sourceCount = optionalAbsent ? 0 : scalarCount(db,
                                     QStringLiteral("SELECT COUNT(*) FROM verify_src.%1").arg(sourceTable),
                                     &sourceError);
    result.targetCount = scalarCount(db,
                                     QStringLiteral("SELECT COUNT(*) FROM %1.%2").arg(targetSchema, targetTable),
                                     &targetError);
    result.missingCount = (optionalAbsent || missingSql.isEmpty()) ? 0 : scalarCount(db, missingSql, &missingError);
    result.invalidLinkCount = invalidLinkSql.isEmpty() ? 0 : scalarCount(db, invalidLinkSql, &invalidError);
    const qint64 retained = targetSchema == "main" ? MigrationDestinationBaseline::before(db,sourceTable) : 0;
    result.ok = result.sourceCount >= 0
        && result.targetCount >= 0
        && retained >= 0 && retained + result.sourceCount == result.targetCount
        && result.missingCount == 0
        && result.invalidLinkCount == 0;
    if (result.sourceCount < 0) {
        result.messages << QStringLiteral("sourceCountQueryFailed=%1").arg(sourceError);
    }
    if (result.targetCount < 0) {
        result.messages << QStringLiteral("targetCountQueryFailed=%1").arg(targetError);
    }
    if (retained < 0 || retained + result.sourceCount != result.targetCount) {
        result.messages << QStringLiteral("sourceCount=%1 targetCount=%2").arg(result.sourceCount).arg(result.targetCount);
    }
    if (result.missingCount < 0) {
        result.messages << QStringLiteral("missingQueryFailed=%1").arg(missingError);
    }
    if (result.missingCount > 0) {
        result.messages << QStringLiteral("missing=%1").arg(result.missingCount);
    }
    if (result.invalidLinkCount < 0) {
        result.messages << QStringLiteral("invalidLinksQueryFailed=%1").arg(invalidError);
    }
    if (result.invalidLinkCount > 0) {
        result.messages << QStringLiteral("invalidLinks=%1").arg(result.invalidLinkCount);
    }
    return result;
}

QList<int> sourceOrderNumbers(QSqlDatabase db)
{
    QList<int> numbers;
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral("SELECT COALESCE(number,0) FROM verify_src.work_orders WHERE COALESCE(number,0)>0 ORDER BY number ASC"))) {
        return numbers;
    }
    while (q.next()) {
        const int orderNumber = q.value(0).toInt();
        if (orderNumber > 0) numbers.push_back(orderNumber);
    }
    return numbers;
}

QList<int> sampleOrderNumbers(const QList<int>& all)
{
    QList<int> sampled;
    if (all.isEmpty()) return sampled;
    auto addUnique = [&](int index) {
        if (index < 0 || index >= all.size()) return;
        const int value = all.at(index);
        if (!sampled.contains(value)) sampled.push_back(value);
    };

    addUnique(0);
    addUnique(all.size() / 4);
    addUnique(all.size() / 2);
    addUnique((all.size() * 3) / 4);
    addUnique(all.size() - 1);

    if (sampled.size() < 8 && all.size() > 1) {
        const int step = qMax(1, all.size() / 8);
        for (int i = 0; i < all.size() && sampled.size() < 8; i += step) {
            addUnique(i);
        }
    }
    return sampled;
}

int sourceServiceCountForOrder(QSqlDatabase db, int orderNumber)
{
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT COUNT(*) FROM verify_src.work_order_services WHERE COALESCE(order_number,0)=?"));
    q.addBindValue(orderNumber);
    if (!q.exec() || !q.next()) return -1;
    return q.value(0).toInt();
}

int sourcePartCountForOrder(QSqlDatabase db, int orderNumber)
{
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT COUNT(*) FROM verify_src.work_order_parts WHERE COALESCE(order_number,0)=?"));
    q.addBindValue(orderNumber);
    if (!q.exec() || !q.next()) return -1;
    return q.value(0).toInt();
}

int sourceOutsourceCountForOrder(QSqlDatabase db, int orderNumber)
{
    if (SqliteSchemaInspection::tableAbsent(db, "verify_src", "outsource_repairs")) return 0;
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT COUNT(*) FROM verify_src.outsource_repairs WHERE COALESCE(order_number,0)=?"));
    q.addBindValue(orderNumber);
    if (!q.exec() || !q.next()) return -1;
    return q.value(0).toInt();
}

LegacyImportVerifyEntityResult verifyRegistryProjection(QSqlDatabase db,
                                                        const QString& schemaName,
                                                        const QString& entityName)
{
    LegacyImportVerifyEntityResult result;
    result.entityName = entityName;
    const QString prefix = schemaName.trimmed().isEmpty() ? QString() : (schemaName + QStringLiteral("."));
    const QString sourceSql = QStringLiteral("SELECT COUNT(*) FROM verify_src.work_orders");
    const QString targetSql = QStringLiteral(
        "SELECT COUNT(DISTINCT t.order_number) FROM %1order_registry t JOIN verify_src.work_orders s ON s.number=t.order_number")
        .arg(prefix);
    const QString missingSql = QStringLiteral(
        "SELECT COUNT(*) FROM verify_src.work_orders s LEFT JOIN %1order_registry t ON t.order_number=s.number WHERE t.order_number IS NULL")
        .arg(prefix);
    const QString archiveSql = QStringLiteral(
        "SELECT COUNT(DISTINCT t.order_number) FROM %1order_registry t JOIN verify_src.work_orders s ON s.number=t.order_number WHERE COALESCE(t.storage_kind,'')='archive'")
        .arg(prefix);
    const QString activeSql = QStringLiteral(
        "SELECT COUNT(DISTINCT t.order_number) FROM %1order_registry t JOIN verify_src.work_orders s ON s.number=t.order_number WHERE COALESCE(t.storage_kind,'')='active'")
        .arg(prefix);
    QString sourceError, targetError, missingError, archiveError, activeError;
    result.sourceCount = scalarCount(db, sourceSql, &sourceError);
    result.targetCount = scalarCount(db, targetSql, &targetError);
    result.missingCount = scalarCount(db, missingSql, &missingError);
    const int archiveCount = scalarCount(db, archiveSql, &archiveError);
    const int activeCount = scalarCount(db, activeSql, &activeError);
    result.invalidLinkCount = 0;
    result.ok = result.sourceCount >= 0 && result.targetCount >= 0 && result.targetCount >= result.sourceCount && result.missingCount == 0;
    if (result.sourceCount < 0) result.messages << QStringLiteral("sourceCountQueryFailed=%1").arg(sourceError);
    if (result.targetCount < 0) result.messages << QStringLiteral("targetCountQueryFailed=%1").arg(targetError);
    if (result.missingCount < 0) result.messages << QStringLiteral("missingQueryFailed=%1").arg(missingError);
    if (archiveCount < 0 && !archiveError.trimmed().isEmpty()) result.messages << QStringLiteral("archiveCountQueryFailed=%1").arg(archiveError);
    if (activeCount < 0 && !activeError.trimmed().isEmpty()) result.messages << QStringLiteral("activeCountQueryFailed=%1").arg(activeError);
    if (result.targetCount < result.sourceCount) result.messages << QStringLiteral("sourceCount=%1 targetCount=%2").arg(result.sourceCount).arg(result.targetCount);
    if (result.missingCount > 0) result.messages << QStringLiteral("missing=%1").arg(result.missingCount);
    if (archiveCount >= 0 || activeCount >= 0) {
        result.messages << QStringLiteral("archiveProjected=%1 activeProjected=%2")
            .arg(archiveCount >= 0 ? QString::number(archiveCount) : QStringLiteral("n/a"),
                 activeCount >= 0 ? QString::number(activeCount) : QStringLiteral("n/a"));
    }
    return result;
}

static bool isUnavailableExactCountResult(const LegacyImportVerifyEntityResult& result)
{
    return result.sourceCount < 0 && result.targetCount < 0 && result.missingCount == 0 && result.invalidLinkCount == 0;
}

static void appendArchiveEntityOrWarning(LegacyImportVerifyReport* report,
                                         LegacyImportVerifyEntityResult result,
                                         const QString& description)
{
    if (!report) return;
    if (isUnavailableExactCountResult(result)) {
        result.ok = true;
        result.messages.clear();
        report->warningMessages << QStringLiteral("Archive exact-count verify skipped for %1: %2")
                                    .arg(result.entityName, description);
    }
    report->entities.push_back(result);
}

bool appendArchiveBackedChecks(QSqlDatabase db,
                               const QString& archivePath,
                               LegacyImportVerifyReport* report,
                               QString* errorText)
{
    if (!report) return true;
    if (archivePath.trimmed().isEmpty() || !QFileInfo::exists(archivePath)) {
        if (errorText) *errorText = QStringLiteral("Archive DB not found after legacy import: %1").arg(archivePath);
        return false;
    }

    QSqlQuery q(db);
    q.exec(QStringLiteral("DETACH DATABASE verify_archive"));
    if (!q.exec(QStringLiteral("ATTACH DATABASE %1 AS verify_archive").arg(sqlQuoteText(archivePath)))) {
        if (errorText) *errorText = QStringLiteral("Не вдалося приєднати archive DB для verify: %1").arg(q.lastError().text());
        return false;
    }
    auto detachArchive = [&]() {
        QSqlQuery dq(db);
        dq.exec(QStringLiteral("DETACH DATABASE verify_archive"));
    };

    if (!tableExists(db, QStringLiteral("work_orders"), QStringLiteral("verify_archive"))) {
        detachArchive();
        if (errorText) *errorText = QStringLiteral("Archive DB не містить таблицю work_orders після імпорту");
        return false;
    }

    report->infoMessages << QStringLiteral("Verify mode: detected archive-backed legacy import. Full count verification is executed against archive DB, registry and router cache; active workspace is validated by hydration samples.");

    const QSet<QString> archiveOrderCols = tableColumns(db, QStringLiteral("verify_archive"), QStringLiteral("work_orders"));
    const QString archiveOrderUidInvalidSql = archiveOrderCols.contains(QStringLiteral("order_uid"))
        ? QStringLiteral("SELECT COUNT(*) FROM verify_archive.work_orders WHERE TRIM(COALESCE(order_uid,''))=''")
        : QString();
    if (archiveOrderUidInvalidSql.isEmpty()) {
        report->infoMessages << QStringLiteral("Archive verify: raw legacy archive does not expose work_orders.order_uid. order_uid completeness is verified in router cache and hydrated active workspace instead of the sealed archive copy.");
    }

    appendArchiveEntityOrWarning(report,
        exactEntityAgainstSchema(
            db,
            QStringLiteral("orders.archive"),
            QStringLiteral("work_orders"),
            QStringLiteral("verify_archive"),
            QStringLiteral("work_orders"),
            QStringLiteral("SELECT COUNT(*) FROM verify_src.work_orders s LEFT JOIN verify_archive.work_orders t ON t.number=s.number WHERE t.number IS NULL"),
            archiveOrderUidInvalidSql
        ),
        QStringLiteral("archive DB is sealed/runtime-backed; router cache and hydration sample checks stay authoritative when direct count queries are unavailable"));

    if (tableExists(db, QStringLiteral("work_order_services"), QStringLiteral("verify_archive"))) {
        appendArchiveEntityOrWarning(report,
            exactEntityAgainstSchema(
                db,
                QStringLiteral("services.archive"),
                QStringLiteral("work_order_services"),
                QStringLiteral("verify_archive"),
                QStringLiteral("work_order_services"),
                QStringLiteral("SELECT COUNT(*) FROM verify_src.work_order_services s LEFT JOIN verify_archive.work_order_services t ON t.id=s.id AND t.order_number=s.order_number WHERE t.id IS NULL"),
                QStringLiteral("SELECT COUNT(*) FROM verify_archive.work_order_services s LEFT JOIN verify_archive.work_orders o ON o.number=s.order_number WHERE o.number IS NULL")
            ),
            QStringLiteral("archive service counts are optional when direct archive COUNT() is unavailable; router cache plus active hydration samples verify effective access"));
    }

    if (tableExists(db, QStringLiteral("work_order_parts"), QStringLiteral("verify_archive"))) {
        const auto sourceColumns = tableColumns(db, "verify_src", "work_order_parts");
        const auto archiveColumns = tableColumns(db, "verify_archive", "work_order_parts");
        const bool hasService = sourceColumns.contains("service_id");
        if (sourceColumns.isEmpty() || archiveColumns.isEmpty() || hasService != archiveColumns.contains("service_id")) {
            detachArchive();
            if (errorText) *errorText = QStringLiteral("Cannot reconcile archive part service-link schema");
            return false;
        }
        QString partLinksSql = QStringLiteral("SELECT COUNT(*) FROM verify_archive.work_order_parts p "
            "LEFT JOIN verify_archive.work_orders o ON o.number=p.order_number ");
        if (hasService)
            partLinksSql += QStringLiteral("LEFT JOIN verify_archive.work_order_services s ON s.id=p.service_id ");
        partLinksSql += QStringLiteral("WHERE o.number IS NULL");
        if (hasService)
            partLinksSql += QStringLiteral(" OR (COALESCE(p.service_id,0)<>0 AND (s.id IS NULL OR s.order_number<>p.order_number))");

        appendArchiveEntityOrWarning(report,
            exactEntityAgainstSchema(
                db,
                QStringLiteral("parts.archive"),
                QStringLiteral("work_order_parts"),
                QStringLiteral("verify_archive"),
                QStringLiteral("work_order_parts"),
                QStringLiteral("SELECT COUNT(*) FROM verify_src.work_order_parts s LEFT JOIN verify_archive.work_order_parts t ON t.id=s.id AND t.order_number=s.order_number WHERE t.id IS NULL"),
                partLinksSql
            ),
            QStringLiteral("archive part counts are optional when direct archive COUNT() is unavailable; router cache plus active hydration samples verify effective access"));
    }

    if (tableExists(db, QStringLiteral("service_masters"), QStringLiteral("verify_archive"))) {
        LegacyImportVerifyEntityResult serviceMasters;
        serviceMasters.entityName = QStringLiteral("service_masters.archive");
        serviceMasters.sourceCount = scalarCount(db, QStringLiteral("SELECT COUNT(*) FROM verify_src.service_masters"));
        serviceMasters.targetCount = scalarCount(db, QStringLiteral("SELECT COUNT(*) FROM verify_archive.service_masters"));
        serviceMasters.missingCount = scalarCount(db, QStringLiteral("SELECT COUNT(*) FROM verify_src.service_masters s LEFT JOIN verify_archive.service_masters t ON t.service_id=s.service_id AND t.master_id=s.master_id WHERE t.service_id IS NULL"));
        serviceMasters.invalidLinkCount = scalarCount(db, QStringLiteral("SELECT COUNT(*) FROM verify_archive.service_masters sm LEFT JOIN verify_archive.work_order_services s ON s.id=sm.service_id WHERE s.id IS NULL"));
        serviceMasters.ok = serviceMasters.sourceCount >= 0
            && serviceMasters.targetCount >= 0
            && serviceMasters.targetCount >= serviceMasters.sourceCount
            && serviceMasters.missingCount == 0
            && serviceMasters.invalidLinkCount == 0;
        if (serviceMasters.targetCount < serviceMasters.sourceCount) {
            serviceMasters.messages << QStringLiteral("sourceCount=%1 targetCount=%2").arg(serviceMasters.sourceCount).arg(serviceMasters.targetCount);
        } else if (serviceMasters.targetCount > serviceMasters.sourceCount) {
            serviceMasters.messages << QStringLiteral("targetHasAutoGeneratedLinks=%1").arg(serviceMasters.targetCount - serviceMasters.sourceCount);
        }
        if (serviceMasters.missingCount > 0) serviceMasters.messages << QStringLiteral("missing=%1").arg(serviceMasters.missingCount);
        if (serviceMasters.invalidLinkCount > 0) serviceMasters.messages << QStringLiteral("invalidLinks=%1").arg(serviceMasters.invalidLinkCount);
        appendArchiveEntityOrWarning(report,
                                     serviceMasters,
                                     QStringLiteral("archive service-master counts are optional when direct archive COUNT() is unavailable; service link integrity is rechecked through router cache and hydrated orders"));
    }

    if (tableExists(db, QStringLiteral("outsource_repairs"), QStringLiteral("verify_archive"))) {
        LegacyImportVerifyEntityResult outsourceArchive = exactEntityAgainstSchema(
            db,
            QStringLiteral("outsource.archive"),
            QStringLiteral("outsource_repairs"),
            QStringLiteral("verify_archive"),
            QStringLiteral("outsource_repairs"),
            QStringLiteral("SELECT COUNT(*) FROM verify_src.outsource_repairs s LEFT JOIN verify_archive.outsource_repairs t ON t.order_number=s.order_number AND COALESCE(t.part_name,'')=COALESCE(s.part_name,'') AND COALESCE(t.sent_at,'')=COALESCE(s.sent_at,'') WHERE t.id IS NULL"),
            QStringLiteral("SELECT COUNT(*) FROM verify_archive.outsource_repairs r LEFT JOIN verify_archive.work_orders o ON o.number=r.order_number WHERE o.number IS NULL")
        );

        const int sealedArchiveOrphanServiceRefs = scalarCount(
            db,
            QStringLiteral("SELECT COUNT(*) FROM verify_archive.outsource_repairs r LEFT JOIN verify_archive.work_order_services s ON s.id=r.service_id WHERE COALESCE(r.service_id,0)<>0 AND (s.id IS NULL OR COALESCE(s.order_number,0)<>COALESCE(r.order_number,0))"));
        if (sealedArchiveOrphanServiceRefs > 0) {
            report->warningMessages << QStringLiteral("Archive verify: outsource_repairs contains %1 legacy service links that do not resolve inside the sealed raw archive. This is preserved as historical raw data and is normalized to NULL in the active workspace/router cache, so it does not block successful migration.")
                                        .arg(sealedArchiveOrphanServiceRefs);
            outsourceArchive.messages << QStringLiteral("sealedLegacyOrphanServiceRefs=%1(normalized_in_active_workspace)").arg(sealedArchiveOrphanServiceRefs);
        }

        appendArchiveEntityOrWarning(report,
                                     outsourceArchive,
                                     QStringLiteral("archive outsource counts are optional when direct archive COUNT() is unavailable; router cache and hydrated order checks remain authoritative"));
    }

    auto addRouterEntity = [&](const QString& entityName,
                               const QString& sourceTable,
                               const QString& routerTable,
                               const QString& missingSql,
                               const QString& invalidSql = QString()) {
        if (!tableExists(db, routerTable)) return;
        report->entities.push_back(exactEntityAgainstSchema(db,
                                                            entityName,
                                                            sourceTable,
                                                            QStringLiteral("main"),
                                                            routerTable,
                                                            missingSql,
                                                            invalidSql));
    };

    addRouterEntity(QStringLiteral("orders.router_cache"),
                    QStringLiteral("work_orders"),
                    QStringLiteral("router_cache_work_orders"),
                    QStringLiteral("SELECT COUNT(*) FROM verify_src.work_orders s LEFT JOIN router_cache_work_orders t ON t.number=s.number WHERE t.number IS NULL"),
                    QStringLiteral("SELECT COUNT(*) FROM router_cache_work_orders WHERE TRIM(COALESCE(order_uid,''))=''")
    );
    addRouterEntity(QStringLiteral("services.router_cache"),
                    QStringLiteral("work_order_services"),
                    QStringLiteral("router_cache_work_order_services"),
                    QStringLiteral("SELECT COUNT(*) FROM verify_src.work_order_services s LEFT JOIN router_cache_work_order_services t ON t.id=s.id AND t.order_number=s.order_number WHERE t.id IS NULL"),
                    QStringLiteral("SELECT COUNT(*) FROM router_cache_work_order_services s LEFT JOIN router_cache_work_orders o ON o.number=s.order_number WHERE o.number IS NULL")
    );
    addRouterEntity(QStringLiteral("parts.router_cache"),
                    QStringLiteral("work_order_parts"),
                    QStringLiteral("router_cache_work_order_parts"),
                    QStringLiteral("SELECT COUNT(*) FROM verify_src.work_order_parts s LEFT JOIN router_cache_work_order_parts t ON t.id=s.id AND t.order_number=s.order_number WHERE t.id IS NULL"),
                    QStringLiteral("SELECT COUNT(*) FROM router_cache_work_order_parts p LEFT JOIN router_cache_work_order_services s ON s.id=p.service_id WHERE COALESCE(p.service_id,0)<>0 AND (s.id IS NULL OR COALESCE(s.order_number,0)<>COALESCE(p.order_number,0))")
    );
    if (tableExists(db, QStringLiteral("router_cache_service_masters"))) {
        LegacyImportVerifyEntityResult serviceMastersRouter;
        serviceMastersRouter.entityName = QStringLiteral("service_masters.router_cache");
        const int sourceTableCount = scalarCount(db, QStringLiteral("SELECT COUNT(*) FROM verify_src.sqlite_master WHERE type='table' AND name='service_masters'"));
        serviceMastersRouter.sourceCount = sourceTableCount == 0 ? 0 : scalarCount(db, QStringLiteral("SELECT COUNT(*) FROM verify_src.service_masters"));
        serviceMastersRouter.targetCount = scalarCount(db, QStringLiteral("SELECT COUNT(*) FROM router_cache_service_masters"));
        serviceMastersRouter.missingCount = sourceTableCount == 0 ? 0 : scalarCount(db, QStringLiteral("SELECT COUNT(*) FROM verify_src.service_masters s LEFT JOIN router_cache_service_masters t ON t.service_id=s.service_id AND t.master_id=s.master_id WHERE t.service_id IS NULL"));
        serviceMastersRouter.invalidLinkCount = scalarCount(db, QStringLiteral("SELECT COUNT(*) FROM router_cache_service_masters sm LEFT JOIN router_cache_work_order_services s ON s.id=sm.service_id WHERE s.id IS NULL"));
        serviceMastersRouter.ok = sourceTableCount >= 0 && serviceMastersRouter.sourceCount >= 0
            && serviceMastersRouter.targetCount >= 0
            && serviceMastersRouter.targetCount >= serviceMastersRouter.sourceCount
            && serviceMastersRouter.missingCount == 0
            && serviceMastersRouter.invalidLinkCount == 0;
        if (serviceMastersRouter.targetCount < serviceMastersRouter.sourceCount) {
            serviceMastersRouter.messages << QStringLiteral("sourceCount=%1 targetCount=%2").arg(serviceMastersRouter.sourceCount).arg(serviceMastersRouter.targetCount);
        } else if (serviceMastersRouter.targetCount > serviceMastersRouter.sourceCount) {
            serviceMastersRouter.messages << QStringLiteral("targetHasAutoGeneratedLinks=%1").arg(serviceMastersRouter.targetCount - serviceMastersRouter.sourceCount);
        }
        if (serviceMastersRouter.missingCount > 0) serviceMastersRouter.messages << QStringLiteral("missing=%1").arg(serviceMastersRouter.missingCount);
        if (serviceMastersRouter.invalidLinkCount > 0) serviceMastersRouter.messages << QStringLiteral("invalidLinks=%1").arg(serviceMastersRouter.invalidLinkCount);
        report->entities.push_back(serviceMastersRouter);
    }
    addRouterEntity(QStringLiteral("outsource.router_cache"),
                    QStringLiteral("outsource_repairs"),
                    QStringLiteral("router_cache_outsource_repairs"),
                    QStringLiteral("SELECT COUNT(*) FROM verify_src.outsource_repairs s LEFT JOIN router_cache_outsource_repairs t ON t.order_number=s.order_number AND COALESCE(t.part_name,'')=COALESCE(s.part_name,'') AND COALESCE(t.sent_at,'')=COALESCE(s.sent_at,'') WHERE t.id IS NULL"),
                    QStringLiteral("SELECT COUNT(*) FROM router_cache_outsource_repairs r LEFT JOIN router_cache_work_orders o ON o.number=r.order_number WHERE o.number IS NULL")
    );

    LegacyImportVerifyEntityResult registryLocal;
    if (tableExists(db, QStringLiteral("order_registry"))) {
        registryLocal = verifyRegistryProjection(db, QString(), QStringLiteral("orders.registry_local"));
    } else {
        registryLocal.entityName = QStringLiteral("orders.registry_local");
        registryLocal.sourceCount = scalarCount(db, QStringLiteral("SELECT COUNT(*) FROM verify_src.work_orders"));
        registryLocal.targetCount = -1;
        registryLocal.missingCount = -1;
        registryLocal.invalidLinkCount = 0;
        registryLocal.ok = false;
        registryLocal.messages << QStringLiteral("missing order_registry table in workspace");
    }
    report->entities.push_back(registryLocal);

    QSqlQuery registryAttach(db);
    registryAttach.exec(QStringLiteral("DETACH DATABASE verify_registry"));
    if (QFileInfo::exists(AppRuntime::registryDbFilePath())
        && registryAttach.exec(QStringLiteral("ATTACH DATABASE %1 AS verify_registry").arg(sqlQuoteText(AppRuntime::registryDbFilePath())))) {
        if (tableExists(db, QStringLiteral("order_registry"), QStringLiteral("verify_registry"))) {
            report->entities.push_back(verifyRegistryProjection(db, QStringLiteral("verify_registry"), QStringLiteral("orders.registry_file")));
        }
        QSqlQuery registryDetach(db);
        registryDetach.exec(QStringLiteral("DETACH DATABASE verify_registry"));
    }

    const QList<int> samples = sampleOrderNumbers(sourceOrderNumbers(db));
    for (int orderNumber : samples) {
        QString hydrateError;
        if (!Database::ensureOrderAvailableInActive(orderNumber, &hydrateError)) {
            LegacyImportVerifyEntityResult sample;
            sample.entityName = QStringLiteral("order_sample_%1.hydration").arg(orderNumber);
            sample.ok = false;
            sample.messages << (hydrateError.trimmed().isEmpty()
                ? QStringLiteral("hydrate failed")
                : hydrateError.trimmed());
            report->entities.push_back(sample);
            continue;
        }

        LegacyImportVerifyEntityResult orderSample;
        orderSample.entityName = QStringLiteral("order_sample_%1.active").arg(orderNumber);
        orderSample.sourceCount = 1;
        orderSample.targetCount = scalarCount(db, QStringLiteral("SELECT COUNT(*) FROM work_orders WHERE number=%1").arg(orderNumber));
        orderSample.missingCount = (orderSample.targetCount == 1) ? 0 : 1;
        orderSample.ok = orderSample.targetCount == 1;
        if (!orderSample.ok) orderSample.messages << QStringLiteral("missing=1");

        const int srcServices = sourceServiceCountForOrder(db, orderNumber);
        const int dstServices = scalarCount(db, QStringLiteral("SELECT COUNT(*) FROM work_order_services WHERE order_number=%1").arg(orderNumber));
        if (srcServices != dstServices) orderSample.messages << QStringLiteral("services=%1/%2").arg(dstServices).arg(srcServices);

        const int srcParts = sourcePartCountForOrder(db, orderNumber);
        const int dstParts = scalarCount(db, QStringLiteral("SELECT COUNT(*) FROM work_order_parts WHERE order_number=%1").arg(orderNumber));
        if (srcParts != dstParts) orderSample.messages << QStringLiteral("parts=%1/%2").arg(dstParts).arg(srcParts);

        const int srcOut = sourceOutsourceCountForOrder(db, orderNumber);
        const int dstOut = scalarCount(db, QStringLiteral("SELECT COUNT(*) FROM outsource_repairs WHERE order_number=%1").arg(orderNumber));
        if (srcOut != dstOut) orderSample.messages << QStringLiteral("outsource=%1/%2").arg(dstOut).arg(srcOut);

        if (srcServices < 0 || srcParts < 0 || srcOut < 0 || srcServices != dstServices || srcParts != dstParts || srcOut != dstOut) {
            orderSample.ok = false;
        }
        report->entities.push_back(orderSample);
    }

    detachArchive();
    return true;
}

LegacyImportVerifyEntityResult verifyEntityCounts(QSqlDatabase db,
                                                  const QString& entityName,
                                                  const QString& sourceTable,
                                                  const QString& targetTable,
                                                  const QString& sourcePresenceJoin,
                                                  const QString& invalidLinkSql = QString())
{
    LegacyImportVerifyEntityResult result;
    result.entityName = entityName;
    result.sourceCount = scalarCount(db, QStringLiteral("SELECT COUNT(*) FROM verify_src.%1").arg(sourceTable));
    result.targetCount = scalarCount(db, QStringLiteral("SELECT COUNT(*) FROM %1").arg(targetTable));
    result.missingCount = sourcePresenceJoin.isEmpty() ? 0 : scalarCount(db, sourcePresenceJoin);
    result.invalidLinkCount = invalidLinkSql.isEmpty() ? 0 : scalarCount(db, invalidLinkSql);
    result.ok = result.sourceCount >= 0
        && result.targetCount >= 0
        && result.sourceCount == result.targetCount
        && result.missingCount == 0
        && result.invalidLinkCount == 0;
    if (result.sourceCount != result.targetCount) {
        result.messages << QStringLiteral("sourceCount=%1 targetCount=%2").arg(result.sourceCount).arg(result.targetCount);
    }
    if (result.missingCount > 0) {
        result.messages << QStringLiteral("missing=%1").arg(result.missingCount);
    }
    if (result.invalidLinkCount > 0) {
        result.messages << QStringLiteral("invalidLinks=%1").arg(result.invalidLinkCount);
    }
    return result;
}

}

bool LegacyImportVerifySupport::runVerifyAgainstCurrentWorkspace(const LegacyImportBundle& bundle,
                                                                 LegacyImportVerifyReport* report,
                                                                 QString* errorText)
{
    LegacyImportVerifyReport localReport;
    localReport.ran = true;
    localReport.sourceDbPath = bundle.primaryDbPath;

    if (bundle.primaryDbPath.trimmed().isEmpty() || !QFileInfo::exists(bundle.primaryDbPath)) {
        localReport.errorMessages << QStringLiteral("Неможливо виконати verify mode без доступної source DB");
        if (report) *report = localReport;
        if (errorText) *errorText = localReport.errorMessages.constFirst();
        return false;
    }

    QSqlDatabase db = Database::db();
    if (!db.isValid() || !db.isOpen()) {
        localReport.errorMessages << QStringLiteral("Активна база застосунку не відкрита для verify mode");
        if (report) *report = localReport;
        if (errorText) *errorText = localReport.errorMessages.constFirst();
        return false;
    }

    QString verifyPath=bundle.primaryDbPath;
    std::unique_ptr<QTemporaryDir> verificationFiles;
    const auto prepareVerification = [&]() -> bool {
        if (SqliteSchemaInspection::tableAbsent(db,"main","migration_prepared_source")) return true;
        QSqlQuery source(db);
        if (!source.exec("SELECT source_identity FROM migration_prepared_source WHERE singleton=1") || !source.next())
            return NativeSnapshotMetadata::fail(errorText,"Cannot read migration source identity for verification");
        const auto identity=source.value(0).toString();
        MigrationOrderKeys::Map keys;
        if (!MigrationOrderKeys::load(db,identity,&keys,errorText)) return false;
        verificationFiles=std::make_unique<QTemporaryDir>();
        if (!verificationFiles->isValid() || !NativeSnapshotMetadata::protectBackupDirectory(verificationFiles->path(),errorText))
            return false;
        const auto audited=verificationFiles->filePath("audited.db");
        if (!copyAuditedDatabase(bundle.primaryDbPath,audited,identity,errorText)) return false;
        verifyPath=audited;
        if (keys.isEmpty()) return true;
        const auto projected=verificationFiles->filePath("verification.db");
        if (!MigrationOrderKeys::project(verifyPath,projected,keys,errorText)) return false;
        verifyPath=projected;
        return true;
    };
    if (!prepareVerification()) {
        localReport.errorMessages << (errorText ? *errorText : QStringLiteral("Cannot verify order key mapping"));
        if (report) *report=localReport;
        return false;
    }

    QSqlQuery q(db);
    q.exec(QStringLiteral("DETACH DATABASE verify_src"));
    if (!q.exec(QStringLiteral("ATTACH DATABASE %1 AS verify_src").arg(sqlQuoteText(verifyPath)))) {
        localReport.errorMessages << QStringLiteral("Не вдалося приєднати source DB для verify: %1").arg(q.lastError().text());
        if (report) *report = localReport;
        if (errorText) *errorText = localReport.errorMessages.constFirst();
        return false;
    }

    auto detach = [&]() {
        QSqlQuery dq(db);
        dq.exec(QStringLiteral("DETACH DATABASE verify_src"));
    };

    QString archiveVerifyError;
    if (!appendArchiveBackedChecks(db, AppRuntime::archiveDbFilePath(), &localReport, &archiveVerifyError)) {
        localReport.errorMessages << archiveVerifyError;
        detach();
        if (report) *report = localReport;
        if (errorText) *errorText = archiveVerifyError;
        return false;
    }

    const int sourceOrderCount = scalarCount(db, QStringLiteral("SELECT COUNT(*) FROM verify_src.work_orders"));
    const int activeOrderCount = scalarCount(db, QStringLiteral("SELECT COUNT(*) FROM work_orders"));
    const bool activeWorkspaceHasFullLegacyCopy = sourceOrderCount > 0 && activeOrderCount == sourceOrderCount;

    if (activeWorkspaceHasFullLegacyCopy) {
        localReport.infoMessages << QStringLiteral("Verify mode: active workspace already contains the full migrated legacy dataset; running full active-schema verification.");

        localReport.entities.push_back(verifyEntityCounts(
            db,
            QStringLiteral("orders"),
            QStringLiteral("work_orders"),
            QStringLiteral("work_orders"),
            QStringLiteral("SELECT COUNT(*) FROM verify_src.work_orders s LEFT JOIN work_orders t ON t.number=s.number WHERE t.number IS NULL"),
            QStringLiteral("SELECT COUNT(*) FROM work_orders WHERE TRIM(COALESCE(order_uid,''))=''")
        ));

        localReport.entities.push_back(verifyEntityCounts(
            db,
            QStringLiteral("services"),
            QStringLiteral("work_order_services"),
            QStringLiteral("work_order_services"),
            QStringLiteral("SELECT COUNT(*) FROM verify_src.work_order_services s LEFT JOIN work_order_services t ON t.id=s.id AND t.order_number=s.order_number WHERE t.id IS NULL"),
            QStringLiteral("SELECT COUNT(*) FROM work_order_services s LEFT JOIN work_orders o ON o.number=s.order_number WHERE o.number IS NULL")
        ));

        const int invalidParentIds = scalarCount(db, QStringLiteral(
            "SELECT COUNT(*) FROM work_order_services s LEFT JOIN work_order_services p ON p.id=s.parent_id WHERE COALESCE(s.parent_id,0)<>0 AND p.id IS NULL"));
        if (invalidParentIds > 0) {
            LegacyImportVerifyEntityResult parentCheck;
            parentCheck.entityName = QStringLiteral("services.parent_id");
            parentCheck.sourceCount = -1;
            parentCheck.targetCount = -1;
            parentCheck.invalidLinkCount = invalidParentIds;
            parentCheck.ok = false;
            parentCheck.messages << QStringLiteral("invalidLinks=%1").arg(invalidParentIds);
            localReport.entities.push_back(parentCheck);
        }

        const int targetWorksSumMismatches = scalarCount(db, QStringLiteral(
            "SELECT COUNT(*) FROM ("
            "SELECT o.number, ABS(COALESCE(o.works_sum,0) - COALESCE(SUM(COALESCE(s.amount, COALESCE(s.qty,1)*COALESCE(s.price,0))),0)) AS delta "
            "FROM work_orders o LEFT JOIN work_order_services s ON s.order_number=o.number "
            "GROUP BY o.number HAVING delta > 0.009)"));
        if (targetWorksSumMismatches > 0) {
            localReport.warningMessages << QStringLiteral("Після імпорту залишилися наряди з розбіжністю works_sum і суми робіт: %1").arg(targetWorksSumMismatches);
        }

        LegacyImportEntityVerifySupport::appendEntityResults(db, &localReport);
        LegacyImportAncillaryVerifySupport::appendEntityResults(db, bundle, &localReport);
        LegacyImportPostCheckSupport::appendPostImportChecks(db, &localReport);
    } else {
        localReport.infoMessages << QStringLiteral("Verify mode: active workspace keeps only hydrated legacy orders on demand. Full exact-count verification for orders/services/parts is intentionally performed against archive DB and router cache instead of active work_orders.");
    }

    detach();

    bool ok = localReport.errorMessages.isEmpty();
    for (const LegacyImportVerifyEntityResult& entity : localReport.entities) {
        if (!entity.ok) {
            ok = false;
            localReport.errorMessages << QStringLiteral("Verify failed for %1: %2")
                .arg(entity.entityName, entity.messages.join(QStringLiteral(", ")));
        }
    }
    localReport.success = ok;
    if (report) *report = localReport;
    if (!ok && errorText) *errorText = localReport.errorMessages.constFirst();
    return ok;
}
