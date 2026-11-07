#pragma once

#include <QString>

struct LegacyImportSafetySnapshot {
    QString snapshotDirPath;
    bool created = false;
};

class LegacyImportSafetySupport
{
public:
    static bool createSnapshot(LegacyImportSafetySnapshot* snapshot, QString* errorText = nullptr);
    static bool restoreSnapshot(const LegacyImportSafetySnapshot& snapshot, QString* errorText = nullptr);
    static void quarantineCurrentArtifacts(const LegacyImportSafetySnapshot& snapshot, const QString& reason);
};
