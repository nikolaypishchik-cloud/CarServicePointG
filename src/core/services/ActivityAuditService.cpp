#include "core/util/DateFormats.h"
#include "core/services/ActivityAuditService.h"

#include "core/services/MutationResult.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>

#include <algorithm>

namespace {

QMutex& actorMutex()
{
    static QMutex mutex;
    return mutex;
}

ActivityAuditActor& actorStorage()
{
    static ActivityAuditActor actor;
    return actor;
}

void setError(QString* errorText, const QString& value)
{
    if (errorText) *errorText = value;
}

QDateTime parseDateTime(const QString& value)
{
    QDateTime parsed = DateFormats::parseDateTime(value);
    if (parsed.isValid()) return parsed;
    parsed = QDateTime::fromString(value, QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    if (parsed.isValid()) return parsed;
    return QDateTime::fromString(value.left(19), QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}

QString normalizeCategory(const QString& entityType, const QString& operation)
{
    const QString input = (entityType + QLatin1Char(' ') + operation).toLower();
    if (input.contains(QStringLiteral("part"))) return QStringLiteral("parts");
    if (input.contains(QStringLiteral("service")) || input.contains(QStringLiteral("work_order")) || input.contains(QStringLiteral("order"))) {
        return QStringLiteral("work");
    }
    if (input.contains(QStringLiteral("inventory")) || input.contains(QStringLiteral("material"))) return QStringLiteral("inventory");
    if (input.contains(QStringLiteral("master")) || input.contains(QStringLiteral("personnel"))) return QStringLiteral("team");
    return QStringLiteral("system");
}

QString mutationTitle(const MutationResult& result)
{
    if (!result.description.trimmed().isEmpty()) return result.description.trimmed();

    const QString entity = result.entityType.trimmed();
    const QString operation = result.operation.trimmed();
    if (entity == QStringLiteral("work_order") && operation == QStringLiteral("create")) {
        return QString::fromUtf8("Створено наряд");
    }
    if (entity == QStringLiteral("work_order")) return QString::fromUtf8("Змінено наряд");
    if (entity.contains(QStringLiteral("part"), Qt::CaseInsensitive)) return QString::fromUtf8("Змінено запчастину");
    if (entity.contains(QStringLiteral("service"), Qt::CaseInsensitive)) return QString::fromUtf8("Змінено роботу");
    return QString::fromUtf8("Змінено дані СТО");
}

ActivityAuditActor actorForResult(const MutationResult& result)
{
    if (!result.systemMaintenance) return ActivityAuditService::currentActor();
    return {QStringLiteral("system"), QString::fromUtf8("Система"), QStringLiteral("system"), QString()};
}

bool tableExists(QSqlDatabase database, const QString& name)
{
    const QStringList names = database.tables(static_cast<QSql::TableType>(QSql::Tables | QSql::Views));
    return std::any_of(names.cbegin(), names.cend(), [&name](const QString& item) {
        return item.compare(name, Qt::CaseInsensitive) == 0;
    });
}

bool columnExists(QSqlDatabase database, const QString& table, const QString& column)
{
    QSqlQuery query(database);
    if (!query.exec(QStringLiteral("PRAGMA table_info(%1)").arg(table))) return false;
    while (query.next()) {
        if (query.value(1).toString().compare(column, Qt::CaseInsensitive) == 0) return true;
    }
    return false;
}

} // namespace

namespace ActivityAuditService {

bool ensureSchema(QSqlDatabase database, QString* errorText)
{
    if (!database.isValid()) {
        setError(errorText, QString::fromUtf8("Немає з'єднання з базою даних аудиту."));
        return false;
    }
    if (!database.isOpen() && !database.open()) {
        setError(errorText, database.lastError().text());
        return false;
    }

    QSqlQuery query(database);
    if (!query.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS activity_audit_events ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "occurred_at TEXT NOT NULL DEFAULT (datetime('now')),"
            "actor_id TEXT NOT NULL DEFAULT '',"
            "actor_name TEXT NOT NULL DEFAULT 'Система',"
            "actor_role TEXT NOT NULL DEFAULT 'system',"
            "session_id TEXT NOT NULL DEFAULT '',"
            "category TEXT NOT NULL,"
            "action TEXT NOT NULL,"
            "title TEXT NOT NULL,"
            "order_number INTEGER NOT NULL DEFAULT 0,"
            "entity_type TEXT NOT NULL DEFAULT '',"
            "entity_id TEXT NOT NULL DEFAULT '',"
            "subject TEXT NOT NULL DEFAULT '',"
            "payload_json TEXT NOT NULL DEFAULT '{}'"
            ")"))
        || !query.exec(QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_activity_audit_occurred_at "
            "ON activity_audit_events(occurred_at DESC)"))
        || !query.exec(QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_activity_audit_actor "
            "ON activity_audit_events(actor_id, occurred_at DESC)"))
        || !query.exec(QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_activity_audit_order "
            "ON activity_audit_events(order_number, occurred_at DESC)"))) {
        setError(errorText, query.lastError().text());
        return false;
    }
    if (!columnExists(database, QStringLiteral("activity_audit_events"), QStringLiteral("external_uid"))
        && !query.exec(QStringLiteral("ALTER TABLE activity_audit_events ADD COLUMN external_uid TEXT NOT NULL DEFAULT ''"))) {
        setError(errorText, query.lastError().text());
        return false;
    }
    if (!query.exec(QStringLiteral(
            "CREATE UNIQUE INDEX IF NOT EXISTS idx_activity_audit_external_uid "
            "ON activity_audit_events(external_uid) WHERE external_uid <> ''"))) {
        setError(errorText, query.lastError().text());
        return false;
    }
    return true;
}

