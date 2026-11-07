#include "core/services/NativeBackupRestoreService.h"

#include "runtime/AppRuntime.h"
#include "runtime/RuntimeServices.h"
#include "infrastructure/db/sqlite/Db.h"
#include "infrastructure/security/EncryptedStorage.h"
#include "core/backup/BackupDtos.h"
#include "core/backup/RestoreCoordinator.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QSqlQuery>

bool NativeBackupRestoreService::auditBackupFile(const QString& sourcePath,
                                                 NativeBackupAuditResult* result,
                                                 QString* errorText)
{
    NativeBackupAuditResult local;
    if (!DatabaseImportClassifier::classify(sourcePath, &local.classification, errorText)) {
        if (result) *result = local;
        return false;
    }
    if (!local.classification.isNativeBackup()) {
        if (errorText) {
            *errorText = QStringLiteral("Обраний файл не є резервною копією поточної БД CarService.");
        }
        if (result) *result = local;
        return false;
    }
    local.orderCount = local.classification.workOrdersCount;
    if (result) *result = local;
    return true;
}

bool NativeBackupRestoreService::restoreBackupFile(const QString& sourcePath,
                                                   NativeBackupRestoreResult* result,
                                                   QString* errorText)
{
    NativeBackupAuditResult audit;
    if (!auditBackupFile(sourcePath, &audit, errorText)) {
        return false;
    }

    if (auto* coordinator = RuntimeServices::restoreCoordinator()) {
        SnapshotMetadata meta;
        meta.snapshotPath = sourcePath;
        meta.validationStatus = QStringLiteral("valid");
        meta.source = QStringLiteral("compatibility_wrapper");
        return coordinator->restoreSnapshot(meta, result, errorText);
    }

    if (errorText) {
        *errorText = QStringLiteral("RestoreCoordinator недоступний. Відновлення БД дозволено тільки через контрольований RestoreCoordinator flow.");
    }
    return false;
}
