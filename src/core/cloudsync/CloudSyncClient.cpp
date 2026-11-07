#include "core/util/DateFormats.h"
#include "core/cloudsync/CloudSyncClient.h"
#include "core/cloudsync/SyncBatchIsolation.h"

#include "core/cloudsync/CloudSyncInboxService.h"
#include "core/cloudsync/CloudSyncOutboxService.h"
#include "core/cloudsync/ConflictResolverService.h"
#include "core/cloudsync/CloudSyncSettings.h"
#include "core/cloudsync/DeviceIdentityService.h"
#include "core/cloudsync/RemoteMutationApplyService.h"
#include "core/services/ActivityAuditService.h"
#include "infrastructure/db/sqlite/Db.h"
#include "runtime/RuntimeJobDispatcher.h"

#include <QDateTime>
#include <QScopeGuard>
#include "runtime/WorkerNetwork.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSqlError>
#include <QSqlQuery>
#include <QTimer>
#include <QThread>
#include <QUrl>
#include <QUrlQuery>

namespace {

bool syncCancelled(QString* errorText)
{
    if (!QThread::currentThread()->isInterruptionRequested()
        && !RuntimeJob::RuntimeJobDispatcher::instance().isShutdownRequested()) return false;
    if (errorText) *errorText = QString::fromUtf8("Синхронізацію зупинено. Незавершені зміни залишено для повторної спроби.");
    return true;
}

bool manualCloudSyncNetworkAllowed(const CloudSyncSettings::Settings& settings, QString* errorText)
{
    if (!settings.enabled) {
        if (errorText) {
            *errorText = QString::fromUtf8("Cloud sync вимкнено. Мережеву перевірку можна запускати тільки вручну після увімкнення cloudsync.");
        }
        return false;
    }
    if (settings.serverUrl.trimmed().isEmpty()) {
        if (errorText) *errorText = QString::fromUtf8("Не заданий Business Server API URL.");
        return false;
    }
    return true;
}

struct HttpResult {
    int status = 0;
    QByteArray body;
    QString error;
};

QString utcNow()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

QUrl endpointUrl(const CloudSyncSettings::Settings& settings, const QString& path)
{
    QString base = settings.serverUrl.trimmed();
    while (base.endsWith(QLatin1Char('/'))) base.chop(1);

    // Users often paste either https://host or https://host/api/v1.
    // Desktop calls always pass /api/v1/... paths, so strip the suffix once
    // to avoid broken /api/v1/api/v1/... requests.
    const QString apiSuffix = QStringLiteral("/api/v1");
    if (base.endsWith(apiSuffix, Qt::CaseInsensitive)) {
        base.chop(apiSuffix.size());
        while (base.endsWith(QLatin1Char('/'))) base.chop(1);
    }

    QString suffix = path.trimmed();
    if (!suffix.startsWith(QLatin1Char('/'))) suffix.prepend(QLatin1Char('/'));
    return QUrl(base + suffix);
}

QNetworkRequest buildRequest(const CloudSyncSettings::Settings& settings, const QString& path)
{
    QNetworkRequest req(endpointUrl(settings, path));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("CarService-CloudSync/2.1"));
    req.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    if (!settings.apiToken.trimmed().isEmpty()) {
        req.setRawHeader("Authorization", QByteArray("Bearer ") + settings.apiToken.trimmed().toUtf8());
    }
    return req;
}

HttpResult runJsonRequest(const QString& method, const QString& path, const QJsonObject& body = QJsonObject())
{
    HttpResult result;
    if (syncCancelled(&result.error)) return result;
    const auto settings = CloudSyncSettings::load();
    if (!manualCloudSyncNetworkAllowed(settings, &result.error)) return result;

    const QUrl url = endpointUrl(settings, path);
    if (!url.isValid() || url.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) != 0 || url.host().trimmed().isEmpty()) {
        result.error = QString::fromUtf8("Для інтернет-синхронізації вкажіть коректну HTTPS-адресу сервера.");
        return result;
    }

    const QString normalizedMethod = method.trimmed().toUpper();
    if(normalizedMethod!="GET" && normalizedMethod!="POST") { result.error="Unsupported sync method";return result; }
    const auto reply=WorkerNetwork::request(buildRequest(settings,path),QJsonDocument(body).toJson(QJsonDocument::Compact),normalizedMethod=="POST",20000,[&]{return !syncCancelled(&result.error);});
    if (!result.error.isEmpty() || syncCancelled(&result.error)) return result;

    if (reply.stopped) {
        
        result.error = QString::fromUtf8("Сервер синхронізації не відповів за 20 секунд.");
        return result;
    }

    result.status = reply.status;
    result.body = reply.body;
    if (reply.error == QNetworkReply::NoError && result.status >= 200 && result.status < 300) return result;

    QString detail;
    const QJsonDocument response = QJsonDocument::fromJson(result.body);
    if (response.isObject()) detail = response.object().value(QStringLiteral("detail")).toString().trimmed();
    if (detail.isEmpty()) detail = QString::fromUtf8(result.body).trimmed();
    if (detail.isEmpty()) detail = reply.message.trimmed();
    result.error = result.status > 0
        ? QString::fromUtf8("Сервер повернув HTTP %1: %2").arg(result.status).arg(detail)
        : QString::fromUtf8("Помилка мережі: %1").arg(detail);
    return result;
}

