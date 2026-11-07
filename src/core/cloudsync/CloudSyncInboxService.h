#pragma once

#include <QJsonObject>
#include <QList>
#include <QSqlDatabase>
#include <QString>

namespace CloudSyncInboxService {

struct InboxEvent {
    int id = 0;
    QString eventUid;
    QString serverEventId;
    QString sourceDeviceUid;
    QString entityType;
    QString entityUid;
    QString operation;
    QString payloadJson;
    QString status;
    QString ackedAt;
};

bool ensureSchema(QSqlDatabase db = QSqlDatabase(), QString* errorText = nullptr);
bool enqueueEvent(QSqlDatabase db, const QJsonObject& event, QString* errorText = nullptr);
QList<InboxEvent> pendingEvents(QSqlDatabase db = QSqlDatabase(), int limit = 50, QString* errorText = nullptr);
bool markStatus(QSqlDatabase db, const QString& eventUid, const QString& status, const QString& error = QString(), QString* errorText = nullptr);
bool recoverInterruptedApplying(QSqlDatabase db = QSqlDatabase(), int olderThanSeconds = 900, QString* errorText = nullptr);
QList<InboxEvent> unackedAppliedEvents(QSqlDatabase db = QSqlDatabase(), int limit = 100, QString* errorText = nullptr);
bool markAcknowledged(QSqlDatabase db, const QString& eventUid, QString* errorText = nullptr);
int pendingCount(QSqlDatabase db = QSqlDatabase(), QString* errorText = nullptr);
int conflictCount(QSqlDatabase db = QSqlDatabase(), QString* errorText = nullptr);

} // namespace CloudSyncInboxService
