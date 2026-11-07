#include "core/services/startup/StartupDatabaseFacade.h"

#include "infrastructure/db/sqlite/Db.h"

bool StartupDatabaseFacade::runStartupMigrationCheck(QString* errorText)
{
    return Database::runStartupMigrationCheck(errorText);
}

bool StartupDatabaseFacade::runStartupArchiveMaintenance(QString* errorText)
{
    return Database::runStartupArchiveMaintenance(errorText);
}

bool StartupDatabaseFacade::prepareRuntime(QString* errorText)
{
    return Database::prepareRuntime(errorText);
}

bool StartupDatabaseFacade::openPrepared(QString* errorText)
{
    return Database::openPrepared(errorText);
}

bool StartupDatabaseFacade::runStartupSchemaCheck(QString* errorText)
{
    return Database::runStartupSchemaCheck(errorText);
}

bool StartupDatabaseFacade::setSetting(QStringView key, const QString& value)
{
    return Database::setSetting(key.toString(), value);
}
