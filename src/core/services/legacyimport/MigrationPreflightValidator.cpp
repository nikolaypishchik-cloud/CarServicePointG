#include "core/services/legacyimport/MigrationPreflightValidator.h"

#include "runtime/AppRuntime.h"
#include "infrastructure/db/sqlite/RuntimeDbPreflightValidator.h"

#include <QFileInfo>

namespace {

bool validateRole(const QString& path,
                  RuntimeDbPreflightValidator::DbRole role,
                  const QString& label,
                  QString* errorText,
                  QStringList* failures)
{
    QString localError;
    if (!RuntimeDbPreflightValidator::validateFile(path, role, &localError)) {
        failures->push_back(QStringLiteral("%1=%2").arg(label, localError));
        if (errorText && errorText->trimmed().isEmpty()) {
            *errorText = QStringLiteral("%1 preflight failed: %2").arg(label, localError);
        }
        return false;
    }
    return true;
}

} // namespace

bool MigrationPreflightValidator::validateSourceBundle(const LegacyImportBundle& bundle,
                                                       const LegacyImportAuditReport& auditReport,
                                                       QString* errorText)
{
    QStringList failures;
    if (bundle.primaryDbPath.trimmed().isEmpty() || !QFileInfo::exists(bundle.primaryDbPath)) {
        failures << QStringLiteral("source_db_missing=%1").arg(bundle.primaryDbPath);
    }
    if (auditReport.orderCount <= 0) failures << QStringLiteral("source_orders=0");
    if (auditReport.serviceCount < 0) failures << QStringLiteral("source_services_invalid");
    if (auditReport.partCount < 0) failures << QStringLiteral("source_parts_invalid");

    if (!failures.isEmpty()) {
        if (errorText) *errorText = QStringLiteral("Migration source preflight failed: %1").arg(failures.join(QStringLiteral(" | ")));
        return false;
    }
    return true;
}

bool MigrationPreflightValidator::validateRuntimeTargets(QString* errorText)
{
    QStringList failures;
    validateRole(AppRuntime::activeDbFilePath(), RuntimeDbPreflightValidator::DbRole::Workspace, QStringLiteral("workspace"), errorText, &failures);

    // A fresh post-import reopen may need to bootstrap the registry database
    // from the newly restored workspace. Validate the registry only when the
    // file already exists; the startup refresh path will materialize it if not.
    if (QFileInfo::exists(AppRuntime::registryDbFilePath())) {
        validateRole(AppRuntime::registryDbFilePath(), RuntimeDbPreflightValidator::DbRole::Registry, QStringLiteral("registry"), errorText, &failures);
    }

    // Archive DB may legitimately be absent before the first import; only validate it when present.
    if (QFileInfo::exists(AppRuntime::archiveDbFilePath())) {
        validateRole(AppRuntime::archiveDbFilePath(), RuntimeDbPreflightValidator::DbRole::Archive, QStringLiteral("archive"), errorText, &failures);
    }

    if (!failures.isEmpty()) {
        if (errorText) *errorText = QStringLiteral("Migration runtime preflight failed: %1").arg(failures.join(QStringLiteral(" | ")));
        return false;
    }
    return true;
}
