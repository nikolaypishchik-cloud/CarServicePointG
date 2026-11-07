#pragma once

#include <QString>
#include <QStringView>

class StartupDatabaseFacade
{
public:
    static bool runStartupMigrationCheck(QString* errorText);
    static bool runStartupArchiveMaintenance(QString* errorText);
    static bool prepareRuntime(QString* errorText);
    static bool openPrepared(QString* errorText);
    static bool runStartupSchemaCheck(QString* errorText);
    static bool setSetting(QStringView key, const QString& value);
};
