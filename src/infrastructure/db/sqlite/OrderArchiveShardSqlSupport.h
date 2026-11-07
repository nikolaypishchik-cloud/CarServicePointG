#pragma once

#include <QSqlDatabase>
#include <QString>

namespace OrderArchiveShardSqlSupport {

bool copyCompleteOrderGraph(QSqlDatabase database, int orderNumber, QString* errorText = nullptr);

// Requires an attached archive_rollover database; never writes data.
bool verifyCopiedOrderGraph(QSqlDatabase database, int orderNumber, QString* errorText = nullptr);

bool archiveSingleActiveOrderToShard(QSqlDatabase database, int orderNumber, QString* errorText = nullptr);
bool pruneActiveClosedOrdersToArchive(QSqlDatabase database, QString* errorText = nullptr);
bool purgeHydratedArchiveCopiesFromActive(QSqlDatabase database, QString* errorText = nullptr);

} // namespace OrderArchiveShardSqlSupport
