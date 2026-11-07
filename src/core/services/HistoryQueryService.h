#pragma once

#include <QDate>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVector>
#include <QSqlDatabase>

struct HistoryQueryParams
{
    QDate from;
    QDate to;
    QString filter;
    bool includeActive = false;
    bool useSearchIndex = false;
    bool includeMobileState = false;
    int pageSize = 0;
    int offset = 0;
};

struct HistoryQueryResult
{
    QStringList headers;
    QVector<QVector<QVariant>> rows;
    QString errorText;
    bool hasMore = false;
};

namespace HistoryQueryService {

QString buildHistorySql(const HistoryQueryParams& params);
QString buildHistorySql(QSqlDatabase database, const HistoryQueryParams& params);
HistoryQueryResult loadHistory(const HistoryQueryParams& params);

} // namespace HistoryQueryService
