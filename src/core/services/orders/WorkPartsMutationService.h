#pragma once

#include <QString>
#include <functional>
#include <QSqlDatabase>

#include "core/services/MutationExecutor.h"

namespace WorkPartsMutationService {

bool setServiceAttachment(int serviceId, const QString& filePath, const QString& displayName, QString* errorText = nullptr);
bool setPartAttachment(int partId, const QString& filePath, const QString& displayName,
                       const QString& invoiceNumber, const QString& invoiceDate,
                       QString* errorText = nullptr, bool confirmedImport = false, double confirmedAmount = -1);
bool updatePartDeliveryDateValue(int partId, const QString& isoDate, const QString& orderUid,
                                 int orderNumber, QString* errorText = nullptr);
bool ensurePartDeliveryDateIfMissing(int partId, const QString& isoDate, QString* errorText = nullptr);
bool removePart(int orderNumber, int partId, QString* errorText = nullptr);
MutationExecutor::Result execute(const MutationExecutor::Request& request,
                                 const std::function<bool(QSqlDatabase&, QString*)>& body);
bool runInActiveTransaction(const QString& connectionName,
                            const std::function<bool(QSqlDatabase&, QString*)>& body,
                            QString* errorText = nullptr);

[[deprecated("Use execute(...) for new DB mutations; use runInActiveTransaction(...) only from an already active WorkPartsMutationService::execute body.")]]
bool withDatabase(const QString& connectionName,
                  const std::function<bool(QSqlDatabase&, QString*)>& body,
                  QString* errorText = nullptr);

} // namespace WorkPartsMutationService
