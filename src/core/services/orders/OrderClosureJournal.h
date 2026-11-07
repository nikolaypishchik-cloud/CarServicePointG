#pragma once
#include <QJsonObject>
#include <QJsonArray>
#include <QSqlDatabase>
namespace OrderClosureJournal {
bool ensureSchema(QSqlDatabase db, QString* error);
bool remember(QSqlDatabase db, const QJsonObject& request, QString* error);
QJsonObject pending(QSqlDatabase db, const QString& orderUid, QString* error);
bool reject(QSqlDatabase db, const QString& requestUid, QString* error);
// Status, installed_at, receipts and the applied marker live in the same main DB.
bool apply(QSqlDatabase db, const QJsonObject& result, QString* error);
}