void setCurrentActor(const ActivityAuditActor& actor)
{
    QMutexLocker locker(&actorMutex());
    actorStorage() = actor;
}

ActivityAuditActor currentActor()
{
    QMutexLocker locker(&actorMutex());
    return actorStorage();
}

void clearCurrentActor()
{
    setCurrentActor({});
}

static bool recordAs(QSqlDatabase database,
            const QString& category,
            const QString& action,
            const QString& title,
            int orderNumber,
            const QString& entityType,
            const QString& entityId,
            const QString& subject,
            const QString& payloadJson,
            QString* errorText,
            const ActivityAuditActor& requestedActor)
{
    if (!ensureSchema(database, errorText)) return false;

    ActivityAuditActor actor = requestedActor;
    if (!actor.isValid()) actor = {QStringLiteral("system"), QString::fromUtf8("Система"), QStringLiteral("system"), QString()};

    QSqlQuery query(database);
    query.prepare(QStringLiteral(
        "INSERT INTO activity_audit_events("
        "actor_id, actor_name, actor_role, session_id, category, action, title, order_number, "
        "entity_type, entity_id, subject, payload_json) VALUES(?,?,?,?,?,?,?,?,?,?,?,?)"));
    query.addBindValue(actor.accountId);
    query.addBindValue(actor.displayName.trimmed().isEmpty() ? QString::fromUtf8("Система") : actor.displayName.trimmed());
    query.addBindValue(actor.role.trimmed().isEmpty() ? QStringLiteral("system") : actor.role.trimmed());
    query.addBindValue(actor.sessionId.isNull() ? QStringLiteral("") : actor.sessionId);
    query.addBindValue(category.trimmed().isEmpty() ? QStringLiteral("system") : category.trimmed());
    query.addBindValue(action.trimmed().isEmpty() ? QStringLiteral("record") : action.trimmed());
    query.addBindValue(title.trimmed().isEmpty() ? QString::fromUtf8("Подія СТО") : title.trimmed());
    query.addBindValue(qMax(0, orderNumber));
    query.addBindValue(entityType.isNull() ? QStringLiteral("") : entityType.trimmed());
    query.addBindValue(entityId.isNull() ? QStringLiteral("") : entityId.trimmed());
    query.addBindValue(subject.isNull() ? QStringLiteral("") : subject.trimmed());
    query.addBindValue(payloadJson.trimmed().isEmpty() ? QStringLiteral("{}") : payloadJson.trimmed());
    if (!query.exec()) {
        setError(errorText, query.lastError().text());
        return false;
    }
    return true;
}

bool record(QSqlDatabase database,
            const QString& category, const QString& action, const QString& title,
            int orderNumber, const QString& entityType, const QString& entityId,
            const QString& subject, const QString& payloadJson, QString* errorText)
{
    return recordAs(database, category, action, title, orderNumber, entityType,
                    entityId, subject, payloadJson, errorText, currentActor());
}

