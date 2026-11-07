#include "core/services/LegacyAncillaryMutationLogSupport.h"

#include "core/services/LegacyMutationLogSupport.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QJsonDocument>
#include <QJsonObject>

namespace {
QString jsonForSingleRow(QSqlQuery& q)
{
    if (!q.next()) return QStringLiteral("{}");
    QJsonObject obj;
    const QSqlRecord rec = q.record();
    for (int i = 0; i < rec.count(); ++i) {
        obj.insert(rec.fieldName(i), QJsonValue::fromVariant(q.value(i)));
    }
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}
}

namespace LegacyAncillaryMutationLogSupport {

QString snapshotCatalogItem(QSqlDatabase database, int catalogId)
{
    if (catalogId <= 0) return QStringLiteral("{}");
    QSqlQuery q(database);
    q.prepare(QStringLiteral("SELECT * FROM parts_catalog WHERE id=? LIMIT 1"));
    q.addBindValue(catalogId);
    if (!q.exec()) return QStringLiteral("{}");
    return jsonForSingleRow(q);
}

QString snapshotExpensePeriod(QSqlDatabase database, const QString& periodFrom, const QString& periodTo)
{
    if (periodFrom.trimmed().isEmpty() || periodTo.trimmed().isEmpty()) return QStringLiteral("{}");
    QSqlQuery q(database);
    q.prepare(QStringLiteral("SELECT * FROM expenses WHERE (CASE WHEN substr(period_from,3,1)='.' THEN '20'||substr(period_from,7,2)||'-'||substr(period_from,4,2)||'-'||substr(period_from,1,2) ELSE period_from END)=? AND (CASE WHEN substr(period_to,3,1)='.' THEN '20'||substr(period_to,7,2)||'-'||substr(period_to,4,2)||'-'||substr(period_to,1,2) ELSE period_to END)=? ORDER BY id DESC LIMIT 1"));
    q.addBindValue(periodFrom);
    q.addBindValue(periodTo);
    if (!q.exec()) return QStringLiteral("{}");
    return jsonForSingleRow(q);
}

QString snapshotOutsourceService(QSqlDatabase database, int serviceId)
{
    if (serviceId <= 0) return QStringLiteral("{}");
    QSqlQuery q(database);
    q.prepare(QStringLiteral("SELECT * FROM outsource_jobs WHERE service_id=? ORDER BY id DESC LIMIT 1"));
    q.addBindValue(serviceId);
    if (!q.exec()) return QStringLiteral("{}");
    return jsonForSingleRow(q);
}

bool logCatalogMutation(QSqlDatabase database,
                        const QString& action,
                        int catalogId,
                        const QString& beforeJson,
                        const QString& afterJson,
                        QString* errorText)
{
    return LegacyMutationLogSupport::logMutation(database, QStringLiteral("catalog"), action, 0, QString(), QString::number(catalogId), beforeJson, afterJson, errorText);
}

bool logExpenseMutation(QSqlDatabase database,
                        const QString& action,
                        const QString& periodFrom,
                        const QString& periodTo,
                        const QString& beforeJson,
                        const QString& afterJson,
                        QString* errorText)
{
    return LegacyMutationLogSupport::logMutation(database, QStringLiteral("expenses"), action, 0, QString(), periodFrom + QStringLiteral("|") + periodTo, beforeJson, afterJson, errorText);
}

bool logOutsourceMutation(QSqlDatabase database,
                          const QString& action,
                          int orderNumber,
                          int serviceId,
                          const QString& beforeJson,
                          const QString& afterJson,
                          QString* errorText)
{
    return LegacyMutationLogSupport::logMutation(database,
                                                 QStringLiteral("outsource_job"),
                                                 action,
                                                 orderNumber,
                                                 LegacyMutationLogSupport::orderUidForNumber(database, orderNumber),
                                                 QString::number(serviceId),
                                                 beforeJson,
                                                 afterJson,
                                                 errorText);
}

}
