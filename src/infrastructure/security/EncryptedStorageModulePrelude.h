#pragma once

// P2 architecture module prelude.
// Generated during monolith-removal p2 so converted implementation slices
// compile as separate .cpp modules without being text-included by the owner.
// Owner: src/infrastructure/security/EncryptedStorage.cpp

#include "infrastructure/security/EncryptedStorage.h"
#include "infrastructure/security/EncryptedStorageLocalSupport.h"
#include "runtime/AppRuntime.h"
#include "core/io/SafeFileOps.h"
#include "infrastructure/db/sqlite/ScopedConnectionManager.h"
#include "infrastructure/db/sqlite/RuntimeDbPreflightValidator.h"
#include <QByteArray>
#include <QBuffer>
#include <QDataStream>
#include <QDir>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QThread>
#include <cstring>
#include <windows.h>
#include <wincrypt.h>
#include <bcrypt.h>

inline QStringList encryptedStorageSafeDeleteRoots(const QString& extraRoot = QString())
{
    QStringList roots{AppRuntime::dataDir(), AppRuntime::securityDir(), AppRuntime::sessionDir()};
    if (!extraRoot.trimmed().isEmpty()) roots << extraRoot;
    roots.removeDuplicates();
    return roots;
}

inline bool safeRemoveEncryptedStorageFile(const QString& path, const QString& extraRoot = QString())
{
    return SafeFileOps::safeRemoveFile(path, encryptedStorageSafeDeleteRoots(extraRoot));
}
