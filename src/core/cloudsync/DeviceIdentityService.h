#pragma once

#include <QSqlDatabase>
#include <QString>

namespace DeviceIdentityService {

QString deviceUid(QSqlDatabase db = QSqlDatabase());
QString deviceName(QSqlDatabase db = QSqlDatabase());
QString branchUid(QSqlDatabase db = QSqlDatabase());

bool ensureDeviceIdentity(QSqlDatabase db, QString* errorText = nullptr);
bool updateDeviceName(QSqlDatabase db, const QString& name, QString* errorText = nullptr);
bool updateBranchUid(QSqlDatabase db, const QString& branchUid, QString* errorText = nullptr);
bool regenerateDeviceUid(QSqlDatabase db, QString* newDeviceUid = nullptr, QString* errorText = nullptr);

} // namespace DeviceIdentityService
