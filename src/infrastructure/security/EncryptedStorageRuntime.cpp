// P2 architecture migration: implementation module converted from legacy .ipp implementation-fragment decomposition.
// Compiled as a separate P2 implementation module; include-order dependencies are isolated in the module prelude.

#include "infrastructure/security/EncryptedStorageModulePrelude.h"

namespace EncryptedStorage {

bool prepareRuntimeDatabases(QString* errorText)
{
    QDir().mkpath(AppRuntime::securityDir());
    QDir session(AppRuntime::sessionDir());
    session.mkpath(QStringLiteral("."));

    QByteArray key;
    if (!loadOrCreateMasterKey(&key, errorText)) return false;
    Q_UNUSED(key);

    for (const DbMapping& mapping : dbMappings()) {
        if (!ensureLegacyPlainMigration(mapping, errorText)) return false;
    }
    if (!consumePendingLegacyImport(errorText)) return false;

    const bool runtimeArtifactsPresent = hasAnyRuntimeArtifacts();
    if (runtimeArtifactsPresent) {
        qInfo() << "Detected unfinished runtime DB session; starting crash recovery from runtime artifacts";
    }

    for (const DbMapping& mapping : dbMappings()) {
        const bool mappingHasArtifacts = hasRuntimeArtifacts(mapping);
        if (!runtimeArtifactsPresent) {
            if (!restoreRuntimeFromSealed(mapping, errorText)) return false;
        }

        if (runtimeArtifactsPresent && !mappingHasArtifacts) {
            if (!restoreRuntimeFromSealed(mapping, errorText)) return false;
        }

        QString validationError;
        if (!validateRuntimeSqlite(mapping, &validationError)) {
            qWarning() << "Runtime DB integrity check failed for" << mapping.runtimePath << validationError;
            if (hasRuntimeArtifacts(mapping)) {
                if (!quarantineRuntimeArtifacts(mapping, QStringLiteral("invalid_runtime"), errorText)) return false;
            } else {
                removePlaintextArtifacts(mapping.runtimePath);
            }

            if (QFileInfo::exists(mapping.sealedPath)) {
                if (!restoreRuntimeFromSealed(mapping, errorText)) return false;
                QString restoredValidationError;
                if (!validateRuntimeSqlite(mapping, &restoredValidationError)) {
                    qWarning() << "Restored runtime DB integrity check failed for" << mapping.runtimePath << restoredValidationError;
                    if (runtimeRoleForMapping(mapping) == RuntimeDbPreflightValidator::DbRole::Registry) {
                        removePlaintextArtifacts(mapping.runtimePath);
                    } else {
                        if (errorText) {
                            *errorText = QStringLiteral("Відновлений runtime snapshot пошкоджений: %1")
                                .arg(mapping.runtimePath);
                        }
                        return false;
                    }
                }
            } else {
                if (runtimeRoleForMapping(mapping) == RuntimeDbPreflightValidator::DbRole::Registry) {
                    qWarning() << "Registry runtime DB invalid and no sealed snapshot; runtime registry will be rebuilt from workspace:" << mapping.runtimePath;
                    removePlaintextArtifacts(mapping.runtimePath);
                } else {
                    if (errorText) {
                        *errorText = QStringLiteral("Runtime БД пошкоджена, а sealed snapshot відсутній: %1")
                            .arg(mapping.runtimePath);
                    }
                    return false;
                }
            }
        } else if (runtimeArtifactsPresent && mappingHasArtifacts) {
            qInfo() << "Recovered runtime DB after unfinished session:" << mapping.runtimePath;
        }
    }

    if (!writeRuntimeSessionMarker(errorText)) return false;
    return true;
}

bool sealRuntimeDatabases(QString* errorText)
{
    for (const DbMapping& mapping : dbMappings()) {
        if (!QFileInfo::exists(mapping.runtimePath)) {
            removePlaintextArtifacts(mapping.runtimePath);
            continue;
        }
        if (!prepareSqliteForSealing(mapping.runtimePath, errorText)) return false;
        if (!encryptPlainFileToSealed(mapping.runtimePath, mapping.sealedPath, errorText)) return false;
        removePlaintextArtifacts(mapping.runtimePath);
    }
    clearRuntimeSessionMarker();
    return true;
}

bool exportPlaintextDatabase(const QString& sealedPath, const QString& targetPlainPath, QString* errorText)
{
    return decryptSealedFileToPlain(sealedPath, targetPlainPath, errorText);
}

bool restorePlaintextBackupToActive(const QString& sourcePlainPath, QString* errorText)
{
    if (!QFileInfo::exists(sourcePlainPath)) {
        if (errorText) *errorText = QStringLiteral("Файл резервної копії не знайдено");
        return false;
    }
    QByteArray key;
    if (!loadOrCreateMasterKey(&key, errorText)) return false;
    Q_UNUSED(key);

    safeRemoveEncryptedStorageFile(AppRuntime::sealedActiveDbFilePath(), AppRuntime::securityDir());
    if (!encryptPlainFileToSealed(sourcePlainPath, AppRuntime::sealedActiveDbFilePath(), errorText)) return false;
    removePlaintextArtifacts(AppRuntime::activeDbFilePath());
    removePlaintextArtifacts(AppRuntime::registryDbFilePath());
    removePlaintextArtifacts(AppRuntime::legacyDbFilePath());
    safeRemoveEncryptedStorageFile(AppRuntime::sealedRegistryDbFilePath(), AppRuntime::securityDir());
    return true;
}

bool publishPreparedWorkspace(const QString& sourcePlainPath, const QString& archivePath, const QString& workspacePath, QString* errorText)
{
    if (!QFileInfo::exists(sourcePlainPath)) {
        if (errorText) *errorText = QStringLiteral("Файл legacy БД не знайдено");
        return false;
    }
    QByteArray key;
    if (!loadOrCreateMasterKey(&key, errorText)) return false;
    Q_UNUSED(key);

    // Publish the validated workspace and preserve the audited source in the archive.
    safeRemoveEncryptedStorageFile(AppRuntime::sealedActiveDbFilePath(), AppRuntime::securityDir());
    if (!encryptPlainFileToSealed(workspacePath, AppRuntime::sealedActiveDbFilePath(), errorText)) return false;

    safeRemoveEncryptedStorageFile(AppRuntime::sealedArchiveDbFilePath(), AppRuntime::securityDir());
    if (!encryptPlainFileToSealed(archivePath, AppRuntime::sealedArchiveDbFilePath(), errorText)) return false;

    safeRemoveEncryptedStorageFile(AppRuntime::sealedRegistryDbFilePath(), AppRuntime::securityDir());

    safeRemoveEncryptedStorageFile(AppRuntime::sealedLegacyDbFilePath(), AppRuntime::securityDir());
    if (!encryptPlainFileToSealed(sourcePlainPath, AppRuntime::sealedLegacyDbFilePath(), errorText)) return false;

    removePlaintextArtifacts(AppRuntime::activeDbFilePath());
    removePlaintextArtifacts(AppRuntime::archiveDbFilePath());
    removePlaintextArtifacts(AppRuntime::registryDbFilePath());
    removePlaintextArtifacts(AppRuntime::legacyDbFilePath());
    return true;
}

} // namespace EncryptedStorage
