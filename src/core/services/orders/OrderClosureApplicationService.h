#pragma once
#include <QJsonObject>
#include <QStringList>
#include <QSqlDatabase>
namespace OrderClosureApplicationService {
struct Reply { int status=0; QJsonObject body; bool ok=false; };
QJsonObject readState(QSqlDatabase db,const QString& uid,QString* error);
QJsonObject readState(const QString& uid,QString* error);
QStringList pendingOrders(QSqlDatabase db,QString* error);
QStringList pendingOrders(QString* error);
QString deletionBlockReason(const QString& orderUid);
Reply localRequest(const QString& endpoint,const QJsonObject& payload);
QJsonObject pending(const QString& uid,QString* error);
bool remember(const QJsonObject& command,QString* error);
bool reject(const QString& requestUid,QString* error);
bool refreshRegistry(const QString& uid,QString* error);
bool apply(const QJsonObject& result,QString* error);
}
