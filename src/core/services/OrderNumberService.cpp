#include "core/services/OrderNumberService.h"

#include "infrastructure/db/sqlite/Db.h"
#include "infrastructure/db/sqlite/ScopedConnectionManager.h"
#include "infrastructure/db/sqlite/DbThreadConnectionSupport.h"
#include "core/services/OrderIdentityService.h"
#include "runtime/AppRuntime.h"
#include <QDateTime>
#include <QTimeZone>
#include <QFileInfo>
#include <memory>

int OrderNumberService::nextVisibleOrderNumber()
{
    const QTimeZone zone(QByteArrayLiteral("Europe/Kyiv"));
    if (!zone.isValid()) return 0;
    std::unique_ptr<ScopedSqliteConnection> registry;
    std::function<QSqlDatabase()> resolver;
    if (QFileInfo::exists(AppRuntime::registryDbFilePath())) {
        registry=std::make_unique<ScopedSqliteConnection>(AppRuntime::registryDbFilePath(),QStringLiteral("annual_number_preview"),nullptr,true);
        if (!registry->isOpen()) return 0;
        resolver=[&]{return registry->db();};
    }
    return OrderIdentityService::nextVisibleOrderNumber(Database::db(),resolver,
        QDateTime::currentDateTimeUtc().toTimeZone(zone).date());
}

bool OrderNumberService::allocate(QSqlDatabase db, const QString& requestUid, const QDateTime& createdAt,
                                  int preferredInternalNumber, OrderIdentityService::Allocation* result, QString* error)
{
    std::unique_ptr<ScopedSqliteConnection> registry;
    std::function<QSqlDatabase()> resolver;
    QString openError;
    if (DbThreadConnectionSupport::isRuntimeConnection(db.connectionName())) {
        const auto path = AppRuntime::registryDbFilePath();
        if (QFileInfo::exists(path)) {
            resolver = [&,path] {
                registry = std::make_unique<ScopedSqliteConnection>(path,QStringLiteral("annual_identity_registry"),&openError,true);
                return registry->isOpen() ? registry->db() : QSqlDatabase{};
            };
        }
    }
    const bool ok = OrderIdentityService::allocateAnnualIdentity(db,requestUid,createdAt,preferredInternalNumber,result,error,resolver);
    if (!ok && error && !openError.isEmpty()) *error = openError;
    return ok;
}
