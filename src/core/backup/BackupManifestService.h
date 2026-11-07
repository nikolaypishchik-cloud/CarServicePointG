#pragma once

#include "core/backup/BackupDtos.h"

#include <QString>
#include <QVector>

class BackupManifestService
{
public:
    static QVector<SnapshotMetadata> listSnapshots();
    static bool appendSnapshot(const SnapshotMetadata& meta, QString* errorText = nullptr);
    static bool replaceSnapshotIndex(const QVector<SnapshotMetadata>& entries, QString* errorText = nullptr);
    static bool reconcile(QString* errorText = nullptr);
};
