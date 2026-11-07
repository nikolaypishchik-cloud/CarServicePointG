#include "core/crm/CrmTaskService.h"

#include "core/cloudsync/EntityUidService.h"
#include "core/services/MutationExecutor.h"
#include "core/crm/CrmTimelineService.h"
#include "core/services/ActivityAuditService.h"
#include "core/cloudsync/CloudSyncOutboxService.h"
#include "infrastructure/db/sqlite/Db.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QVariantMap>
#include <QVariant>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>

namespace {

QSqlDatabase effectiveDb(QSqlDatabase db)
{
    return db.isValid() ? db : Database::db();
}

QString taskUidOrNew(const QString& taskUid)
{
    const QString trimmed = taskUid.trimmed();
    return trimmed.isEmpty() ? EntityUidService::generateUid(QStringLiteral("task")) : trimmed;
}

} // namespace

namespace CrmTaskService {

bool createTaskWithTimeline(const QString& clientUid, const QString& vehicleUid,
                            const QString& title, const QString& dueAt,
                            QString* errorText, QSqlDatabase db, const QString& requestId)
{
    if (errorText) errorText->clear();
    if (clientUid.trimmed().isEmpty() || title.trimmed().isEmpty()) {
        if (errorText) *errorText = QString::fromUtf8("Вкажіть клієнта та назву задачі.");
        return false;
    }
    db = effectiveDb(db);
    // Receipt is local recovery metadata, committed with the business operation.
    const auto key = requestId.trimmed();
    const auto fingerprint = QCryptographicHash::hash(QJsonDocument(QJsonArray{
        clientUid.trimmed(), vehicleUid.trimmed(), title.trimmed(), dueAt.trimmed()
    }).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex();
    if (!key.isEmpty()) {
        QSqlQuery receipt(db);
        if (!receipt.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS crm_task_requests (request_id TEXT PRIMARY KEY, fingerprint TEXT NOT NULL, task_uid TEXT NOT NULL)"))) {
            if (errorText) *errorText = receipt.lastError().text();
            return false;
        }
        receipt.prepare(QStringLiteral("SELECT fingerprint FROM crm_task_requests WHERE request_id=?"));
        receipt.addBindValue(key);
        if (!receipt.exec()) {
            if (errorText) *errorText = receipt.lastError().text();
            return false;
        }
        if (receipt.next()) {
            if (receipt.value(0).toByteArray() == fingerprint) return true;
            if (errorText) *errorText = QString::fromUtf8("Цей запит уже виконано з іншими даними. Створіть новий запит.");
            return false;
        }
        if (receipt.lastError().isValid()) {
            if (errorText) *errorText = receipt.lastError().text();
            return false;
        }
    }
    const auto taskUid = EntityUidService::generateUid(QStringLiteral("task"));
    const auto eventUid = EntityUidService::generateUid(QStringLiteral("event"));
    MutationExecutor::Request request;
    request.sourceModule = QStringLiteral("CrmTaskService");
    request.entityType = QStringLiteral("crm_task");
    request.entityId = taskUid;
    request.operation = QStringLiteral("create");
    request.description = QString::fromUtf8("Створення CRM-задачі та запису історії");
    request.changedTables = {QStringLiteral("crm_client_tasks"), QStringLiteral("crm_client_events")};
    const auto result = MutationExecutor::execute(db, request, [&](QSqlDatabase& tx, QString* error) {
        if (!key.isEmpty()) {
            QSqlQuery receipt(tx);
            receipt.prepare(QStringLiteral("INSERT INTO crm_task_requests(request_id,fingerprint,task_uid) VALUES(?,?,?)"));
            receipt.addBindValue(key);receipt.addBindValue(QString::fromLatin1(fingerprint));receipt.addBindValue(taskUid);
            if (!receipt.exec()) { if (error) *error = receipt.lastError().text(); return false; }
        }
        if (!createTaskDirect(tx, taskUid, clientUid, vehicleUid, QStringLiteral(""), title,
                              dueAt, QStringLiteral("normal"), QStringLiteral(""), error)) return false;
        if (!CrmTimelineService::addEventDirect(tx, eventUid, clientUid,
                vehicleUid.isEmpty() ? QStringLiteral("") : vehicleUid, QStringLiteral(""),
                QStringLiteral("task_created"), QString::fromUtf8("Створено задачу"),
                title, QStringLiteral("user"), error)) return false;
        // The executor records the task. Record the second entity in the same transaction.
        MutationResult event;
        event.ok = true; event.operationId = eventUid;
        event.sourceModule = request.sourceModule;
        event.entityType = QStringLiteral("crm_event"); event.entityId = eventUid;
        event.operation = QStringLiteral("create"); event.description = request.description;
        event.changedTables = {QStringLiteral("crm_client_events")};
        return ActivityAuditService::recordMutation(tx, event, error)
            && CloudSyncOutboxService::appendMutation(tx, event, error);
    });
    if (!result.ok && errorText) *errorText = result.errorText;
    return result.ok;
}

bool createTaskDirect(QSqlDatabase db,
                      const QString& taskUid,
                      const QString& clientUid,
                      const QString& vehicleUid,
                      const QString& orderUid,
                      const QString& title,
                      const QString& dueAt,
                      const QString& priority,
                      const QString& assignedTo,
                      QString* errorText)
{
    db = effectiveDb(db);
    const QString uid = taskUidOrNew(taskUid);
    QSqlQuery q(db);
    q.prepare(QStringLiteral("INSERT INTO crm_client_tasks(task_uid, client_uid, vehicle_uid, order_uid, title, due_at, status, priority, assigned_to, created_at, completed_at) "
                             "VALUES(?,?,?,?,?,?, 'open',?,?,datetime('now'),'')"));
    q.addBindValue(uid);
    q.addBindValue(clientUid.trimmed());
    q.addBindValue(vehicleUid.isEmpty() ? QStringLiteral("") : vehicleUid.trimmed());
    q.addBindValue(orderUid.isEmpty() ? QStringLiteral("") : orderUid.trimmed());
    q.addBindValue(title.trimmed());
    q.addBindValue(dueAt.isEmpty() ? QStringLiteral("") : dueAt.trimmed());
    q.addBindValue(priority.trimmed().isEmpty() ? QStringLiteral("normal") : priority.trimmed());
    q.addBindValue(assignedTo.isEmpty() ? QStringLiteral("") : assignedTo.trimmed());
    if (!q.exec()) {
        if (errorText) *errorText = q.lastError().text();
        return false;
    }
    return true;
}

bool createTask(QSqlDatabase db,
                const QString& clientUid,
                const QString& vehicleUid,
                const QString& orderUid,
                const QString& title,
                const QString& dueAt,
                const QString& priority,
                const QString& assignedTo,
                QString* taskUid,
                QString* errorText)
{
    db = effectiveDb(db);
    const QString uid = taskUidOrNew(taskUid ? *taskUid : QString());

    MutationExecutor::Request request;
    request.sourceModule = QStringLiteral("CrmTaskService");
    request.entityType = QStringLiteral("crm_task");
    request.entityId = uid;
    request.operation = QStringLiteral("create");
    request.description = QString::fromUtf8("Створення CRM-задачі");
    request.changedTables = {QStringLiteral("crm_client_tasks")};
    request.beginErrorFallback = QString::fromUtf8("Не вдалося відкрити транзакцію створення CRM-задачі.");
    request.commitErrorFallback = QString::fromUtf8("Не вдалося завершити створення CRM-задачі.");

    const auto result = MutationExecutor::execute(db, request, [&](QSqlDatabase& txDb, QString* txError) {
        return createTaskDirect(txDb,
                                uid,
                                clientUid,
                                vehicleUid,
                                orderUid,
                                title,
                                dueAt,
                                priority,
                                assignedTo,
                                txError);
    });

    if (!result.ok) {
        if (errorText) *errorText = result.errorText;
        return false;
    }
    if (taskUid) *taskUid = uid;
    return true;
}

bool markCompletedDirect(QSqlDatabase db, const QString& taskUid, QString* errorText)
{
    if (errorText) errorText->clear();
    db = effectiveDb(db);
    QSqlQuery q(db);
    q.prepare(QStringLiteral("UPDATE crm_client_tasks SET status='completed', completed_at=datetime('now') WHERE task_uid=? AND COALESCE(status,'')<>'completed'"));
    q.addBindValue(taskUid.trimmed());
    if (!q.exec()) {
        if (errorText) *errorText = q.lastError().text();
        return false;
    }
    if (q.numRowsAffected() != 1) {
        if (errorText) *errorText = QString::fromUtf8("Задачу не знайдено або вже завершено. Оновіть список задач.");
        return false;
    }
    return true;
}


bool upsertTaskDirect(QSqlDatabase db,
                      const QString& taskUid,
                      const QVariantMap& fields,
                      QString* errorText)
{
    db = effectiveDb(db);
    const QString uid = taskUidOrNew(taskUid);
    static const QStringList allowed = {
        QStringLiteral("client_uid"), QStringLiteral("vehicle_uid"), QStringLiteral("order_uid"),
        QStringLiteral("title"), QStringLiteral("due_at"), QStringLiteral("status"), QStringLiteral("priority"),
        QStringLiteral("assigned_to"), QStringLiteral("created_at"), QStringLiteral("completed_at")
    };

    QStringList assignments;
    QList<QVariant> values;
    for (const QString& key : allowed) {
        if (!fields.contains(key)) continue;
        assignments.append(QStringLiteral("%1=?").arg(key));
        values.append(fields.value(key));
    }
    if (!assignments.isEmpty()) {
        QSqlQuery update(db);
        update.prepare(QStringLiteral("UPDATE crm_client_tasks SET %1 WHERE task_uid=?").arg(assignments.join(QLatin1Char(','))));
        for (const QVariant& value : values) update.addBindValue(value);
        update.addBindValue(uid);
        if (!update.exec()) {
            if (errorText) *errorText = update.lastError().text();
            return false;
        }
        if (update.numRowsAffected() > 0) return true;
    }

    QSqlQuery insert(db);
    insert.prepare(QStringLiteral("INSERT INTO crm_client_tasks(task_uid, client_uid, vehicle_uid, order_uid, title, due_at, status, priority, assigned_to, created_at, completed_at) "
                                   "VALUES(?,?,?,?,?,?,?,?,?,COALESCE(NULLIF(?,''),datetime('now')),?)"));
    insert.addBindValue(uid);
    insert.addBindValue(fields.value(QStringLiteral("client_uid")).toString().trimmed());
    insert.addBindValue(fields.value(QStringLiteral("vehicle_uid")).toString().trimmed());
    insert.addBindValue(fields.value(QStringLiteral("order_uid")).toString().trimmed());
    insert.addBindValue(fields.value(QStringLiteral("title")).toString().trimmed());
    insert.addBindValue(fields.value(QStringLiteral("due_at")).toString().trimmed());
    insert.addBindValue(fields.value(QStringLiteral("status"), QStringLiteral("open")).toString().trimmed().isEmpty() ? QStringLiteral("open") : fields.value(QStringLiteral("status")).toString().trimmed());
    insert.addBindValue(fields.value(QStringLiteral("priority"), QStringLiteral("normal")).toString().trimmed().isEmpty() ? QStringLiteral("normal") : fields.value(QStringLiteral("priority")).toString().trimmed());
    insert.addBindValue(fields.value(QStringLiteral("assigned_to")).toString().trimmed());
    insert.addBindValue(fields.value(QStringLiteral("created_at")).toString().trimmed());
    insert.addBindValue(fields.value(QStringLiteral("completed_at")).toString().trimmed());
    if (!insert.exec()) {
        if (errorText) *errorText = insert.lastError().text();
        return false;
    }
    return true;
}

bool markCompleted(QSqlDatabase db, const QString& taskUid, QString* errorText)
{
    if (errorText) errorText->clear();
    db = effectiveDb(db);
    const QString uid = taskUid.trimmed();
    if (uid.isEmpty()) {
        if (errorText) *errorText = QString::fromUtf8("Не вибрано задачу для завершення.");
        return false;
    }

    MutationExecutor::Request request;
    request.sourceModule = QStringLiteral("CrmTaskService");
    request.entityType = QStringLiteral("crm_task");
    request.entityId = uid;
    request.operation = QStringLiteral("update");
    request.description = QString::fromUtf8("Завершення CRM-задачі");
    request.changedTables = {QStringLiteral("crm_client_tasks")};
    request.beginErrorFallback = QString::fromUtf8("Не вдалося відкрити транзакцію завершення CRM-задачі.");
    request.commitErrorFallback = QString::fromUtf8("Не вдалося завершити CRM-задачу.");

    const auto result = MutationExecutor::execute(db, request, [&](QSqlDatabase& txDb, QString* txError) {
        return markCompletedDirect(txDb, uid, txError);
    });
    if (!result.ok) {
        if (errorText) *errorText = result.errorText;
        return false;
    }
    return true;
}

} // namespace CrmTaskService