bool okOrStoreError(const HttpResult& result, QString* errorText)
{
    if (result.error.isEmpty()) return true;
    CloudSyncSettings::setLastError(result.error);
    if (errorText) *errorText = result.error;
    return false;
}

QString auditCategory(const QString& action, const QString& entityType)
{
    const QString input = (action + QLatin1Char(' ') + entityType).toLower();
    if (input.contains(QStringLiteral("pairing")) || input.contains(QStringLiteral("device"))) return QStringLiteral("session");
    if (input.contains(QStringLiteral("part"))) return QStringLiteral("parts");
    if (input.contains(QStringLiteral("service")) || input.contains(QStringLiteral("work_order")) || input.contains(QStringLiteral("order"))) return QStringLiteral("work");
    if (input.contains(QStringLiteral("material")) || input.contains(QStringLiteral("inventory"))) return QStringLiteral("inventory");
    if (input.contains(QStringLiteral("access")) || input.contains(QStringLiteral("role"))) return QStringLiteral("access");
    return QStringLiteral("system");
}

QString auditTitle(const QString& action)
{
    const QString normalized = action.trimmed().toLower();
    if (normalized == QStringLiteral("mobile_pairing_created")) return QString::fromUtf8("Створено QR-код прив'язки телефону");
    if (normalized == QStringLiteral("mobile_pairing_claimed")) return QString::fromUtf8("Працівник підключив Android-телефон");
    if (normalized == QStringLiteral("mobile_device_online")) return QString::fromUtf8("Телефон працівника знову онлайн");
    if (normalized == QStringLiteral("mobile_service_completed")) return QString::fromUtf8("Роботу виконано з телефона");
    if (normalized == QStringLiteral("mobile_part_installed")) return QString::fromUtf8("Запчастину або матеріал встановлено з телефона");
    if (normalized == QStringLiteral("mobile_part_request_created") || normalized == QStringLiteral("mobile_diagnostic_part_request_created")) return QString::fromUtf8("Майстер подав заявку на запчастину");
    if (normalized == QStringLiteral("mobile_part_request_ordered")) return QString::fromUtf8("Адміністратор позначив запчастину як замовлену");
    if (normalized == QStringLiteral("mobile_part_request_approved")) return QString::fromUtf8("Заявку на запчастину схвалено");
    if (normalized == QStringLiteral("mobile_part_request_clarification_requested")) return QString::fromUtf8("Потрібне уточнення заявки на запчастину");
    if (normalized == QStringLiteral("mobile_part_request_cancelled")) return QString::fromUtf8("Заявку на запчастину скасовано");
    if (normalized == QStringLiteral("repair_ai_service_added")) return QString::fromUtf8("До наряду додано погоджену роботу після діагностики");
    if (normalized == QStringLiteral("mobile_order_created")) return QString::fromUtf8("Майстер створив новий наряд з телефона");
    if (normalized == QStringLiteral("mobile_planning_created")) return QString::fromUtf8("Майстер додав запис до планування з телефона");
    if (normalized == QStringLiteral("mobile_order_photo_added") || normalized == QStringLiteral("mobile_photo_uploaded")) return QString::fromUtf8("До наряду додано фото з телефона");
    if (normalized == QStringLiteral("mobile_recommendation_added")) return QString::fromUtf8("Додано рекомендацію з телефона");
    if (normalized == QStringLiteral("mobile_customer_signature_added") || normalized == QStringLiteral("mobile_customer_signature_uploaded")) return QString::fromUtf8("Додано підпис клієнта з телефона");
    return action.trimmed().isEmpty() ? QString::fromUtf8("Серверна подія") : action;
}

QDateTime auditOccurredAt(const QString& value)
{
    const QDateTime timestamp = DateFormats::parseDateTime(value);
    return timestamp.isValid() ? timestamp : QDateTime::currentDateTimeUtc();
}

