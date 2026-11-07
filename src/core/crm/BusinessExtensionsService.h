#pragma once

#include <QSqlDatabase>
#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace BusinessExtensionsService {
bool upsertEstimateApproval(QSqlDatabase db, const QVariantMap& payload, QString* approvalUid = nullptr, QString* errorText = nullptr);
bool decideEstimateApproval(QSqlDatabase db, const QString& approvalUid, const QString& status, const QString& comment, QString* errorText = nullptr);
bool upsertSupplierQuote(QSqlDatabase db, const QVariantMap& payload, QString* quoteUid = nullptr, QString* errorText = nullptr);
bool queueCommunication(QSqlDatabase db, const QVariantMap& payload, QString* messageUid = nullptr, QString* errorText = nullptr);
bool recordAuditEvent(QSqlDatabase db, const QString& action, const QString& entityType, const QString& entityUid, const QVariantMap& details = {}, QString* errorText = nullptr);
bool upsertMappedEntity(QSqlDatabase db, const QString& table, const QString& uidColumn, const QString& uid, const QStringList& writableFields, const QVariantMap& payload, QString* errorText = nullptr);
}
