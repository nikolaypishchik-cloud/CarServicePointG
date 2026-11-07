#pragma once

#include <QString>
#include <QStringList>

namespace SnapshotCacheInvalidator {

void invalidateAll(const QString& reason = QString());
void invalidateAfterMutation(const QStringList& changedTables, const QString& reason = QString());
void invalidateOrdersAndParts(const QString& reason = QString());
void invalidateReportsAndStats(const QString& reason = QString());

} // namespace SnapshotCacheInvalidator
