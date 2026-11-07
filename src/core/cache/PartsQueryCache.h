#pragma once

#include <QDate>
#include <QString>
#include <QVector>
#include <QtGlobal>

#include "core/services/PartsQueryService.h"

namespace PartsQueryCache {

quint64 currentRevision();
QString makeRowsKey(const QString& dbPath, const QString& filter, const QDate& from, const QDate& to);
QString makeDateBoundsKey(const QString& dbPath);

bool tryGetRows(const QString& key, QVector<PartRowSnapshot>* out);
void storeRows(const QString& key, const QVector<PartRowSnapshot>& rows);

bool tryGetDateBounds(const QString& key, PartsQueryService::PartArchiveDateBounds* out);
void storeDateBounds(const QString& key, const PartsQueryService::PartArchiveDateBounds& bounds);

void invalidate(const QString& reason = QString());
void bumpRevision(const QString& reason = QString());

} // namespace PartsQueryCache
