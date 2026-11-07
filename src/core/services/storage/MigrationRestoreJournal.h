#pragma once

#include <QString>
#include <QMap>

class MigrationRestoreJournal {
public:
    static bool beginForSnapshot(const QString& snapshotDirPath, QString* errorText);
    static bool begin(const QString& snapshotDirPath, const QString& manifestHash, QString* errorText);
    // Empty path means no interrupted restore. Invalid intent is an error.
    static bool pending(QString* snapshotDirPath, QString* errorText);
    static bool finish(QString* errorText);
};

QString migrationSnapshotsRootPath();

bool restoreMigrationSnapshotFromDirectory(const QString& snapshotDirPath, QString* errorText);

class MigrationProfileSettings {
public:
    static bool capture(const QString& path, QString* errorText);
    static bool validate(const QString& path, QString* errorText);
    static bool storageRoots(const QString& path, QMap<QString,QString>* roots, QString* errorText);
    static bool restore(const QString& path, QString* errorText);
};
