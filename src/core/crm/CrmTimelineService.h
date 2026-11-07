#pragma once

#include <QSqlDatabase>
#include <QString>

namespace CrmTimelineService {

bool addEvent(QSqlDatabase db,
              const QString& clientUid,
              const QString& vehicleUid,
              const QString& orderUid,
              const QString& eventType,
              const QString& title,
              const QString& body,
              const QString& createdBy = QString(),
              QString* eventUid = nullptr,
              QString* errorText = nullptr);

// Direct in-transaction helper for schema/backfill code that already owns a
// transaction or must bypass business mutation gates. UI/business writes must
// use addEvent(), which publishes a sync_outbox mutation after commit.
bool addEventDirect(QSqlDatabase db,
                    const QString& eventUid,
                    const QString& clientUid,
                    const QString& vehicleUid,
                    const QString& orderUid,
                    const QString& eventType,
                    const QString& title,
                    const QString& body,
                    const QString& createdBy = QString(),
                    QString* errorText = nullptr);

} // namespace CrmTimelineService
