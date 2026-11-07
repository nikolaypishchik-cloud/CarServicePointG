#include "core/services/legacyimport/LegacyImportAcceptanceSupport.h"

#include "core/services/LegacyMigrationService.h"
#include "core/services/legacyimport/LegacyImportTypes.h"

namespace {

LegacyImportAcceptanceItem item(const QString& id, const QString& title, const QString& status, const QString& note)
{
    LegacyImportAcceptanceItem v;
    v.id = id;
    v.title = title;
    v.status = status;
    v.note = note;
    return v;
}

bool countsMatch(int sourceCount, int importedCount)
{
    return sourceCount >= 0 && importedCount >= 0 && sourceCount == importedCount;
}

bool countsPreservedOrExpanded(int sourceCount, int importedCount)
{
    return sourceCount >= 0 && importedCount >= 0 && importedCount >= sourceCount;
}

bool hasInvalidLinks(const LegacyImportVerifyReport* verifyReport)
{
    if (!verifyReport) return true;
    for (const LegacyImportVerifyEntityResult& entity : verifyReport->entities) {
        if (entity.invalidLinkCount > 0 || entity.missingCount > 0) return true;
    }
    return false;
}

bool hasReportArtifacts(const LegacyMigrationResult* migrationResult)
{
    return migrationResult
        && !migrationResult->summaryReportPath.trimmed().isEmpty()
        && !migrationResult->validationReportPath.trimmed().isEmpty()
        && !migrationResult->conflictsJsonPath.trimmed().isEmpty()
        && !migrationResult->conflictsCsvPath.trimmed().isEmpty();
}

} // namespace