bool recordExternal(QSqlDatabase database,
                    const QString& externalUid,
                    const QDateTime& occurredAt,
                    const ActivityAuditActor& actor,
                    const QString& category,
                    const QString& action,
                    const QString& title,
                    int orderNumber,
                    const QString& entityType,
                    const QString& entityId,
                    const QString& subject,
                    const QString& payloadJson,
                    QString* errorText)
{
    if (externalUid.trimmed().isEmpty()) {
        setError(errorText, QString::fromUtf8("Для зовнішньої події не вказано ідентифікатор аудиту."));
        return false;
    }
    if (!ensureSchema(database, errorText)) return false;

    const QDateTime timestamp = occurredAt.isValid() ? occurredAt : QDateTime::currentDateTimeUtc();
    const ActivityAuditActor eventActor = actor.isValid()
        ? actor
        : ActivityAuditActor{QStringLiteral("system"), QString::fromUtf8("Система"), QStringLiteral("system"), QString()};
    QSqlQuery query(database);
    query.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO activity_audit_events("
        "occurred_at, actor_id, actor_name, actor_role, session_id, category, action, title, order_number, "
        "entity_type, entity_id, subject, payload_json, external_uid) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?)"));
    query.addBindValue(timestamp.toUTC().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
    query.addBindValue(eventActor.accountId);
    query.addBindValue(eventActor.displayName.trimmed().isEmpty() ? QString::fromUtf8("Система") : eventActor.displayName.trimmed());
    query.addBindValue(eventActor.role.trimmed().isEmpty() ? QStringLiteral("system") : eventActor.role.trimmed());
    query.addBindValue(eventActor.sessionId);
    query.addBindValue(category.trimmed().isEmpty() ? QStringLiteral("system") : category.trimmed());
    query.addBindValue(action.trimmed().isEmpty() ? QStringLiteral("record") : action.trimmed());
    query.addBindValue(title.trimmed().isEmpty() ? QString::fromUtf8("Подія СТО") : title.trimmed());
    query.addBindValue(qMax(0, orderNumber));
    query.addBindValue(entityType.isNull() ? QStringLiteral("") : entityType.trimmed());
    query.addBindValue(entityId.isNull() ? QStringLiteral("") : entityId.trimmed());
    query.addBindValue(subject.isNull() ? QStringLiteral("") : subject.trimmed());
    query.addBindValue(payloadJson.trimmed().isEmpty() ? QStringLiteral("{}") : payloadJson.trimmed());
    query.addBindValue(externalUid.trimmed());
    if (!query.exec()) {
        setError(errorText, query.lastError().text());
        return false;
    }
    return true;
}
bool recordMutation(QSqlDatabase database, const MutationResult& result)
{
    return recordMutation(database,result,nullptr);
}

bool recordMutation(QSqlDatabase database, const MutationResult& result, QString* errorText)
{
    if (!result.ok) return true;

    const ActivityAuditActor eventActor = actorForResult(result);

    QJsonObject payload;
    payload.insert(QStringLiteral("operation_id"), result.operationId);
    payload.insert(QStringLiteral("source_module"), result.sourceModule);
    payload.insert(QStringLiteral("operation"), result.operation);
    payload.insert(QStringLiteral("system_maintenance"), result.systemMaintenance);
    const QJsonDocument beforeDocument = QJsonDocument::fromJson(result.auditBeforeJson.toUtf8());
    const QJsonDocument afterDocument = QJsonDocument::fromJson(result.auditAfterJson.toUtf8());
    if (beforeDocument.isObject() && !beforeDocument.object().isEmpty()) {
        payload.insert(QStringLiteral("before"), beforeDocument.object());
    }
    if (afterDocument.isObject() && !afterDocument.object().isEmpty()) {
        payload.insert(QStringLiteral("after"), afterDocument.object());
    }
    QJsonArray tables;
    for (const QString& table : result.changedTables) tables.append(table);
    payload.insert(QStringLiteral("changed_tables"), tables);

    bool parsedOrder = false;
    const int orderNumber = result.entityType == QStringLiteral("work_order")
        ? result.entityId.toInt(&parsedOrder) : 0;
    const bool recorded = recordAs(database,
                                 normalizeCategory(result.entityType, result.operation),
                                 result.operation,
                                 mutationTitle(result),
                                 parsedOrder ? orderNumber : 0,
                                 result.entityType,
                                 result.entityId,
                                 result.description,
                                 QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact)),
                                 errorText, eventActor);
    return recorded;
}

