#include "core/services/MutationExecutor.h"

#include "infrastructure/db/sqlite/TransactionRunner.h"
#include "infrastructure/audit/MutationAuditTrail.h"
#include "core/services/MutationPostCommitService.h"
#include "core/services/ActivityAuditService.h"
#include "core/cloudsync/CloudSyncOutboxService.h"
#include "core/diagnostics/RuntimeLoggingControl.h"

#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QSqlError>
#include <QUuid>

namespace {

QJsonObject rowObject(QSqlQuery& query)
{
    QJsonObject object;
    const QSqlRecord record = query.record();
    for (int index = 0; index < record.count(); ++index) {
        object.insert(record.fieldName(index), QJsonValue::fromVariant(query.value(index)));
    }
    return object;
}

QJsonObject selectOne(QSqlDatabase database, const QString& sql, const QVariantList& values, QString* errorText)
{
    QSqlQuery query(database);
    if (!query.prepare(sql)) {
        if (errorText) *errorText = query.lastError().text();
        return {};
    }
    for (const QVariant& value : values) query.addBindValue(value);
    if (!query.exec()) {
        if (errorText) *errorText = query.lastError().text();
        return {};
    }
    if (query.next()) return rowObject(query);
    if (query.lastError().isValid() && errorText) *errorText = query.lastError().text();
    return {};
}

QJsonArray selectRows(QSqlDatabase database, const QString& sql, const QVariantList& values, QString* errorText)
{
    QJsonArray rows;
    QSqlQuery query(database);
    if (!query.prepare(sql)) {
        if (errorText) *errorText = query.lastError().text();
        return {};
    }
    for (const QVariant& value : values) query.addBindValue(value);
    if (!query.exec()) {
        if (errorText) *errorText = query.lastError().text();
        return rows;
    }
    while (query.next()) rows.append(rowObject(query));
    if (query.lastError().isValid() && errorText) *errorText = query.lastError().text();
    return rows;
}

QJsonObject snapshotForAudit(QSqlDatabase database, const MutationExecutor::Request& request, QString* errorText)
{
    const QString entity = request.entityType.trimmed().toLower();
    const QString operation = request.operation.trimmed().toLower();
    const QString id = request.entityId.trimmed();
    if (id.isEmpty()) return {};

    if (entity == QStringLiteral("crm_task")) {
        return selectOne(database, QStringLiteral("SELECT * FROM crm_client_tasks WHERE task_uid=?"), {id}, errorText);
    }
    if (entity == QStringLiteral("crm_event")) {
        return selectOne(database, QStringLiteral("SELECT * FROM crm_client_events WHERE event_uid=?"), {id}, errorText);
    }

    if (entity == QStringLiteral("work_order")) {
        return selectOne(database, QStringLiteral("SELECT * FROM work_orders WHERE number=?"), {id}, errorText);
    }
    if ((entity == QStringLiteral("work_order_service") || entity == QStringLiteral("service_attachment"))) {
        if (operation == QStringLiteral("insert_service")) {
            return selectOne(database, QStringLiteral("SELECT * FROM work_order_services WHERE order_number=? ORDER BY id DESC LIMIT 1"), {id}, errorText);
        }
        if (entity == QStringLiteral("service_attachment")) {
            return selectOne(database, QStringLiteral("SELECT * FROM work_order_service_attachments WHERE service_id=?"), {id}, errorText);
        }
        return selectOne(database, QStringLiteral("SELECT * FROM work_order_services WHERE id=?"), {id}, errorText);
    }
    if (entity == QStringLiteral("work_order_service_batch")) {
        QJsonObject snapshot;
        snapshot.insert(QStringLiteral("services"), selectRows(database,
            QStringLiteral("SELECT * FROM work_order_services WHERE order_number=? ORDER BY id"), {id}, errorText));
        return snapshot;
    }
    if ((entity == QStringLiteral("work_order_part") || entity == QStringLiteral("part"))) {
        return selectOne(database, QStringLiteral("SELECT * FROM work_order_parts WHERE id=?"), {id}, errorText);
    }
    if (entity == QStringLiteral("part_attachment")) {
        return selectOne(database, QStringLiteral("SELECT * FROM work_order_part_attachments WHERE part_id=?"), {id}, errorText);
    }
    return {};
}

bool isCreationOperation(const QString& operation)
{
    return operation.startsWith(QStringLiteral("insert")) || operation == QStringLiteral("create")
        || operation.startsWith(QStringLiteral("add_"));
}

bool isDeletionOperation(const QString& operation)
{
    return operation.contains(QStringLiteral("delete")) || operation.contains(QStringLiteral("remove"));
}

QString compactJson(const QJsonObject& object)
{
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

} // namespace

namespace MutationExecutor {

Result execute(QSqlDatabase db,
               const Request& request,
               const Step& step)
{
    Result result;
    result.operationId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    result.sourceModule = request.sourceModule;
    result.entityType = request.entityType;
    result.entityId = request.entityId;
    result.operation = request.operation;
    result.changedTables = request.changedTables;
    result.description = request.description;
    result.systemMaintenance = request.systemMaintenance;

    const QString transactionOwner = QStringLiteral("MutationExecutor:%1:%2").arg(result.sourceModule, result.operation);
    const QString normalizedOperation = result.operation.trimmed().toLower();

    MutationAuditTrail::appendEvent(result, QStringLiteral("started"));
    if (RuntimeLoggingControl::deepDebugEnabled()) {
        qInfo().noquote() << QStringLiteral("DB_MUTATION executor_started operationId=") << result.operationId
                          << QStringLiteral(" source=") << result.sourceModule
                          << QStringLiteral(" entity=") << result.entityType
                          << QStringLiteral(" entityId=") << result.entityId
                          << QStringLiteral(" operation=") << result.operation
                          << QStringLiteral(" systemMaintenance=") << result.systemMaintenance
                          << QStringLiteral(" connection=") << db.connectionName()
                          << QStringLiteral(" transactionOwner=") << transactionOwner
                          << QStringLiteral(" phase=BEGIN");
    }

    const auto txResult = TransactionRunner::run(
        db,
        request.beginErrorFallback,
        request.commitErrorFallback,
        [&](QString* errorText) {
            QString snapshotError;
            if (!result.systemMaintenance && !isCreationOperation(normalizedOperation)) {
                result.auditBeforeJson = compactJson(snapshotForAudit(db, request, &snapshotError));
                if (!snapshotError.isEmpty()) {
                    if (errorText) *errorText = QStringLiteral("Audit snapshot before mutation: %1").arg(snapshotError);
                    return false;
                }
            }
            if (!step(db, errorText)) return false;
            // Creation callbacks resolve the durable identity inside the transaction.
            // Audit/outbox records must use that result rather than a provisional ID.
            result.entityId = request.entityId;
            result.operation = request.operation;
            result.description = request.description;
            result.changedTables = request.changedTables;
            if (!result.systemMaintenance && !isDeletionOperation(request.operation.trimmed().toLower())) {
                result.auditAfterJson = compactJson(snapshotForAudit(db, request, &snapshotError));
                if (!snapshotError.isEmpty()) {
                    if (errorText) *errorText = QStringLiteral("Audit snapshot after mutation: %1").arg(snapshotError);
                    return false;
                }
            }
            // These writers require a successful mutation; publication still waits for COMMIT.
            auto transactionalResult = result;
            transactionalResult.ok = true;
            if (!ActivityAuditService::recordMutation(db, transactionalResult, errorText)) {
                if (errorText) *errorText = QStringLiteral("Не вдалося зберегти обов'язковий аудит. Зміни скасовано: %1").arg(*errorText);
                return false;
            }
            if (!CloudSyncOutboxService::appendMutation(db, transactionalResult, errorText)) return false;
            return true;
        });

    result.ok = txResult.ok;
    result.errorText = txResult.errorText;

    if (RuntimeLoggingControl::deepDebugEnabled()) {
        qInfo().noquote() << QStringLiteral("DB_MUTATION executor_finished operationId=") << result.operationId
                          << QStringLiteral(" ok=") << result.ok
                          << QStringLiteral(" error=") << result.errorText
                          << QStringLiteral(" systemMaintenance=") << result.systemMaintenance
                          << QStringLiteral(" connection=") << db.connectionName()
                          << QStringLiteral(" transactionOwner=") << transactionOwner
                          << QStringLiteral(" phase=") << (result.ok ? QStringLiteral("COMMIT") : QStringLiteral("ROLLBACK"));
    }
    if (result.ok) {
        MutationPostCommitService::runAfterCommit(db, result);
    }
    MutationAuditTrail::appendEvent(result, result.ok ? QStringLiteral("completed") : QStringLiteral("failed"));
    return result;
}

} // namespace MutationExecutor
