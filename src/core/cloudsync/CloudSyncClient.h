#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QString>

namespace CloudSyncClient {

bool testConnection(QString* errorText = nullptr);
bool healthVersionReadinessCheck(QString* errorText = nullptr);
QJsonObject requestRepairAdvisorSuggestions(const QString& orderUid, const QJsonArray& findings, QString* errorText = nullptr);
bool recordRepairAdvisorFeedback(const QString& orderUid, const QJsonArray& feedback, QString* errorText = nullptr);
QJsonObject listPartRequests(QString* errorText = nullptr);
// Worker entry points release thread-local persistence resources on all exits.
QJsonObject listPartRequestsInWorker(QString* errorText = nullptr);
bool orderPartRequestInWorker(const QString& requestUid, QString* errorText = nullptr);
bool decidePartRequest(const QString& requestUid, const QString& decision, const QString& comment = QString(), QString* errorText = nullptr);
QJsonObject prepareUploadOutboxBatchContract(QSqlDatabase db = QSqlDatabase(), QString* errorText = nullptr);
QJsonObject prepareFetchInboxContract(QSqlDatabase db = QSqlDatabase(), QString* errorText = nullptr);
bool registerDevice(QString* errorText = nullptr);
bool pushPendingEvents(QString* errorText = nullptr);
bool pullRemoteEvents(QString* errorText = nullptr);
bool acknowledgeEvent(const QString& eventUid, QString* errorText = nullptr);
bool syncNow(QString* errorText = nullptr, int* appliedEvents = nullptr);

} // namespace CloudSyncClient
