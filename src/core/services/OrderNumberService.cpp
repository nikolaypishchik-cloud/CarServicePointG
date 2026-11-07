#include "core/services/OrderNumberService.h"

#include "infrastructure/db/sqlite/Db.h"
#include "infrastructure/db/sqlite/ScopedConnectionManager.h"
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
