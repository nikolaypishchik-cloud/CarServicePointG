#pragma once

#include <QSqlDatabase>
#include <QString>

namespace EntityUidService {

QString generateUid(const QString& prefix);
bool ensureUidForRow(QSqlDatabase db,
                     const QString& table,
                     const QString& uidColumn,
                     int id,
                     QString* errorText = nullptr);
bool backfillMissingUids(QSqlDatabase db, QString* errorText = nullptr);

} // namespace EntityUidService
