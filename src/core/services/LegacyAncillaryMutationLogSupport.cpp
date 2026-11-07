#include "core/services/LegacyAncillaryMutationLogSupport.h"

#include "core/services/LegacyMutationLogSupport.h"
#include "core/util/DateFormats.h"

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

QString snapshotExpensePeriod(QSqlDatabase database, const QString& periodFrom, const QString& periodTo, QString* errorText)
{
    QSqlQuery q(database);
    q.prepare(QStringLiteral("SELECT * FROM expenses WHERE %1=? AND %2=? LIMIT 2")
                  .arg(DateFormats::isoSql("period_from"), DateFormats::isoSql("period_to")));
    q.addBindValue(periodFrom);
    q.addBindValue(periodTo);
    if (q.exec()) {
        const auto row = jsonForSingleRow(q);
        if (!q.lastError().isValid()) {
            if (row == "{}") return row;
            if (q.next()) {
                if (errorText) *errorText = QStringLiteral("Кілька записів витрат за один період");
                return {};
            }
            if (!q.lastError().isValid()) return row;
        }
    }
    if (errorText) *errorText = q.lastError().text();
    return {};
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