QString auditSubject(const QJsonObject& item, const QJsonObject& metadata)
{
    QStringList parts;
    const QString employee = metadata.value(QStringLiteral("employee_name")).toString().trimmed();
    const QString requestedBy = metadata.value(QStringLiteral("requested_by_name")).toString().trimmed();
    const QString decidedBy = metadata.value(QStringLiteral("decided_by_name")).toString().trimmed();
    const QString actor = item.value(QStringLiteral("actor_login")).toString().trimmed();
    const QString person = !requestedBy.isEmpty() ? requestedBy
        : (!decidedBy.isEmpty() ? decidedBy : (!employee.isEmpty() ? employee : actor));
    const QString device = metadata.value(QStringLiteral("device_name")).toString().trimmed();
    const QString orderNumber = metadata.value(QStringLiteral("order_number")).toVariant().toString().trimmed();
    const QString orderUid = metadata.value(QStringLiteral("order_uid")).toString().trimmed();
    const QString partName = metadata.value(QStringLiteral("part_name")).toString().trimmed();
    const QString serviceTitle = metadata.value(QStringLiteral("title")).toString().trimmed();

    if (!person.isEmpty()) parts.append(person);
    if (!orderNumber.isEmpty()) parts.append(QString::fromUtf8("Наряд №%1").arg(orderNumber));
    else if (!orderUid.isEmpty()) parts.append(QString::fromUtf8("Наряд: %1").arg(orderUid));
    if (!partName.isEmpty()) parts.append(partName);
    else if (!serviceTitle.isEmpty()) parts.append(serviceTitle);
    if (!device.isEmpty()) parts.append(device);
    if (parts.isEmpty() && !item.value(QStringLiteral("entity_uid")).toString().isEmpty()) {
        parts.append(item.value(QStringLiteral("entity_uid")).toString());
    }
    return parts.join(QStringLiteral(" | "));
}
bool importAuditEvents(QSqlDatabase db, const QJsonObject& root, QString* errorText)
{
    for (const QJsonValue& value : root.value(QStringLiteral("items")).toArray()) {
        const QJsonObject item = value.toObject();
        const QString auditUid = item.value(QStringLiteral("audit_uid")).toString().trimmed();
        if (auditUid.isEmpty()) continue;
        const QByteArray rawMetadata = item.value(QStringLiteral("metadata")).toString().toUtf8();
        QJsonObject metadata = QJsonDocument::fromJson(rawMetadata).object();
        // Preserve server snapshots so the desktop journal can explain what changed,
        // not only that a synchronized event happened.
        if (!item.value(QStringLiteral("before")).isUndefined() && !item.value(QStringLiteral("before")).isNull()) {
            metadata.insert(QStringLiteral("before"), item.value(QStringLiteral("before")));
        }
        if (!item.value(QStringLiteral("after")).isUndefined() && !item.value(QStringLiteral("after")).isNull()) {
            metadata.insert(QStringLiteral("after"), item.value(QStringLiteral("after")));
        }
        metadata.insert(QStringLiteral("server_source"), item.value(QStringLiteral("source")).toString());
        const ActivityAuditActor actor{
            item.value(QStringLiteral("actor_user_uid")).toString(),
            item.value(QStringLiteral("actor_login")).toString(),
            item.value(QStringLiteral("actor_role")).toString(),
            QString(),
        };
        const QString action = item.value(QStringLiteral("action")).toString();
        const QString entityType = item.value(QStringLiteral("entity_type")).toString();
        const QString orderValue = metadata.value(QStringLiteral("order_number")).toVariant().toString();
        bool hasOrderNumber = false;
        const int orderNumber = orderValue.toInt(&hasOrderNumber);
        if (!ActivityAuditService::recordExternal(
                db,
                auditUid,
                auditOccurredAt(item.value(QStringLiteral("created_at")).toString()),
                actor,
                auditCategory(action, entityType),
                action,
                auditTitle(action),
                hasOrderNumber ? orderNumber : 0,
                entityType,
                item.value(QStringLiteral("entity_uid")).toString(),
                auditSubject(item, metadata),
                QString::fromUtf8(QJsonDocument(metadata).toJson(QJsonDocument::Compact)),
                errorText)) {
            return false;
        }
    }
    return true;
}

