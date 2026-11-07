#pragma once

#include <QString>

namespace EncryptedStorage {

bool prepareRuntimeDatabases(QString* errorText = nullptr);
bool sealRuntimeDatabases(QString* errorText = nullptr);
bool exportPlaintextDatabase(const QString& sealedPath, const QString& targetPlainPath, QString* errorText = nullptr);
bool restorePlaintextBackupToActive(const QString& sourcePlainPath, QString* errorText = nullptr);
bool publishPreparedWorkspace(const QString& sourcePlainPath, const QString& archivePath, const QString& workspacePath, QString* errorText = nullptr);

} // namespace EncryptedStorage
