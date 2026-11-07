#pragma once

#include <QSqlDatabase>
#include <QString>
#include <QVariantMap>

namespace WorkOrderSyncApplyService {

bool upsertWorkOrderDirect(QSqlDatabase db, const QString& orderUid, const QVariantMap& fields, QString* errorText = nullptr);
bool upsertServiceDirect(QSqlDatabase db, const QString& serviceUid, const QVariantMap& fields, QString* errorText = nullptr);
bool upsertPartDirect(QSqlDatabase db, const QString& partUid, const QVariantMap& fields, QString* errorText = nullptr);

} // namespace WorkOrderSyncApplyService
