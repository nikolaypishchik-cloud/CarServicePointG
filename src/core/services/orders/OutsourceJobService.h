#pragma once

#include <QDate>
#include <QHash>
#include <QSqlDatabase>
#include <QString>

class OutsourceJobService
{
public:
    static QHash<int, QString> loadServiceDisplayNames(QSqlDatabase db, const QString& orderUid);

    static bool saveJob(const QString& orderUid,
                        int orderNumber,
                        int serviceId,
                        const QString& companyId,
                        const QString& companyName,
                        const QDate& sentDate,
                        const QDate& returnDate,
                        const QString& partName,
                        double companyPrice,
                        double markupPercent,
                        double clientPrice,
                        const QString& note,
                        QString* errorText);

    static bool saveJob(QSqlDatabase db,
                        const QString& orderUid,
                        int orderNumber,
                        int serviceId,
                        const QString& companyId,
                        const QString& companyName,
                        const QDate& sentDate,
                        const QDate& returnDate,
                        const QString& partName,
                        double companyPrice,
                        double markupPercent,
                        double clientPrice,
                        const QString& note,
                        QString* errorText);
};
