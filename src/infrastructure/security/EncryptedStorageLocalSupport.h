#pragma once

#include "infrastructure/db/sqlite/RuntimeDbPreflightValidator.h"

#include <QByteArray>
#include <QList>
#include <QString>

namespace EncryptedStorage {

struct DbMapping {
    QString runtimePath;
    QString sealedPath;
    QString legacyPlainPath;
};

QList<DbMapping> dbMappings();
void removePlaintextArtifacts(const QString& basePath);
bool hasRuntimeArtifacts(const DbMapping& mapping);
bool hasAnyRuntimeArtifacts();
bool writeRuntimeSessionMarker(QString* errorText);
void clearRuntimeSessionMarker();
bool quarantineRuntimeArtifacts(const DbMapping& mapping, const QString& reason, QString* errorText);
RuntimeDbPreflightValidator::DbRole runtimeRoleForMapping(const DbMapping& mapping);
bool validateRuntimeSqlite(const DbMapping& mapping, QString* errorText);
bool restoreRuntimeFromSealed(const DbMapping& mapping, QString* errorText);
bool loadOrCreateMasterKey(QByteArray* keyOut, QString* errorText);
bool encryptPlainFileToSealed(const QString& plainPath, const QString& sealedPath, QString* errorText);
bool decryptSealedFileToPlain(const QString& sealedPath, const QString& plainPath, QString* errorText);
bool prepareSqliteForSealing(const QString& dbPath, QString* errorText);
bool ensureLegacyPlainMigration(const DbMapping& mapping, QString* errorText);
bool consumePendingLegacyImport(QString* errorText);
bool exportPlainFromRuntimeOrSealed(const QString& runtimePath, const QString& sealedPath, const QString& targetPlainPath, QString* errorText);

} // namespace EncryptedStorage
