#include "OutsourceJobService.h"

#include "../LegacyAncillaryMutationLogSupport.h"
#include "infrastructure/db/sqlite/Db.h"

#include <QSqlError>
#include <QSqlQuery>

namespace {
QString outsourceCompanyDisplayName_service(const QString& companyName)
{
    const QString company = companyName.trimmed();
    if (company.isEmpty()) return QString::fromUtf8("Аутсорс");
    return QString::fromUtf8("Аутсорс \"%1\"").arg(company);
}

bool ensureValidOpenDb_outsource(QSqlDatabase& db, QString* errorText)
{
    if (!db.isValid() || !db.isOpen()) db = Database::db();
    if (!db.isValid()) {
        if (errorText) *errorText = QString::fromUtf8("БД недоступна для збереження аутсорсу");
        return false;
    }
    if (!db.isOpen() && !db.open()) {
        if (errorText) *errorText = db.lastError().text();
        return false;
    }
    return true;
}
}

QHash<int, QString> OutsourceJobService::loadServiceDisplayNames(QSqlDatabase db, const QString& orderUid)
{
    QHash<int, QString> out;
    if (orderUid.trimmed().isEmpty()) return out;
    if (!ensureValidOpenDb_outsource(db, nullptr)) return out;
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT service_id, COALESCE(company_name,'') FROM outsource_jobs WHERE order_uid=? ORDER BY id ASC"));
    q.addBindValue(orderUid);
    if (!q.exec()) return out;
    while (q.next()) {
        out.insert(q.value(0).toInt(), outsourceCompanyDisplayName_service(q.value(1).toString()));
    }
    return out;
}


bool OutsourceJobService::saveJob(const QString& orderUid,
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
                                  QString* errorText)
{
    QSqlDatabase db = Database::db();
    return saveJob(db, orderUid, orderNumber, serviceId, companyId, companyName, sentDate, returnDate, partName, companyPrice, markupPercent, clientPrice, note, errorText);
}

bool OutsourceJobService::saveJob(QSqlDatabase db,
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
                                  QString* errorText)
{
    if (orderUid.trimmed().isEmpty()) {
        if (errorText) *errorText = QString::fromUtf8("Не вдалося визначити order_uid для аутсорсу");
        return false;
    }
    if (!ensureValidOpenDb_outsource(db, errorText)) return false;

    const QString beforeJson = LegacyAncillaryMutationLogSupport::snapshotOutsourceService(db, serviceId);

    QSqlQuery del(db);
    del.prepare(QStringLiteral("DELETE FROM outsource_jobs WHERE service_id=?"));
    del.addBindValue(serviceId);
    if (!del.exec()) {
        if (errorText) *errorText = del.lastError().text();
        return false;
    }

    QSqlQuery ins(db);
    ins.prepare(QStringLiteral(
        "INSERT INTO outsource_jobs(order_uid, order_number, service_id, company_id, company_name, sent_date, return_date, part_name, company_price, markup_percent, client_price, note) VALUES(?,?,?,?,?,?,?,?,?,?,?,?)"));
    ins.addBindValue(orderUid);
    ins.addBindValue(orderNumber);
    ins.addBindValue(serviceId);
    ins.addBindValue(companyId);
    ins.addBindValue(companyName);
    ins.addBindValue(sentDate.toString(Qt::ISODate));
    ins.addBindValue(returnDate.isValid() ? returnDate.toString(Qt::ISODate) : QStringLiteral(""));
    ins.addBindValue(partName);
    ins.addBindValue(companyPrice);
    ins.addBindValue(markupPercent);
    ins.addBindValue(clientPrice);
    ins.addBindValue(note);
    if (!ins.exec()) {
        if (errorText) *errorText = ins.lastError().text();
        return false;
    }

    QString logError;
    LegacyAncillaryMutationLogSupport::logOutsourceMutation(
        db,
        beforeJson == QStringLiteral("{}") ? QStringLiteral("insert") : QStringLiteral("update"),
        orderNumber,
        serviceId,
        beforeJson,
        LegacyAncillaryMutationLogSupport::snapshotOutsourceService(db, serviceId),
        &logError);
    return true;
}
