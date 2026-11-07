#include "core/services/legacyimport/LegacyImportUiSupport.h"

#include "core/services/LegacyMigrationService.h"
#include "core/services/legacyimport/LegacyImportTypes.h"

QString LegacyImportUiSupport::buildPreviewDialogText(const LegacyImportAuditReport& report, const QString& reportDirPath)
{
    return QString::fromUtf8(
        "Dry-run / preview завершено.\n\n"
        "Джерело: %1\n"
        "Тип: %2\n"
        "Нарядів: %3\n"
        "Робіт: %4\n"
        "Запчастин: %5\n"
        "Sidecar-файли: %6\n"
        "Попереджень: %7\n"
        "Blocking findings: %8\n\n"
        "Повний preview report:\n%9\n\n"
        "Можна переходити до реальної міграції legacy БД.")
        .arg(report.sourcePath)
        .arg(report.sourceKind)
        .arg(report.orderCount)
        .arg(report.serviceCount)
        .arg(report.partCount)
        .arg(report.detectedSidecars.isEmpty() ? QString::fromUtf8("немає") : report.detectedSidecars.join(QStringLiteral(", ")))
        .arg(report.warningMessages.size())
        .arg([&report]() {
            int blocking = 0;
            for (const LegacyImportFinding& finding : report.findings) if (finding.blocksExecute) ++blocking;
            return blocking;
        }())
        .arg(reportDirPath);
}

QString LegacyImportUiSupport::buildSuccessDialogText(const LegacyMigrationResult& result, const QString& reportDirPath)
{
    return QString::fromUtf8(
        "Імпорт старої БД завершено.\n\n"
        "Наряди: %1 -> %2\n"
        "Роботи: %3 -> %4\n"
        "Запчастини: %5 -> %6\n"
        "service_masters: %7 -> %8\n"
        "Аутсорс: %9 -> %10\n"
        "Майстри: %11 -> %12\n"
        "Каталог: %13 -> %14\n"
        "Планування: %15 -> %16\n"
        "Витрати: %17 -> %18\n"
        "Попереджень pre-audit: %19\n"
        "Попереджень verify: %20\n"
        "Помилок verify: %21\n\n"
        "Пакет звітів:\n%22\n\n"
        "Основні файли:\n"
        "- Summary: %23\n"
        "- Validation: %24\n"
        "- Conflicts JSON: %25\n"
        "- Conflicts CSV: %26\n"
        "- Import log: %27\n\n"
        "Перезапустіть застосунок і виконайте ручний smoke-test UI.")
        .arg(result.sourceOrderCount)
        .arg(result.importedOrderCount)
        .arg(result.sourceServiceCount)
        .arg(result.importedServiceCount)
        .arg(result.sourcePartCount)
        .arg(result.importedPartCount)
        .arg(result.sourceServiceMasterCount)
        .arg(result.importedServiceMasterCount)
        .arg(result.sourceOutsourceCount)
        .arg(result.importedOutsourceCount)
        .arg(result.sourceMasterCount)
        .arg(result.importedMasterCount)
        .arg(result.sourceCatalogCount)
        .arg(result.importedCatalogCount)
        .arg(result.sourcePlanningCount)
        .arg(result.importedPlanningCount)
        .arg(result.sourceExpenseCount)
        .arg(result.importedExpenseCount)
        .arg(result.auditWarnings.size())
        .arg(result.verifyWarnings.size())
        .arg(result.verifyErrors.size())
        .arg(reportDirPath)
        .arg(result.summaryReportPath)
        .arg(result.validationReportPath)
        .arg(result.conflictsJsonPath)
        .arg(result.conflictsCsvPath)
        .arg(result.importLogPath);
}

QString LegacyImportUiSupport::buildBlockedDialogText(const LegacyImportAuditReport& report, const QString& reportDirPath)
{
    const QString reason = !report.errorMessages.isEmpty()
        ? report.errorMessages.constFirst()
        : QString::fromUtf8("execute mode заблокований pre-import audit-ом");
    return QString::fromUtf8(
        "Execute mode заблоковано.\n\n"
        "Причина: %1\n\n"
        "Перевірте preview report:\n%2")
        .arg(reason)
        .arg(reportDirPath);
}
