#pragma once

#include "core/services/ActivityAuditService.h"

#include <QString>
#include <QVector>

class QSqlDatabase;

struct StaffAccount
{
    QString id;
    QString masterId;
    QString displayName;
    QString role;
    bool active = true;
    bool pinConfigured = false;
};

namespace StaffAccountService {

bool ensureSchema(QSqlDatabase database, QString* errorText = nullptr);
QVector<StaffAccount> accounts(QSqlDatabase database, QString* errorText = nullptr);
bool setPin(QSqlDatabase database, const QString& accountId, const QString& pin, QString* errorText = nullptr);
bool signIn(QSqlDatabase database, const QString& accountId, const QString& pin, ActivityAuditActor* actor = nullptr, QString* errorText = nullptr);
void closeCurrentSession(QSqlDatabase database, const QString& source = QStringLiteral("shutdown"));

} // namespace StaffAccountService
namespace StaffAccountService {
// UI entry points: connection and transaction ownership remain in the service.
QVector<StaffAccount> accounts(QString* errorText = nullptr);
bool setPin(const QString& accountId, const QString& pin, QString* errorText = nullptr);
bool signIn(const QString& accountId, const QString& pin, ActivityAuditActor* actor = nullptr, QString* errorText = nullptr);
QVector<QPair<QString,QString>> orderingMasters();
}
