#pragma once

#include "core/backup/BackupDtos.h"

class RecoveryMetadataResolver
{
public:
    static RecoveryMetadataResolution resolve(const DraftPayload& draft);
    static RecoveryUiRestoreRequest buildUiRestoreRequest(const DraftPayload& draft, const RecoveryMetadataResolution& metadata);
};
