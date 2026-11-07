#pragma once

#include "core/backup/BackupDtos.h"

class RecoveryConflictDetector
{
public:
    static QString initialConflictReason(const DraftPayload& draft);
    static QString effectiveConflictState(const DraftPayload& draft);
    static QString runtimeConflictReason(const DraftPayload& draft, const QString& currentCommittedVersion);
    static RecoveryConflictCheck checkRuntimeConflict(const DraftPayload& draft, const QString& currentCommittedVersion);
};