bool pullRemoteAuditEvents(QString* errorText)
{
    const auto settings = CloudSyncSettings::load();
    QString path = QStringLiteral("/api/v1/audit/events?limit=1000");
    if (!settings.lastAuditPullAt.trimmed().isEmpty()) {
        path += QStringLiteral("&since=") + QString::fromUtf8(QUrl::toPercentEncoding(settings.lastAuditPullAt));
    }
    const HttpResult result = runJsonRequest(QStringLiteral("GET"), path);
    if (result.status == 403) return true; // Only the owner may read the centralized audit journal.
    if (!okOrStoreError(result, errorText)) return false;

    const QJsonDocument document = QJsonDocument::fromJson(result.body);
    if (!document.isObject()) {
        const QString message = QString::fromUtf8("Сервер повернув некоректний журнал подій.");
        CloudSyncSettings::setLastError(message);
        if (errorText) *errorText = message;
        return false;
    }
    QString localError;
    if (!importAuditEvents(Database::db(), document.object(), &localError)) {
        CloudSyncSettings::setLastError(localError);
        if (errorText) *errorText = localError;
        return false;
    }
    const QString watermark = document.object().value(QStringLiteral("server_time")).toString().trimmed();
    CloudSyncSettings::setLastAuditPullAt(watermark.isEmpty() ? utcNow() : watermark);
    return true;
}

QJsonObject devicePayload(QSqlDatabase db)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("device_uid"), DeviceIdentityService::deviceUid(db));
    obj.insert(QStringLiteral("device_name"), DeviceIdentityService::deviceName(db));
    obj.insert(QStringLiteral("device_type"), QStringLiteral("desktop"));
    obj.insert(QStringLiteral("organization_uid"), CloudSyncSettings::load().organizationUid);
    obj.insert(QStringLiteral("branch_uid"), DeviceIdentityService::branchUid(db));
    obj.insert(QStringLiteral("last_seen_at"), utcNow());
    return obj;
}

QJsonArray pendingOutboxEvents(QSqlDatabase db, QString* errorText)
{
    QJsonArray arr;
    // Recover from an interrupted desktop process or network abort. Events left
    // in `sending` are safe to retry after a short timeout because the server
    // de-duplicates by event_uid.
    {
        QSqlQuery recover(db);
        if (!recover.exec(QStringLiteral(
                "UPDATE sync_outbox "
                "SET status='pending', last_error='Recovered interrupted sending state' "
                "WHERE status='sending' AND datetime(created_at) <= datetime('now','-15 minutes')"))) {
            if (errorText) *errorText = recover.lastError().text();
            return arr;
        }
    }
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT event_uid,payload_json FROM sync_outbox WHERE status IN ('pending','failed') "
                             "AND (COALESCE(next_retry_at,'')='' OR datetime(next_retry_at)<=datetime('now')) "
                             "ORDER BY CASE status WHEN 'pending' THEN 0 ELSE 1 END,created_at,id LIMIT 100"));
    if (!q.exec()) {
        if (errorText) *errorText = q.lastError().text();
        return arr;
    }
    while (q.next()) {
        QJsonObject fallback;
        fallback.insert(QStringLiteral("event_uid"), q.value(0).toString());
        const QJsonDocument doc = QJsonDocument::fromJson(q.value(1).toString().toUtf8());
        arr.append(doc.isObject() ? doc.object() : fallback);
    }
    return arr;
}

QSqlDatabase effectiveDb(QSqlDatabase db)
{
    return db.isValid() ? db : Database::db();
}

bool markOutboxStatus(QSqlDatabase db, const QStringList& eventUids, const QString& status, const QString& errorText = QString())
{
    if (eventUids.isEmpty()) return true;
    for (const QString& uid : eventUids) {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("UPDATE sync_outbox SET status=?, last_error=?, retry_count=CASE WHEN ?='failed' THEN retry_count+1 ELSE retry_count END, "
                                 "sent_at=CASE WHEN ? IN ('sent','acked') THEN datetime('now') ELSE sent_at END, "
                                 "server_ack_at=CASE WHEN ?='acked' THEN datetime('now') ELSE server_ack_at END, "
                                 "next_retry_at=CASE WHEN ?='failed' THEN datetime('now','+5 minutes') ELSE '' END "
                                 "WHERE event_uid=?"));
        q.addBindValue(status);
        const QString safeErrorText = errorText.trimmed();
        q.addBindValue(safeErrorText.isNull() ? QStringLiteral("") : safeErrorText);
        q.addBindValue(status);
        q.addBindValue(status);
        q.addBindValue(status);
        q.addBindValue(status);
        q.addBindValue(uid.trimmed());
        if (!q.exec()) {
            qWarning().noquote() << QStringLiteral("CLOUDSYNC outbox_status_update_failed status=") << status
                                 << QStringLiteral(" event_uid=") << uid
                                 << QStringLiteral(" error=") << q.lastError().text();
            return false;
        }
    }
    return true;
}

