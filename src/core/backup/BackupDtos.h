#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include "core/backup/RecoveryUiContracts.h"

struct DataCommitEvent {
    QString eventId;
    QString commitId;
    QDateTime committedAt;
    QString module;
    QString page;
    QString entityType;
    QString entityId;
    QString orderUid;
    int orderNumber = 0;
    QString operationType;
    QString userVisibleDescription;
    QString schemaVersion;
    QString appVersion;
    QStringList changedTables;
};

struct SnapshotJobContext {
    DataCommitEvent event;
    QString requestedBy;
    QString source = QStringLiteral("autobackup");
    bool emergency = false;
    int retryCount = 0;
    qint64 enqueuedAtMs = 0;
};

struct SnapshotMetadata {
    QString snapshotId;
    QDateTime createdAt;
    QString operationType;
    QString module;
    QString page;
    QString entityType;
    QString entityId;
    QString orderUid;
    int orderNumber = 0;
    QString description;
    QString appVersion;
    QString schemaVersion;
    QString dbPath;
    QString snapshotPath;
    qint64 snapshotSizeBytes = 0;
    QString validationStatus;
    QString commitId;
    QString eventId;
    QString source;
    QString engineUsed;
    QString backupRoot;
    QString hashSha256;
    QString warning;
    QString degradedReason;
    QStringList changedTables;
    qint64 dbFileSizeBytes = 0;
    qint64 pageCount = 0;
    qint64 snapshotDurationMs = 0;
    qint64 validationDurationMs = 0;

    QJsonObject toJson() const;
    static SnapshotMetadata fromJson(const QJsonObject& o);
};

struct BackupStatusSnapshot {
    int queuedJobs = 0;
    int runningJobs = 0;
    int pendingJobs = 0;
    int snapshotsToday = 0;
    qint64 totalSizeBytes = 0;
    QDateTime lastBackupAt;
    QString lastBackupStatus;
    QString lastError;
    QString engineMode;
    bool engineDegraded = false;
    QString degradedReason;
    QString lastEngineError;
    QString lastFallbackReason;
    qint64 lastEnqueueMs = 0;
    qint64 lastQueueWaitMs = 0;
    qint64 lastSnapshotDurationMs = 0;
    qint64 lastValidateDurationMs = 0;
    qint64 lastManifestWriteDurationMs = 0;
    qint64 lastCleanupDurationMs = 0;
};

struct BackupValidationSummary {
    int checkedCount = 0;
    int validCount = 0;
    int invalidCount = 0;
    int missingCount = 0;
    QStringList problems;
};

struct RecoverySessionState {
    QString sessionId;
    QDateTime appStartTime;
    QDateTime lastHeartbeat;
    QDateTime lastDraftWrite;
    bool cleanShutdown = false;
    bool dirtyState = false;
    QString activeModule;
    int draftCount = 0;
    QString appVersion;
    QString schemaVersion;
    QString sessionDir;

    QJsonObject toJson() const;
    static RecoverySessionState fromJson(const QJsonObject& o);
};

struct DraftPayload {
    QString recoverySessionId;
    QString draftId;
    QString module;
    QString formType;
    QString entityType;
    QString entityId;
    QString orderUid;
    QDateTime createdAt;
    QDateTime updatedAt;
    QString committedBaseVersion;
    QString payloadSchemaVersion;
    QString appVersion;
    QString schemaVersion;
    QString baseEntityVersion;
    QDateTime baseUpdatedAt;
    QString payloadHash;
    QJsonObject payloadData;
    QString validationState;
    QString conflictState;
    QString filePath;
    qint64 fileSizeBytes = 0;

    QJsonObject toJson() const;
    static DraftPayload fromJson(const QJsonObject& o);
};


struct RecoveryMetadataResolution {
    bool valid = false;
    QString effectiveFormType;
    int orderNumber = -1;
    QString error;
};

struct RecoveryConflictCheck {
    bool conflict = false;
    QString warning;
    QString currentCommittedVersion;
};

struct RecoveryUiRestoreRequest {
    bool handled = false;
    QString effectiveFormType;
    int orderNumber = -1;
    RecoveryUiTarget target = RecoveryUiTarget::None;
    bool restoreOrderDraft = false;
    bool restoreWorkPartsDraft = false;
    bool openWorkPartsTab = false;
    DraftPayload draft;
};

struct RecoveryRestoreResult {
    int restoredCount = 0;
    int skippedCount = 0;
    int conflictCount = 0;
    QStringList warnings;
    QStringList errors;
};

