#include "core/services/legacyimport/LegacyImportPolicySupport.h"

#include "runtime/AppRuntime.h"
#include "core/io/SafeFileOps.h"
#include "core/services/legacyimport/LegacyImportTypes.h"

#include <QDir>
#include <QFileInfo>
#include <algorithm>

namespace {

QString normalizedAbsPath(const QString& path)
{
    if (path.trimmed().isEmpty()) return QString();
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

QString resolvedPath(const QString& path)
{
    const auto canonical = QFileInfo(path).canonicalFilePath();
    return canonical.isEmpty() ? normalizedAbsPath(path) : canonical;
}

}

bool LegacyImportPolicySupport::validateSourceSelection(const QString& sourcePath, QString* errorText)
{
    const QString normalized = normalizedAbsPath(sourcePath);
    if (normalized.isEmpty()) {
        if (errorText) *errorText = QStringLiteral("Не вибрано legacy джерело для імпорту");
        return false;
    }

    const QStringList forbiddenRoots = {
        AppRuntime::securityDir(),
        AppRuntime::sessionDir()
    };
    for (const QString& root : forbiddenRoots) {
        if (SafeFileOps::isInsideRoot(normalized, root)
                || SafeFileOps::isInsideRoot(resolvedPath(normalized), resolvedPath(root))) {
            if (errorText) {
                *errorText = QStringLiteral("Заборонено імпортувати БД напряму з внутрішнього runtime/sealed каталогу застосунку. Потрібен зовнішній legacy backup або архів.");
            }
            return false;
        }
    }
    return true;
}

bool LegacyImportPolicySupport::validateExecutePolicy(const LegacyImportBundle& bundle,
                                                      const LegacyImportAuditReport& auditReport,
                                                      QString* errorText,
                                                      QStringList* warnings)
{
    if (!validateSourceSelection(bundle.sourcePath, errorText)) {
        return false;
    }

    const QString primaryDb = normalizedAbsPath(bundle.primaryDbPath);
    const QStringList forbiddenExact = {
        normalizedAbsPath(AppRuntime::activeDbFilePath()),
        normalizedAbsPath(AppRuntime::legacyDbFilePath()),
        normalizedAbsPath(AppRuntime::archiveDbFilePath()),
        normalizedAbsPath(AppRuntime::registryDbFilePath()),
        normalizedAbsPath(AppRuntime::pendingLegacyImportFilePath())
    };
    const auto primaryTarget = resolvedPath(primaryDb);
    if (std::any_of(forbiddenExact.cbegin(), forbiddenExact.cend(), [&](const QString& path) {
            return SafeFileOps::isSamePath(primaryTarget, resolvedPath(path));
        })) {
        if (errorText) {
            *errorText = QStringLiteral("Заборонено запускати execute mode на активній або службовій БД застосунку. Імпорт повинен йти лише через зовнішній legacy backup і staging-пайплайн.");
        }
        return false;
    }

    if (!auditReport.canExecute) {
        if (errorText) {
            *errorText = !auditReport.errorMessages.isEmpty()
                ? auditReport.errorMessages.constFirst()
                : QStringLiteral("Legacy джерело не пройшло pre-import audit; execute mode заборонено");
        }
        return false;
    }

    int blockingFindings = 0;
    for (const LegacyImportFinding& finding : auditReport.findings) {
        if (finding.blocksExecute) ++blockingFindings;
    }
    if (blockingFindings > 0) {
        if (errorText) {
            *errorText = QStringLiteral("У pre-import audit залишилися blocking-конфлікти (%1); execute mode заборонено").arg(blockingFindings);
        }
        return false;
    }

    if (warnings) {
        if (bundle.sourceKind.compare(QStringLiteral("zip"), Qt::CaseInsensitive) == 0) {
            warnings->push_back(QStringLiteral("Імпорт іде лише через staging-розпакування архіву; пряме використання zip як робочої БД заборонено."));
        }
        if (auditReport.detectedSidecars.isEmpty()) {
            warnings->push_back(QStringLiteral("sidecar-файли не знайдені; буде імпортовано лише legacy SQLite БД."));
        }
    }
    return true;
}