QStringList eventUidsFromArray(const QJsonArray& arr)
{
    QStringList result;
    for (const auto& value : arr) {
        const QString uid = value.toObject().value(QStringLiteral("event_uid")).toString().trimmed();
        if (!uid.isEmpty()) result.append(uid);
    }
    return result;
}

bool acknowledgeAppliedInboxEvents(QString* errorText)
{
    QSqlDatabase db = Database::db();
    QString localError;
    const auto events = CloudSyncInboxService::unackedAppliedEvents(db, 100, &localError);
    if (!localError.isEmpty()) {
        if (errorText) *errorText = localError;
        return false;
    }
    for (const auto& ev : events) {
        const HttpResult result = runJsonRequest(QStringLiteral("POST"), QStringLiteral("/api/v1/sync/ack"), [db, &ev]() {
            QJsonObject body;
            body.insert(QStringLiteral("device_uid"), DeviceIdentityService::deviceUid(db));
            body.insert(QStringLiteral("event_uid"), ev.eventUid);
            body.insert(QStringLiteral("acked_at"), utcNow());
            return body;
        }());
        if (!result.error.isEmpty()) {
            if (errorText) *errorText = result.error;
            CloudSyncSettings::setLastError(result.error);
            return false;
        }
        if (!CloudSyncInboxService::markAcknowledged(db, ev.eventUid, &localError)) {
            if (errorText) *errorText = localError;
            CloudSyncSettings::setLastError(localError);
            return false;
        }
    }
    return true;
}

} // namespace

