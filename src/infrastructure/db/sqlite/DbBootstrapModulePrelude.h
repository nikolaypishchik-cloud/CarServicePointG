#pragma once

// P2 architecture module prelude.
// Generated during monolith-removal p2 so converted implementation slices
// compile as separate .cpp modules without being text-included by the owner.
// Owner: src/infrastructure/db/sqlite/DbBootstrap.cpp

#include "infrastructure/db/sqlite/Db.h"
#include "infrastructure/db/sqlite/DbFacadeSupport.h"
#include "infrastructure/db/sqlite/DbArchiveInternals.h"
#include "infrastructure/db/sqlite/LegacyCanonicalImportNormalization.h"
#include "infrastructure/db/sqlite/LegacyCanonicalAncillaryImportSupport.h"
#include "infrastructure/db/sqlite/DbSchemaAuditSupport.h"
#include "infrastructure/db/sqlite/DbSettingsCacheSupport.h"
#include "infrastructure/db/sqlite/DbThreadConnectionSupport.h"
#include "infrastructure/db/sqlite/RuntimeDbPreflightValidator.h"
#include "infrastructure/db/sqlite/OrderArchiveShardSqlSupport.h"
#include "core/services/LegacyMutationLogSupport.h"
#include "core/services/OrderIdentityService.h"
#include "core/services/OrderStoragePolicyService.h"
#include "core/services/legacyimport/RuntimeRecoveryCoordinator.h"
#include "core/services/OrderArchiveRotationService.h"
#include "core/services/OrderDetailIntegrityValidator.h"
#include "core/services/legacyimport/LegacyOrderChildMapper.h"
#include "infrastructure/security/EncryptedStorage.h"
#include "runtime/AppRuntime.h"
#include "infrastructure/db/sqlite/DbCatalogImportLocalSupport.h"
#include <QCryptographicHash>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QTextStream>
#include <QUuid>
#include <QVariant>
#include <QVector>
#include <algorithm>
#include <limits>

// File-local helpers required by converted slices.
namespace {

bool applyOpenPragmas(QSqlDatabase database, QString* errorText)
{
    if (!database.isValid()) {
        if (errorText) *errorText = QStringLiteral("Invalid database handle");
        return false;
    }
    if (!database.isOpen() && !database.open()) {
        if (errorText) *errorText = database.lastError().text();
        return false;
    }

    QSqlQuery q(database);
    auto execPragma = [&](const QString& sql) -> bool {
        if (q.exec(sql)) return true;
        if (errorText) *errorText = q.lastError().text();
        return false;
    };

    if (!execPragma(QStringLiteral("PRAGMA foreign_keys = ON;"))) return false;
    if (!execPragma(QStringLiteral("PRAGMA busy_timeout = 5000;"))) return false;
    if (!execPragma(QStringLiteral("PRAGMA journal_mode = WAL;"))) return false;
    if (!execPragma(QStringLiteral("PRAGMA synchronous = FULL;"))) return false;
    if (!execPragma(QStringLiteral("PRAGMA temp_store = MEMORY;"))) return false;
    if (!execPragma(QStringLiteral("PRAGMA cache_size = -32000;"))) return false;
    if (!execPragma(QStringLiteral("PRAGMA mmap_size = 268435456;"))) return false;
    if (!execPragma(QStringLiteral("PRAGMA optimize;"))) return false;
    return true;
}


}
