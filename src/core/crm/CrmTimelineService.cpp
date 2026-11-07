#include "core/crm/CrmTimelineService.h"

#include "core/cloudsync/EntityUidService.h"
#include "core/services/MutationExecutor.h"
#include "infrastructure/db/sqlite/Db.h"

#include <QSqlError>
#include <QSqlQuery>

namespace {

QSqlDatabase effectiveDb(QSqlDatabase db)
{
    return db.isValid() ? db : Database::db();
}

QString eventUidOrNew(const QString& eventUid)
{
    const QString trimmed = eventUid.trimmed();
    return trimmed.isEmpty() ? EntityUidService::generateUid(QStringLiteral("event")) : trimmed;
}

} // namespace

namespace CrmTimelineService {

bool addEventDirect(QSqlDatabase db,
                    const QString& eventUid,
                    const QString& clientUid,
                    const QString& vehicleUid,
                    const QString& orderUid,
                    const QString& eventType,
                    const QString& title,
                    const QString& body,
                    const QString& createdBy,
                    QString* errorText)
{
    db = effectiveDb(db);
    const QString uid = eventUidOrNew(eventUid);
    QSqlQuery q(db);
    q.prepare(QStringLiteral("INSERT OR IGNORE INTO crm_client_events(event_uid, client_uid, vehicle_uid, order_uid, event_type, title, body, created_by, created_at) "
                             "VALUES(?,?,?,?,?,?,?,?,datetime('now'))"));
    q.addBindValue(uid);
    q.addBindValue(clientUid.trimmed());
    q.addBindValue(vehicleUid.trimmed());
    q.addBindValue(orderUid.trimmed());
    q.addBindValue(eventType.trimmed());
    q.addBindValue(title.trimmed());
    q.addBindValue(body.trimmed());
    q.addBindValue(createdBy.trimmed());
    if (!q.exec()) {
        if (errorText) *errorText = q.lastError().text();
        return false;
    }
    return true;
}

bool addEvent(QSqlDatabase db,
              const QString& clientUid,
              const QString& vehicleUid,
              const QString& orderUid,
              const QString& eventType,
              const QString& title,
              const QString& body,
              const QString& createdBy,
              QString* eventUid,
              QString* errorText)
{
    db = effectiveDb(db);
    const QString uid = eventUidOrNew(eventUid ? *eventUid : QString());

    MutationExecutor::Request request;
    request.sourceModule = QStringLiteral("CrmTimelineService");
    request.entityType = QStringLiteral("crm_event");
    request.entityId = uid;
    request.operation = QStringLiteral("create");
    request.description = QString::fromUtf8("Створення події CRM");
    request.changedTables = {QStringLiteral("crm_client_events")};
    request.beginErrorFallback = QString::fromUtf8("Не вдалося відкрити транзакцію створення CRM-події.");
    request.commitErrorFallback = QString::fromUtf8("Не вдалося завершити створення CRM-події.");

    const auto result = MutationExecutor::execute(db, request, [&](QSqlDatabase& txDb, QString* txError) {
        return addEventDirect(txDb,
                              uid,
                              clientUid,
                              vehicleUid,
                              orderUid,
                              eventType,
                              title,
                              body,
                              createdBy,
                              txError);
    });

    if (!result.ok) {
        if (errorText) *errorText = result.errorText;
        return false;
    }
    if (eventUid) *eventUid = uid;
    return true;
}

} // namespace CrmTimelineService
