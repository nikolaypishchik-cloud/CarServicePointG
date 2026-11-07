#pragma once

#include <QString>

class RuntimeRecoveryCoordinator {
public:
    static bool recoverPendingSnapshotRestore(QString* errorText = nullptr);
    static bool prepareMaterializedRuntime(QString* errorText = nullptr);
    static bool preparePublicationRuntime(const QString& snapshotDirPath, QString* errorText = nullptr);
    static bool prepareValidatedRuntime(QString* errorText = nullptr);
};
