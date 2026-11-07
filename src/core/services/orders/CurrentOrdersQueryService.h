#pragma once

#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVector>

struct CurrentOrdersQueryResult
{
    QStringList headers;
    QVector<QVector<QVariant>> rows;
    QString sourceTable;
    bool hasSource = false;
    QString errorText;
};


class CurrentOrdersQueryService
{
public:
    static CurrentOrdersQueryResult loadOpenOrders(const QString& filter);
};
