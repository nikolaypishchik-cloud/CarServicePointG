#pragma once

#include <QtGlobal>

struct BackupTelemetrySnapshot
{
    qint64 lastEnqueueMs = 0;
    qint64 lastQueueWaitMs = 0;
    qint64 lastSnapshotMs = 0;
    qint64 lastValidateMs = 0;
    qint64 lastManifestWriteMs = 0;
    qint64 lastCleanupMs = 0;
};

class BackupTelemetry
{
public:
    void recordEnqueueMs(qint64 value);
    void recordQueueWaitMs(qint64 value);
    void recordSnapshotMs(qint64 value);
    void recordValidateMs(qint64 value);
    void recordManifestWriteMs(qint64 value);
    void recordCleanupMs(qint64 value);
    BackupTelemetrySnapshot snapshot() const;

private:
    BackupTelemetrySnapshot m_snapshot;
};
