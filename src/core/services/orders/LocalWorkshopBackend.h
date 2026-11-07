#pragma once
#include <QJsonObject>
#include <QSqlDatabase>

// Native SQLite authority for standalone desktops. No network or subprocesses.
namespace LocalWorkshopBackend {
struct Reply { int status = 500; QJsonObject body; bool ok = false; };
Reply request(QSqlDatabase db, const QString& endpoint, const QJsonObject& payload = {});
bool hasChanges(QSqlDatabase db);
QString deletionBlockReason(QSqlDatabase db, const QString& orderUid);
}
