#include "core/backup/BackupTelemetry.h"

void BackupTelemetry::recordEnqueueMs(qint64 value)
{
    m_snapshot.lastEnqueueMs = value;
}

void BackupTelemetry::recordQueueWaitMs(qint64 value)
{
    m_snapshot.lastQueueWaitMs = value;
}

void BackupTelemetry::recordSnapshotMs(qint64 value)
{
    m_snapshot.lastSnapshotMs = value;
}

void BackupTelemetry::recordValidateMs(qint64 value)
{
    m_snapshot.lastValidateMs = value;
}

void BackupTelemetry::recordManifestWriteMs(qint64 value)
{
    m_snapshot.lastManifestWriteMs = value;
}

void BackupTelemetry::recordCleanupMs(qint64 value)
{
    m_snapshot.lastCleanupMs = value;
}

BackupTelemetrySnapshot BackupTelemetry::snapshot() const
{
    return m_snapshot;
}
