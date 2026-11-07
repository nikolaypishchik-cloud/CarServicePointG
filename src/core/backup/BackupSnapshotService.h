#pragma once

#include "core/backup/BackupDtos.h"

#include <QString>

#include <atomic>
#include <memory>

class BackupSnapshotService
{
public:
    static bool createSnapshot(const SnapshotJobContext& ctx, SnapshotMetadata* outMeta, QString* errorText = nullptr, const std::shared_ptr<std::atomic_bool>& cancelToken = {});
    static bool validateSnapshot(const QString& snapshotPath, QString* errorText = nullptr);
    static bool rehearseRestore(const QString& snapshotPath, QString* errorText = nullptr);
};