namespace CloudSyncClient {

bool testConnection(QString* errorText)
{
    const HttpResult result = runJsonRequest(QStringLiteral("GET"), QStringLiteral("/api/v1/health"));
    if (!okOrStoreError(result, errorText)) return false;
    CloudSyncSettings::setLastError(QString());
    return true;
}

bool healthVersionReadinessCheck(QString* errorText)
{
    const HttpResult result = runJsonRequest(QStringLiteral("GET"), QStringLiteral("/api/v1/ready"));
    if (!okOrStoreError(result, errorText)) return false;
    const QJsonObject payload = QJsonDocument::fromJson(result.body).object();
    if (!payload.value(QStringLiteral("ok")).toBool()) {
        const QString message = QString::fromUtf8("Сервер не готовий: %1").arg(payload.value(QStringLiteral("database")).toString());
        CloudSyncSettings::setLastError(message);
        if (errorText) *errorText = message;
        return false;
    }
    CloudSyncSettings::setLastError(QString());
    return true;
}

QJsonObject prepareUploadOutboxBatchContract(QSqlDatabase db, QString* errorText)
{
    db = effectiveDb(db);
    QString localError;
    const QJsonArray events = pendingOutboxEvents(db, &localError);
    if (!localError.isEmpty()) {
        if (errorText) *errorText = localError;
        return QJsonObject{};
    }
    const auto settings = CloudSyncSettings::load();
    QJsonObject body;
    body.insert(QStringLiteral("device_uid"), DeviceIdentityService::deviceUid(db));
    body.insert(QStringLiteral("organization_uid"), settings.organizationUid);
    body.insert(QStringLiteral("branch_uid"), settings.branchUid);
    body.insert(QStringLiteral("events"), events);
    body.insert(QStringLiteral("passive_contract_only"), true);
    return body;
}

QJsonObject prepareFetchInboxContract(QSqlDatabase db, QString* errorText)
{
    db = effectiveDb(db);
    if (!DeviceIdentityService::ensureDeviceIdentity(db, errorText)) return QJsonObject{};
    const auto settings = CloudSyncSettings::load();
    QJsonObject query;
    query.insert(QStringLiteral("device_uid"), DeviceIdentityService::deviceUid(db));
    query.insert(QStringLiteral("organization_uid"), settings.organizationUid);
    query.insert(QStringLiteral("branch_uid"), settings.branchUid);
    query.insert(QStringLiteral("since"), settings.lastPullAt);
    query.insert(QStringLiteral("passive_contract_only"), true);
    return query;
}

bool registerDevice(QString* errorText)
{
    QSqlDatabase db = Database::db();
    QString localError;
    if (!DeviceIdentityService::ensureDeviceIdentity(db, &localError)) {
        if (errorText) *errorText = localError;
        return false;
    }
    const HttpResult result = runJsonRequest(QStringLiteral("POST"), QStringLiteral("/api/v1/devices/register"), devicePayload(db));
    if (!okOrStoreError(result, errorText)) return false;
    const QString deviceToken = QJsonDocument::fromJson(result.body).object().value(QStringLiteral("access_token")).toString().trimmed();
    if (!deviceToken.isEmpty()) {
        auto settings = CloudSyncSettings::load();
        settings.apiToken = deviceToken;
        if (!CloudSyncSettings::save(settings, &localError)) {
            if (errorText) *errorText = localError;
            return false;
        }
    }
    CloudSyncSettings::setLastError(QString());
    return true;
}

bool pushPendingEvents(QString* errorText)
{
    QSqlDatabase db = Database::db();
    QString localError;
    if (!CloudSyncOutboxService::retireLegacyMetadata(db, &localError)
        || !CloudSyncOutboxService::enqueueInitialWorkOrderSnapshots(db, &localError)
        || !CloudSyncOutboxService::enqueuePurchasePaymentSnapshots(db, &localError)) {
        if (errorText) *errorText = localError;
        return false;
    }
    const QJsonArray events = pendingOutboxEvents(db, &localError);
    if (!localError.isEmpty()) {
        if (errorText) *errorText = localError;
        return false;
    }
    if (events.isEmpty()) return true;

    const QStringList uids = eventUidsFromArray(events);
    if (!markOutboxStatus(db, uids, QStringLiteral("sending"))) {
        const QString message = QString::fromUtf8("Не вдалося зберегти стан черги. Відправлення не розпочато.");
        CloudSyncSettings::setLastError(message);
        if (errorText) *errorText = message;
        return false;
    }

    const auto settings = CloudSyncSettings::load();
    QJsonObject body;
    body.insert(QStringLiteral("device_uid"), DeviceIdentityService::deviceUid(db));
    body.insert(QStringLiteral("organization_uid"), settings.organizationUid);
    body.insert(QStringLiteral("branch_uid"), settings.branchUid);
    body.insert(QStringLiteral("events"), events);

    QStringList accepted;
    QStringList failedRequests;
    QString requestError;
    SyncBatchIsolation::deliver(events, [&](const QJsonArray& batch) {
        body.insert(QStringLiteral("events"), batch);
        const HttpResult result = runJsonRequest(QStringLiteral("POST"), QStringLiteral("/api/v1/sync/push"), body);
        requestError = result.error;
        if (!result.error.isEmpty()) return result.status;
        const auto batchUids = eventUidsFromArray(batch);
        const auto response = QJsonDocument::fromJson(result.body).object();
        for (const auto& value : response.value(QStringLiteral("accepted_event_uids")).toArray()) {
            if (batchUids.contains(value.toString()) && !accepted.contains(value.toString())) accepted.append(value.toString());
        }
        return result.status;
    }, [&](const QJsonArray& batch) {
        const auto batchUids = eventUidsFromArray(batch);
        failedRequests.append(batchUids);
        markOutboxStatus(db, batchUids, QStringLiteral("failed"), requestError);
        if (localError.isEmpty()) localError = requestError;
    });
    QStringList rejected = uids;
    for (const QString& uid : accepted) rejected.removeAll(uid);
    QStringList unconfirmed = rejected;
    for (const auto& uid : failedRequests) unconfirmed.removeAll(uid);
    if (!unconfirmed.isEmpty()) markOutboxStatus(db, unconfirmed, QStringLiteral("failed"),
        QString::fromUtf8("Сервер не підтвердив зміни: перевірте конфлікти синхронізації."));
    if (!accepted.isEmpty() && !markOutboxStatus(db, accepted, QStringLiteral("acked"))) {
        const QString message = QString::fromUtf8("Не вдалося підтвердити локальну чергу після успішної відправки.");
        CloudSyncSettings::setLastError(message);
        if (errorText) *errorText = message;
        return false;
    }
    if (!accepted.isEmpty()) CloudSyncSettings::setLastPushAt(utcNow());
    if (!rejected.isEmpty()) {
        const QString message = QString::fromUtf8("Сервер не підтвердив частину змін. Перевірте конфлікти синхронізації.");
        if (errorText) *errorText = message;
        CloudSyncSettings::setLastError(message);
        return false;
    }
    CloudSyncSettings::setLastError(QString());
    return true;
}

bool pullRemoteEvents(QString* errorText)
{
    const auto settings = CloudSyncSettings::load();
    QUrl url = endpointUrl(settings, QStringLiteral("/api/v1/sync/pull"));
    QUrlQuery query(url);
    QSqlDatabase db = Database::db();
    query.addQueryItem(QStringLiteral("device_uid"), DeviceIdentityService::deviceUid(db));
    if (!settings.organizationUid.trimmed().isEmpty()) query.addQueryItem(QStringLiteral("organization_uid"), settings.organizationUid.trimmed());
    if (!settings.branchUid.trimmed().isEmpty()) query.addQueryItem(QStringLiteral("branch_uid"), settings.branchUid.trimmed());
    if (!settings.lastPullAt.isEmpty()) query.addQueryItem(QStringLiteral("since"), settings.lastPullAt);
    url.setQuery(query);

    // runJsonRequest accepts a path, so pass the path+query explicitly.
    const HttpResult result = runJsonRequest(QStringLiteral("GET"), url.path() + QStringLiteral("?") + url.query());
    if (!okOrStoreError(result, errorText)) return false;

    const QJsonDocument doc = QJsonDocument::fromJson(result.body);
    const QJsonObject root = doc.object();
    const QJsonArray events = root.value(QStringLiteral("events")).toArray();
    QString localError;
    for (const auto& value : events) {
        const QJsonObject event = value.toObject();
        if (!event.isEmpty() && !CloudSyncInboxService::enqueueEvent(db, event, &localError)) {
            if (errorText) *errorText = localError;
            CloudSyncSettings::setLastError(localError);
            return false;
        }
    }
    const QString serverWatermark = root.value(QStringLiteral("server_time")).toString().trimmed();
    CloudSyncSettings::setLastPullAt(serverWatermark.isEmpty() ? utcNow() : serverWatermark);
    CloudSyncSettings::setLastError(QString());
    return true;
}

bool acknowledgeEvent(const QString& eventUid, QString* errorText)
{
    QSqlDatabase db = Database::db();
    QJsonObject body;
    body.insert(QStringLiteral("device_uid"), DeviceIdentityService::deviceUid(db));
    body.insert(QStringLiteral("event_uid"), eventUid.trimmed());
    body.insert(QStringLiteral("acked_at"), utcNow());
    const HttpResult result = runJsonRequest(QStringLiteral("POST"), QStringLiteral("/api/v1/sync/ack"), body);
    if (!okOrStoreError(result, errorText)) return false;
    return CloudSyncInboxService::markAcknowledged(db, eventUid, errorText);
}

QJsonObject requestRepairAdvisorSuggestions(const QString& orderUid, const QJsonArray& findings, QString* errorText)
{
    if (orderUid.trimmed().isEmpty() || findings.isEmpty()) {
        const QString message = QString::fromUtf8("Для ШІ-підбору потрібні номер наряду, вузол і агрегат.");
        if (errorText) *errorText = message;
        return {};
    }
    QJsonObject body;
    body.insert(QStringLiteral("findings"), findings);
    const QString path = QStringLiteral("/api/v1/mobile/orders/%1/diagnostics/suggestions")
                             .arg(QString::fromLatin1(QUrl::toPercentEncoding(orderUid.trimmed())));
    const HttpResult result = runJsonRequest(QStringLiteral("POST"), path, body);
    if (!okOrStoreError(result, errorText)) return {};
    const QJsonDocument document = QJsonDocument::fromJson(result.body);
    if (!document.isObject()) {
        const QString message = QString::fromUtf8("Сервер повернув некоректну відповідь ШІ-підбору.");
        CloudSyncSettings::setLastError(message);
        if (errorText) *errorText = message;
        return {};
    }
    CloudSyncSettings::setLastError(QString());
    return document.object();
}

bool recordRepairAdvisorFeedback(const QString& orderUid, const QJsonArray& feedback, QString* errorText)
{
    if (orderUid.trimmed().isEmpty() || feedback.isEmpty()) {
        const QString message = QString::fromUtf8("Немає підтверджених або відхилених пропозицій для навчання ШІ.");
        if (errorText) *errorText = message;
        return false;
    }
    QJsonObject body;
    body.insert(QStringLiteral("suggestion_feedback"), feedback);
    const QString path = QStringLiteral("/api/v1/mobile/orders/%1/diagnostics/feedback")
                             .arg(QString::fromLatin1(QUrl::toPercentEncoding(orderUid.trimmed())));
    const HttpResult result = runJsonRequest(QStringLiteral("POST"), path, body);
    if (!okOrStoreError(result, errorText)) return false;
    CloudSyncSettings::setLastError(QString());
    return true;
}
QJsonObject listPartRequestsInWorker(QString* errorText)
{
    const auto cleanup = qScopeGuard([] { Database::close(); });
    const auto result = listPartRequests(errorText);
    return result;
}

bool orderPartRequestInWorker(const QString& requestUid, QString* errorText)
{
    const auto cleanup = qScopeGuard([] { Database::close(); });
    const bool decided = decidePartRequest(requestUid, QStringLiteral("order"), QString(), errorText);
    const bool synced = decided && syncNow(errorText);
    return synced;
}

QJsonObject listPartRequests(QString* errorText)
{
    const HttpResult result = runJsonRequest(QStringLiteral("GET"), QStringLiteral("/api/v1/mobile/part-requests"));
    if (!okOrStoreError(result, errorText)) return {};
    const QJsonDocument document = QJsonDocument::fromJson(result.body);
    if (!document.isObject()) {
        const QString message = QString::fromUtf8("Сервер повернув некоректний список заявок на запчастини.");
        CloudSyncSettings::setLastError(message);
        if (errorText) *errorText = message;
        return {};
    }
    CloudSyncSettings::setLastError(QString());
    return document.object();
}

bool decidePartRequest(const QString& requestUid, const QString& decision, const QString& comment, QString* errorText)
{
    const QString uid = requestUid.trimmed();
    const QString normalizedDecision = decision.trimmed().toLower();
    if (uid.isEmpty() || normalizedDecision.isEmpty()) {
        const QString message = QString::fromUtf8("Не вказано заявку або рішення для її обробки.");
        if (errorText) *errorText = message;
        return false;
    }
    QJsonObject body;
    body.insert(QStringLiteral("decision"), normalizedDecision);
    if (!comment.trimmed().isEmpty()) body.insert(QStringLiteral("comment"), comment.trimmed());
    const QString path = QStringLiteral("/api/v1/mobile/part-requests/%1/decision")
                             .arg(QString::fromLatin1(QUrl::toPercentEncoding(uid)));
    const HttpResult result = runJsonRequest(QStringLiteral("POST"), path, body);
    if (!okOrStoreError(result, errorText)) return false;
    CloudSyncSettings::setLastError(QString());
    return true;
}

bool syncNow(QString* errorText, int* appliedEvents)

{
    if (appliedEvents) *appliedEvents = 0;
    if (syncCancelled(errorText)) return false;
    QString err;
    if (!Database::ensureConnectionAlive(&err)) {
        if (errorText) *errorText = err;
        CloudSyncSettings::setLastError(err);
        return false;
    }
    if (!registerDevice(&err)) { if (errorText) *errorText = err; return false; }
    if (syncCancelled(errorText)) return false;
    QString pushError;
    const bool pushed = pushPendingEvents(&pushError);
    if (syncCancelled(errorText)) return false;
    const bool ok = pullRemoteEvents(&err);
    if (syncCancelled(errorText)) return false;
    if (!ok) {
        if (errorText) *errorText = err;
        CloudSyncSettings::setLastError(err);
        return false;
    }
    RemoteMutationApplyService::ApplyStats stats = RemoteMutationApplyService::applyPendingEvents(Database::db(), 100, &err);
    if (appliedEvents) *appliedEvents = stats.applied;
    const int pendingConflicts = ConflictResolverService::pendingCount(Database::db());
    if (stats.failed || stats.conflicts || pendingConflicts > 0) {
        const QString details = err;
        err = QString::fromUtf8("Синхронізацію виконано частково. Застосовано змін: %1. "
                               "Помилок обробки: %2. Невирішених конфліктів: %3.\n"
                               "Конфліктні дані не перезаписано. Перевірте розділ «Конфлікти синхронізації».")
                  .arg(stats.applied).arg(stats.failed).arg(qMax(stats.conflicts, pendingConflicts));
        if (!details.isEmpty()) err += QLatin1Char('\n') + details;
    }
    if (!pushed && !pushError.isEmpty()) err = pushError + QStringLiteral("\n") + err;
    QString ackError;
    if (syncCancelled(errorText)) return false;
    if (!acknowledgeAppliedInboxEvents(&ackError)) err += QStringLiteral("\n") + ackError;
    if (!err.isEmpty()) {
        if (errorText) *errorText = err;
        CloudSyncSettings::setLastError(err);
        return false;
    }
    if (!pullRemoteAuditEvents(&err)) {
        if (errorText) *errorText = err;
        return false;
    }
    if (!pushed) { if (errorText) *errorText = pushError; CloudSyncSettings::setLastError(pushError); return false; }
    if (!CloudSyncSettings::setLastSuccessfulSyncAt(utcNow(), &err)) {
        if (errorText) *errorText = err;
        CloudSyncSettings::setLastError(err);
        return false;
    }
    CloudSyncSettings::setLastError(QString());
    return true;
}

} // namespace CloudSyncClient
