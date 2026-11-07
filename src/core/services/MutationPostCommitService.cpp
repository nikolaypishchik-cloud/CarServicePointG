#include "core/services/MutationPostCommitService.h"

#include "core/services/LegacyMutationLogSupport.h"
#include "core/services/ActivityAuditService.h"
#include "core/crm/CrmClientService.h"
#include "core/services/PartsQueryService.h"
#include "core/cache/SnapshotCacheInvalidator.h"
#include "core/cloudsync/CloudSyncOutboxService.h"
#include "core/diagnostics/RuntimeLoggingControl.h"

#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace {

bool touchesPartsCache(const QStringList& tables)
{
    for (const QString& table : tables) {
        const QString normalized = table.trimmed().toLower();
        if (normalized == QStringLiteral("work_order_parts")
            || normalized == QStringLiteral("router_cache_work_order_parts")
            || normalized == QStringLiteral("router_work_order_parts")
            || normalized == QStringLiteral("work_order_part_attachments")
            || normalized == QStringLiteral("work_orders")
            || normalized == QStringLiteral("router_work_orders")
            || normalized == QStringLiteral("router_cache_work_orders")) {
            return true;
        }
    }
    return false;
}

bool touchesTable(const QStringList& tables, const QString& target)
{
    for (const QString& table : tables) {
        if (table.trimmed().compare(target, Qt::CaseInsensitive) == 0) return true;
    }
    return false;
}

QString postCommitPayload(const MutationResult& result)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("operation_id"), result.operationId);
    obj.insert(QStringLiteral("source_module"), result.sourceModule);
    obj.insert(QStringLiteral("entity_type"), result.entityType);
    obj.insert(QStringLiteral("entity_id"), result.entityId);
    obj.insert(QStringLiteral("operation"), result.operation);
    obj.insert(QStringLiteral("description"), result.description);
    obj.insert(QStringLiteral("committed"), result.ok);

    QJsonArray changedTables;
    for (const QString& table : result.changedTables) {
        changedTables.append(table);
    }
    obj.insert(QStringLiteral("changed_tables"), changedTables);
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

void appendPostCommitMutationLog(QSqlDatabase database, const MutationResult& result)
{
    QString logError;
    const QString afterJson = postCommitPayload(result);
    const bool logged = LegacyMutationLogSupport::logMutation(
        database,
        result.entityType.trimmed().isEmpty() ? QStringLiteral("mutation") : result.entityType.trimmed(),
        result.operation.trimmed().isEmpty() ? QStringLiteral("commit") : result.operation.trimmed(),
        0,
        QString(),
        result.entityId,
        QStringLiteral("{}"),
        afterJson,
        &logError);

    if (!logged) {
        qWarning().noquote() << QStringLiteral("DB_MUTATION post_commit_mutation_log_failed operationId=") << result.operationId
                             << QStringLiteral(" source=") << result.sourceModule
                             << QStringLiteral(" operation=") << result.operation
                             << QStringLiteral(" error=") << logError;
        return;
    }

    if (RuntimeLoggingControl::deepDebugEnabled()) {
        qInfo().noquote() << QStringLiteral("DB_MUTATION post_commit_mutation_log_ok operationId=") << result.operationId
                          << QStringLiteral(" source=") << result.sourceModule
                          << QStringLiteral(" operation=") << result.operation;
    }
}

} // namespace

namespace MutationPostCommitService {

void runAfterCommit(QSqlDatabase database, const MutationResult& result)
{
    if (!result.ok) return;

    appendPostCommitMutationLog(database, result);


    if (touchesPartsCache(result.changedTables)) {
        // Central invalidation keeps Parts/Reports/Stats snapshots consistent after any order/parts mutation.
        SnapshotCacheInvalidator::invalidateOrdersAndParts(result.sourceModule + QStringLiteral("/") + result.operation);
        if (RuntimeLoggingControl::deepDebugEnabled()) {
            qInfo().noquote() << QStringLiteral("DB_MUTATION post_commit_cache_invalidated operationId=") << result.operationId
                              << QStringLiteral(" source=") << result.sourceModule
                              << QStringLiteral(" operation=") << result.operation
                              << QStringLiteral(" changedTables=") << result.changedTables.join(QLatin1Char(','));
        }
    }

    // Workspace saves already validate/write CRM inside their mandatory transaction.
    if (result.sourceModule != QStringLiteral("OrderWorkspaceService")
        && result.entityType == QStringLiteral("work_order") && touchesTable(result.changedTables, QStringLiteral("work_orders"))) {
        bool ok = false;
        const int orderNumber = result.entityId.trimmed().toInt(&ok);
        if (ok && orderNumber > 0) {
            QString crmError;
            if (!CrmClientService::ensureFromWorkOrder(database, orderNumber, nullptr, nullptr, &crmError)) {
                qWarning().noquote() << QStringLiteral("CRM_ORDER_LINK_POST_COMMIT_SKIPPED order=%1 error=%2").arg(orderNumber).arg(crmError.trimmed());
            }
        }
    }

}

} // namespace MutationPostCommitService
