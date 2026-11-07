#pragma once
#include "core/services/AccessControlService.h"
#include <QDate>
#include <QVariant>
#include <QVector>
#include <atomic>
#include <memory>
namespace StatisticsReadService {
QDate parseBusinessDate(const QString& raw);
struct PartTraceRow
{
    qint64 id = 0;
    int orderNumber = 0;
    int visibleOrderNumber = 0;
    QString orderUid;
    QString name;
    QString partNumber;
    QString orderedAt;
    QDate orderedOn;
    QDate deliveredOn;
    QDate installedOn;
    QString store;
    QString invoiceNumber;
    QString invoiceDate;
    QString invoiceFileName;
    QString car;
    QString client;
    QString responsible;
    int storageDays = 0;
    QString status;
};

struct Snapshot {
    QVariantList staff, orders, stock, events;
    QVector<PartTraceRow> parts;
    QString error;
};
struct Request {
    QDate from,to;bool performance=false,allowed=false;
    AccessControlService::OrderScope scope;
    QString ownMaster,scopeKey,path;
};
Request capture(const QDate& from,const QDate& to,bool performance);
Snapshot run(const Request& request,const std::shared_ptr<std::atomic_bool>& cancel);
Snapshot load(QSqlDatabase db, const QDate& fromDate, const QDate& toDate,
              bool performance, const AccessControlService::OrderScope& scope,
              const QString& ownMaster, const std::shared_ptr<std::atomic_bool>& cancel);
}
