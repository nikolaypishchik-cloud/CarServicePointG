#pragma once

#include <QDate>
#include <QDateTime>
#include <QString>
#include <QVector>

class QSqlDatabase;
struct MutationResult;

struct ActivityAuditActor
{
    QString accountId;
    QString displayName;
    QString role;
    QString sessionId;

    bool isValid() const { return !accountId.trimmed().isEmpty(); }
};

struct ActivityAuditEvent
{
    qint64 id = 0;
    QDateTime occurredAt;
    ActivityAuditActor actor;
    QString category;
    QString action;
    QString title;
    int orderNumber = 0;
    QString entityType;
    QString entityId;
    QString subject;
    QString payloadJson;
    bool legacy = false;
};

struct ActivityAuditFilter
{
    QDate from;
    QDate to;
    QString actorId;
    QString category;
    QString search;
    int limit = 1000;
    bool includeLegacy = true;
    bool includeSystem = true;
};

namespace ActivityAuditService {

bool ensureSchema(QSqlDatabase database, QString* errorText = nullptr);

void setCurrentActor(const ActivityAuditActor& actor);
ActivityAuditActor currentActor();
void clearCurrentActor();

bool record(QSqlDatabase database,
            const QString& category,
            const QString& action,
            const QString& title,
            int orderNumber = 0,
            const QString& entityType = {},
            const QString& entityId = {},
            const QString& subject = {},
            const QString& payloadJson = QStringLiteral("{}"),
            QString* errorText = nullptr);

bool recordExternal(QSqlDatabase database,
                    const QString& externalUid,
                    const QDateTime& occurredAt,
                    const ActivityAuditActor& actor,
                    const QString& category,
                    const QString& action,
                    const QString& title,
                    int orderNumber = 0,
                    const QString& entityType = {},
                    const QString& entityId = {},
                    const QString& subject = {},
                    const QString& payloadJson = QStringLiteral("{}"),
                    QString* errorText = nullptr);

bool recordMutation(QSqlDatabase database, const MutationResult& result);
bool recordMutation(QSqlDatabase database, const MutationResult& result, QString* errorText);
QVector<ActivityAuditEvent> load(QSqlDatabase database, const ActivityAuditFilter& filter, QString* errorText = nullptr);

} // namespace ActivityAuditService