QVector<ActivityAuditEvent> load(QSqlDatabase database, const ActivityAuditFilter& filter, QString* errorText)
{
    QVector<ActivityAuditEvent> rows;
    if (!ensureSchema(database, errorText)) return rows;

    const QDate from = filter.from.isValid() ? filter.from : QDate::currentDate().addDays(-29);
    const QDate to = filter.to.isValid() ? filter.to : QDate::currentDate();
    if (from > to) {
        setError(errorText, QString::fromUtf8("Некоректний період аудиту."));
        return rows;
    }

    // UI dates are local calendar days; persisted audit timestamps are UTC.
    // Convert both boundaries independently to also cover daylight-saving days.
    const QString fromUtc = from.startOfDay().toUTC().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    const QString untilUtc = to.addDays(1).startOfDay().toUTC().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    QStringList where = {QStringLiteral("datetime(occurred_at) >= datetime(?)"), QStringLiteral("datetime(occurred_at) < datetime(?)")};
    QVariantList values = {fromUtc, untilUtc};
    if (!filter.includeSystem) {
        where.append(QStringLiteral("LOWER(TRIM(COALESCE(actor_id,''))) NOT IN ('','system','legacy')"));
        where.append(QStringLiteral("LOWER(TRIM(COALESCE(actor_role,''))) <> 'system'"));
    }
    if (!filter.actorId.trimmed().isEmpty()) {
        where.append(QStringLiteral("actor_id=?"));
        values.append(filter.actorId.trimmed());
    }
    if (!filter.category.trimmed().isEmpty()) {
        where.append(QStringLiteral("category=?"));
        values.append(filter.category.trimmed());
    }
    if (!filter.search.trimmed().isEmpty()) {
        where.append(QStringLiteral("(title LIKE ? OR subject LIKE ? OR CAST(order_number AS TEXT) LIKE ?)"));
        const QString pattern = QStringLiteral("%") + filter.search.trimmed() + QStringLiteral("%");
        values.append(pattern);
        values.append(pattern);
        values.append(pattern);
    }

    QSqlQuery query(database);
    query.prepare(QStringLiteral(
        "SELECT id, occurred_at, actor_id, actor_name, actor_role, session_id, category, action, title, "
        "order_number, entity_type, entity_id, subject, payload_json "
        "FROM activity_audit_events WHERE %1 ORDER BY datetime(occurred_at) DESC, id DESC LIMIT ?")
        .arg(where.join(QStringLiteral(" AND "))));
    for (const QVariant& value : values) query.addBindValue(value);
    query.addBindValue(qBound(1, filter.limit, 5000));
    if (!query.exec()) {
        setError(errorText, query.lastError().text());
        return rows;
    }
    while (query.next()) {
        ActivityAuditEvent event;
        event.id = query.value(0).toLongLong();
        event.occurredAt = parseDateTime(query.value(1).toString());
        event.actor = {query.value(2).toString(), query.value(3).toString(), query.value(4).toString(), query.value(5).toString()};
        event.category = query.value(6).toString();
        event.action = query.value(7).toString();
        event.title = query.value(8).toString();
        event.orderNumber = query.value(9).toInt();
        event.entityType = query.value(10).toString();
        event.entityId = query.value(11).toString();
        event.subject = query.value(12).toString();
        event.payloadJson = query.value(13).toString();
        rows.append(event);
    }

    if (!filter.includeSystem || !filter.includeLegacy || !tableExists(database, QStringLiteral("legacy_mutation_log"))) return rows;
    if (!filter.actorId.trimmed().isEmpty() || (!filter.category.trimmed().isEmpty() && filter.category != QStringLiteral("history"))) {
        return rows;
    }

    QSqlQuery legacy(database);
    legacy.prepare(QStringLiteral(
        "SELECT id, created_at, entity_name, action, order_number, record_key, after_json "
        "FROM legacy_mutation_log WHERE datetime(created_at) >= datetime(?) AND datetime(created_at) < datetime(?) "
        "ORDER BY datetime(created_at) DESC, id DESC LIMIT ?"));
    legacy.addBindValue(fromUtc);
    legacy.addBindValue(untilUtc);
    legacy.addBindValue(qBound(1, filter.limit, 5000));
    if (!legacy.exec()) {
        setError(errorText, legacy.lastError().text());
        return rows;
    }
    while (legacy.next()) {
        ActivityAuditEvent event;
        event.id = -legacy.value(0).toLongLong();
        event.occurredAt = parseDateTime(legacy.value(1).toString());
        event.actor = {QStringLiteral("legacy"), QString::fromUtf8("Система (історія)"), QStringLiteral("system"), QString()};
        event.category = QStringLiteral("history");
        event.action = legacy.value(3).toString();
        event.title = QString::fromUtf8("Історична зміна: %1").arg(event.action);
        event.orderNumber = legacy.value(4).toInt();
        event.entityType = legacy.value(2).toString();
        event.entityId = legacy.value(5).toString();
        event.subject = event.entityType;
        event.payloadJson = legacy.value(6).toString();
        event.legacy = true;
        if (!filter.search.trimmed().isEmpty()) {
            const QString probe = event.title + QLatin1Char(' ') + event.subject + QLatin1Char(' ') + QString::number(event.orderNumber);
            if (!probe.contains(filter.search, Qt::CaseInsensitive)) continue;
        }
        rows.append(event);
    }

    std::sort(rows.begin(), rows.end(), [](const ActivityAuditEvent& left, const ActivityAuditEvent& right) {
        return left.occurredAt > right.occurredAt;
    });
    if (rows.size() > filter.limit) rows.resize(filter.limit);
    return rows;
}

} // namespace ActivityAuditService