QVector<LegacyImportAcceptanceItem> LegacyImportAcceptanceSupport::evaluate(const LegacyImportAuditReport* auditReport,
                                                                            const LegacyMigrationResult* migrationResult,
                                                                            const LegacyImportVerifyReport* verifyReport)
{
    Q_UNUSED(auditReport)

    const bool verifySuccess = verifyReport && verifyReport->success;
    const bool noDangling = verifySuccess && !hasInvalidLinks(verifyReport) && verifyReport->errorMessages.isEmpty();

    return {
        item(QStringLiteral("8.1"), QStringLiteral("Усі наряди перенесені"),
             countsMatch(migrationResult ? migrationResult->sourceOrderCount : -1, migrationResult ? migrationResult->importedOrderCount : -1) ? QStringLiteral("PASS") : QStringLiteral("FAIL"),
             QStringLiteral("orders: %1 -> %2").arg(migrationResult ? migrationResult->sourceOrderCount : -1).arg(migrationResult ? migrationResult->importedOrderCount : -1)),

        item(QStringLiteral("8.2"), QStringLiteral("Усі роботи перенесені"),
             countsMatch(migrationResult ? migrationResult->sourceServiceCount : -1, migrationResult ? migrationResult->importedServiceCount : -1) ? QStringLiteral("PASS") : QStringLiteral("FAIL"),
             QStringLiteral("services: %1 -> %2").arg(migrationResult ? migrationResult->sourceServiceCount : -1).arg(migrationResult ? migrationResult->importedServiceCount : -1)),

        item(QStringLiteral("8.3"), QStringLiteral("Усі запчастини перенесені"),
             countsMatch(migrationResult ? migrationResult->sourcePartCount : -1, migrationResult ? migrationResult->importedPartCount : -1) ? QStringLiteral("PASS") : QStringLiteral("FAIL"),
             QStringLiteral("parts: %1 -> %2").arg(migrationResult ? migrationResult->sourcePartCount : -1).arg(migrationResult ? migrationResult->importedPartCount : -1)),

        item(QStringLiteral("8.4"), QStringLiteral("Усі зв’язки service_masters відновлені"),
             countsMatch(migrationResult ? migrationResult->sourceServiceMasterCount : -1, migrationResult ? migrationResult->importedServiceMasterCount : -1) ? QStringLiteral("PASS") : QStringLiteral("FAIL"),
             QStringLiteral("service_masters: %1 -> %2").arg(migrationResult ? migrationResult->sourceServiceMasterCount : -1).arg(migrationResult ? migrationResult->importedServiceMasterCount : -1)),

        item(QStringLiteral("8.5"), QStringLiteral("Усі зовнішні ремонти перенесені"),
             countsMatch(migrationResult ? migrationResult->sourceOutsourceCount : -1, migrationResult ? migrationResult->importedOutsourceCount : -1) ? QStringLiteral("PASS") : QStringLiteral("FAIL"),
             QStringLiteral("outsource_repairs: %1 -> %2").arg(migrationResult ? migrationResult->sourceOutsourceCount : -1).arg(migrationResult ? migrationResult->importedOutsourceCount : -1)),

        item(QStringLiteral("8.6"), QStringLiteral("Усі майстри/адміни перенесені"),
             countsMatch(migrationResult ? migrationResult->sourceMasterCount : -1, migrationResult ? migrationResult->importedMasterCount : -1) ? QStringLiteral("PASS") : QStringLiteral("FAIL"),
             QStringLiteral("masters: %1 -> %2").arg(migrationResult ? migrationResult->sourceMasterCount : -1).arg(migrationResult ? migrationResult->importedMasterCount : -1)),

        item(QStringLiteral("8.7"), QStringLiteral("Каталог, планування та витрати перенесені"),
             (countsPreservedOrExpanded(migrationResult ? migrationResult->sourceCatalogCount : -1, migrationResult ? migrationResult->importedCatalogCount : -1)
              && countsMatch(migrationResult ? migrationResult->sourcePlanningCount : -1, migrationResult ? migrationResult->importedPlanningCount : -1)
              && countsMatch(migrationResult ? migrationResult->sourceExpenseCount : -1, migrationResult ? migrationResult->importedExpenseCount : -1)) ? QStringLiteral("PASS") : QStringLiteral("FAIL"),
             QStringLiteral("catalog %1->%2 | planning %3->%4 | expenses %5->%6")
                .arg(migrationResult ? migrationResult->sourceCatalogCount : -1)
                .arg(migrationResult ? migrationResult->importedCatalogCount : -1)
                .arg(migrationResult ? migrationResult->sourcePlanningCount : -1)
                .arg(migrationResult ? migrationResult->importedPlanningCount : -1)
                .arg(migrationResult ? migrationResult->sourceExpenseCount : -1)
                .arg(migrationResult ? migrationResult->importedExpenseCount : -1)),

        item(QStringLiteral("8.8"), QStringLiteral("Немає втрачених, порожніх або висячих записів"),
             noDangling ? QStringLiteral("PASS") : QStringLiteral("FAIL"),
             noDangling ? QStringLiteral("verify не виявив missing/invalid links") : QStringLiteral("перевірити MigrationValidationReport / verify errors")),

        item(QStringLiteral("8.9"), QStringLiteral("Повторний запуск не створює дублікати"),
             QStringLiteral("IMPLEMENTED"),
             QStringLiteral("fingerprint guard у журналі імпорту блокує повторний небезпечний import run")),

        item(QStringLiteral("8.10"), QStringLiteral("Є підсумковий звіт, validation report і conflicts export"),
             hasReportArtifacts(migrationResult) ? QStringLiteral("PASS") : QStringLiteral("FAIL"),
             QStringLiteral("summary=%1 | validation=%2 | conflicts_json=%3 | conflicts_csv=%4")
                .arg(migrationResult ? migrationResult->summaryReportPath : QString())
                .arg(migrationResult ? migrationResult->validationReportPath : QString())
                .arg(migrationResult ? migrationResult->conflictsJsonPath : QString())
                .arg(migrationResult ? migrationResult->conflictsCsvPath : QString())),

        item(QStringLiteral("8.11"), QStringLiteral("Є rollback/snapshot для відновлення"),
             (migrationResult && !migrationResult->snapshotDirPath.trimmed().isEmpty()) ? QStringLiteral("PASS") : QStringLiteral("FAIL"),
             migrationResult ? migrationResult->snapshotDirPath : QString()),

        item(QStringLiteral("8.12"), QStringLiteral("Історія legacy БД повністю збережена в новому середовищі"),
             verifySuccess ? QStringLiteral("PASS") : QStringLiteral("FAIL"),
             verifySuccess ? QStringLiteral("counts + post-check completed successfully") : QStringLiteral("verify mode не підтвердив повне збереження історії"))
    };
}
