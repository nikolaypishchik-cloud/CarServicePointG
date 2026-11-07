#include "infrastructure/db/sqlite/ProfileConnectionHandoff.h"
#include "core/services/legacyimport/LegacyImportExecutionSupport.h"
#include "core/diagnostics/RuntimeLoggingControl.h"
#include "core/backup/DataProtectionCoordinator.h"
#include "core/services/storage/MigrationRestoreJournal.h"

#include "runtime/AppRuntime.h"
#include "core/services/legacyimport/LegacyImportAuditSupport.h"
#include "core/services/legacyimport/LegacyImportSourceResolver.h"
#include "core/services/legacyimport/LegacyImportJournalSupport.h"
#include "core/services/legacyimport/LegacyImportSafetySupport.h"
#include "core/services/legacyimport/LegacyImportVerifySupport.h"
#include "core/services/legacyimport/LegacyImportSidecarSettingsSupport.h"
#include "core/services/legacyimport/LegacyImportPolicySupport.h"
#include "core/services/legacyimport/LegacyImportReportSupport.h"
#include "core/services/legacyimport/MigrationPreflightValidator.h"
#include "core/services/legacyimport/TargetConsistencyGate.h"
#include "core/services/legacyimport/MigrationPromoteCoordinator.h"
#include "core/services/legacyimport/MigrationVerifyCoordinator.h"
#include "core/services/legacyimport/MigrationStagingCoordinator.h"
#include "core/services/legacyimport/MigrationRunJournal.h"
#include "infrastructure/db/sqlite/Db.h"
#include "infrastructure/db/sqlite/RuntimeDbPreflightValidator.h"
#include "infrastructure/security/EncryptedStorage.h"

#include <QDebug>
#include <QDir>
#include <QSqlQuery>
#include <QSqlError>

namespace {

int sourceEntityCount(const LegacyImportAuditReport& report, const QString& sourceTable)
{
    for (const LegacyImportEntityAudit& entity : report.entities) {
        if (entity.sourceTable == sourceTable) return qMax(0, entity.rowCount);
    }
    return 0;
}

int sourceOrderCount(const LegacyImportAuditReport& report)
{
    const int exact = sourceEntityCount(report, QStringLiteral("work_orders"));
    return exact > 0 ? exact : qMax(0, report.orderCount);
}

int sourceServiceCount(const LegacyImportAuditReport& report)
{
    const int exact = sourceEntityCount(report, QStringLiteral("work_order_services"));
    return exact > 0 ? exact : qMax(0, report.serviceCount);
}

int sourcePartCount(const LegacyImportAuditReport& report)
{
    const int exact = sourceEntityCount(report, QStringLiteral("work_order_parts"));
    return exact > 0 ? exact : qMax(0, report.partCount);
}

int scalarCount(const QString& sql)
{
    const QSqlDatabase db = Database::db();
    if (!db.isOpen()) return 0;
    QSqlQuery q(db);
    if (!q.exec(sql) || !q.next()) return 0;
    return q.value(0).toInt();
}


int execSqlAndCountRows(QSqlDatabase db, const QString& sql, QString* errorText = nullptr)
{
    if (!db.isValid() || !db.isOpen()) {
        if (errorText) *errorText = QStringLiteral("DB is not open");
        return -1;
    }
    QSqlQuery q(db);
    if (!q.exec(sql)) {
        if (errorText) *errorText = q.lastError().text().trimmed();
        return -1;
    }
    return q.numRowsAffected();
}

bool runLegacyImportP1Repairs(QStringList* warnings)
{
    QSqlDatabase db = Database::db();
    if (!db.isValid() || (!db.isOpen() && !db.open())) {
        if (warnings) warnings->push_back(QStringLiteral("P1 repair skipped: workspace DB is not open"));
        return false;
    }

    bool changed = false;
    QStringList localWarnings;

    const QString repairWorksSumSql = QStringLiteral(
        "UPDATE work_orders "
        "SET works_sum = COALESCE(("
        "  SELECT ROUND(SUM(COALESCE(s.amount, COALESCE(s.qty,1)*COALESCE(s.price,0))), 2) "
        "  FROM work_order_services s WHERE s.order_number=work_orders.number"
        "), 0) "
        "WHERE ABS(COALESCE(works_sum,0) - COALESCE(("
        "  SELECT ROUND(SUM(COALESCE(s.amount, COALESCE(s.qty,1)*COALESCE(s.price,0))), 2) "
        "  FROM work_order_services s WHERE s.order_number=work_orders.number"
        "), 0)) > 0.009");
    QString repairError;
    const int repairedWorksSum = execSqlAndCountRows(db, repairWorksSumSql, &repairError);
    if (repairedWorksSum < 0) {
        localWarnings.push_back(QStringLiteral("P1 repair works_sum skipped: %1").arg(repairError));
    } else if (repairedWorksSum > 0) {
        changed = true;
        if (RuntimeLoggingControl::deepDebugEnabled()) qInfo() << "Legacy import P1: repaired works_sum mismatches" << repairedWorksSum;
    }

    const QString normalizeOutsourceSql = QStringLiteral(
        "UPDATE outsource_repairs "
        "SET service_id = 0 "
        "WHERE COALESCE(service_id,0)<>0 AND NOT EXISTS ("
        "  SELECT 1 FROM work_order_services s "
        "  WHERE s.id=outsource_repairs.service_id "
        "    AND COALESCE(s.order_number,0)=COALESCE(outsource_repairs.order_number,0)"
        ")");
    repairError.clear();
    const int normalizedOutsource = execSqlAndCountRows(db, normalizeOutsourceSql, &repairError);
    if (normalizedOutsource < 0) {
        localWarnings.push_back(QStringLiteral("P1 repair orphan outsource skipped: %1").arg(repairError));
    } else if (normalizedOutsource > 0) {
        changed = true;
        if (RuntimeLoggingControl::deepDebugEnabled()) qInfo() << "Legacy import P1: normalized orphan outsource links" << normalizedOutsource;
    }

    if (changed) {
        QString refreshError;
        if (!Database::runStartupRegistryRefresh(&refreshError)) {
            localWarnings.push_back(QStringLiteral("P1 repair registry/router refresh warning: %1").arg(refreshError));
        }
    }

    if (warnings) warnings->append(localWarnings);
    return changed;
}
void emitProgress(const LegacyImportProgressCallback& cb,
                  const QString& stageKey,
                  const QString& title,
                  const QString& status,
                  int stepCurrent,
                  int stepTotal,
                  int mainPercent,
                  int processedCount = 0,
                  int createdCount = 0,
                  int updatedCount = 0,
                  int skippedCount = 0,
                  int errorCount = 0,
                  bool indeterminate = false,
                  bool finished = false,
                  const QString& metricsCaption = QString(),
                  int orderCount = 0,
                  int serviceCount = 0,
                  int partCount = 0,
                  int warningCount = 0)
{
    if (!cb) return;
    LegacyImportProgressState s;
    s.stageKey = stageKey;
    s.title = title;
    s.status = status;
    s.metricsCaption = metricsCaption;
    s.stepCurrent = stepCurrent;
    s.stepTotal = stepTotal;
    s.mainPercent = mainPercent;
    s.processedCount = processedCount;
    s.createdCount = createdCount;
    s.updatedCount = updatedCount;
    s.skippedCount = skippedCount;
    s.orderCount = orderCount;
    s.serviceCount = serviceCount;
    s.partCount = partCount;
    s.warningCount = warningCount;
    s.errorCount = errorCount;
    s.indeterminate = indeterminate;
    s.finished = finished;
    cb(s);
}

} // namespace

bool LegacyImportExecutionSupport::runPreview(const QString& sourcePath,
                                              LegacyImportAuditReport* report,
                                              QString* errorText,
                                              const LegacyImportProgressCallback& progress)
{
    emitProgress(progress, QStringLiteral("preview_prepare"), QString::fromUtf8("Імпорт старої БД"), QString::fromUtf8("Підготовка до перевірки legacy БД..."), 0, 3, 0, 0, 0, 0, 0, 0, true, false);
    LegacyImportBundle bundle;
    QString resolveError;
    if (!LegacyImportSourceResolver::resolve(sourcePath, &bundle, &resolveError)) {
        emitProgress(progress, QStringLiteral("preview_error"), QString::fromUtf8("Помилка імпорту"), resolveError, 0, 3, 0, 0, 0, 0, 0, 1, false, true);
        if (errorText) *errorText = resolveError;
        return false;
    }

    emitProgress(progress, QStringLiteral("preview_open"), QString::fromUtf8("Імпорт старої БД"), QString::fromUtf8("Відкриття старої БД і аналіз структури..."), 1, 3, 20, 0, 0, 0, 0, 0, true, false);
    const bool ok = LegacyImportAuditSupport::runAudit(bundle, report, errorText);
    emitProgress(progress, QStringLiteral("preview_reports"), QString::fromUtf8("Імпорт старої БД"), QString::fromUtf8("Формування preview-звітів..."), 2, 3, 70, report ? report->orderCount + report->serviceCount + report->partCount : 0, 0, 0, 0, ok ? 0 : 1, true, false,
                 QString::fromUtf8("Виявлено у legacy БД"),
                 report ? qMax(0, report->orderCount) : 0,
                 report ? qMax(0, report->serviceCount) : 0,
                 report ? qMax(0, report->partCount) : 0,
                 report ? report->warningMessages.size() : 0);
    if (report) {
        QString reportDir;
        QString reportError;
        if (LegacyImportReportSupport::writePreviewArtifacts(*report, &reportDir, &reportError)) {
            report->reportDirPath = reportDir;
            report->markdownReportPath = QDir(reportDir).filePath(QStringLiteral("preview_report.md"));
            report->jsonReportPath = QDir(reportDir).filePath(QStringLiteral("preview_report.json"));
        } else {
            qWarning() << "Legacy preview report export failed:" << reportError;
        }
    }
    LegacyImportSourceResolver::cleanup(&bundle);
    return ok;
}

bool LegacyImportExecutionSupport::runExecute(const QString& sourcePath,
                                              LegacyMigrationResult* result,
                                              QString* errorText,
                                              const LegacyImportProgressCallback& progress)
{
    auto operation = DataProtectionCoordinator::tryEnter(
        DataProtectionCoordinator::Operation::DbMigration, QStringLiteral("profile migration"), errorText);
    if (!operation.isActive()) return false;
    ProfileConnectionHandoff handoff;
    bool restoreFailed = false;
    QString publicationSnapshot;
    const auto execute = [&]() -> bool {

    emitProgress(progress, QStringLiteral("execute_prepare"), QString::fromUtf8("Імпорт старої БД"), QString::fromUtf8("Підготовка до імпорту..."), 0, 8, 0, 0, 0, 0, 0, 0, true, false);
    LegacyImportBundle bundle;
    QString resolveError;
    if (!LegacyImportSourceResolver::resolve(sourcePath, &bundle, &resolveError)) {
        emitProgress(progress, QStringLiteral("execute_error"), QString::fromUtf8("Помилка імпорту"), resolveError, 0, 8, 0, 0, 0, 0, 0, 1, false, true);
        if (errorText) *errorText = resolveError;
        return false;
    }

    emitProgress(progress, QStringLiteral("execute_source"), QString::fromUtf8("Імпорт старої БД"), QString::fromUtf8("Відкриття старої БД..."), 1, 8, 8, 0, 0, 0, 0, 0, true, false);
    LegacyImportAuditReport auditReport;
    QString auditError;
    emitProgress(progress, QStringLiteral("execute_audit"), QString::fromUtf8("Імпорт старої БД"), QString::fromUtf8("Аналіз структури і валідація джерела..."), 2, 8, 16, 0, 0, 0, 0, 0, true, false);
    if (!LegacyImportAuditSupport::runAudit(bundle, &auditReport, &auditError)) {
        qWarning() << "Legacy pre-import audit failed for" << sourcePath << auditError;
        LegacyImportSourceResolver::cleanup(&bundle);
        emitProgress(progress, QStringLiteral("execute_error"), QString::fromUtf8("Помилка імпорту"), auditError, 2, 8, 16, 0, 0, 0, 0, 1, false, true);
        if (errorText) *errorText = auditError;
        return false;
    }

    const int auditOrders = sourceOrderCount(auditReport);
    const int auditServices = sourceServiceCount(auditReport);
    const int auditParts = sourcePartCount(auditReport);
    const int auditTotalEntities = auditOrders + auditServices + auditParts;

    emitProgress(progress, QStringLiteral("execute_policy"), QString::fromUtf8("Імпорт старої БД"), QString::fromUtf8("Перевірка правил міграції..."), 3, 8, 24, auditTotalEntities, 0, 0, 0, 0, true, false,
                 QString::fromUtf8("Виявлено у legacy БД"), auditOrders, auditServices, auditParts, auditReport.warningMessages.size());
    QStringList policyWarnings;
    QString policyError;
    if (!LegacyImportPolicySupport::validateExecutePolicy(bundle, auditReport, &policyError, &policyWarnings)) {
        LegacyImportSourceResolver::cleanup(&bundle);
        emitProgress(progress, QStringLiteral("execute_error"), QString::fromUtf8("Помилка імпорту"), policyError, 3, 8, 24, auditTotalEntities, 0, 0, 0, 1, false, true,
                 QString::fromUtf8("Виявлено у legacy БД"), auditOrders, auditServices, auditParts, policyWarnings.size());
        if (errorText) *errorText = policyError;
        return false;
    }

    QString preflightError;
    if (!MigrationPreflightValidator::validateSourceBundle(bundle, auditReport, &preflightError)) {
        LegacyImportSourceResolver::cleanup(&bundle);
        emitProgress(progress, QStringLiteral("execute_error"), QString::fromUtf8("Помилка імпорту"), preflightError, 3, 8, 24, auditTotalEntities, 0, 0, 0, 1, false, true,
                 QString::fromUtf8("Виявлено у legacy БД"), auditOrders, auditServices, auditParts, policyWarnings.size());
        if (errorText) *errorText = preflightError;
        return false;
    }

    if (!LegacyImportJournalSupport::ensureCanImportFingerprint(auditReport.sourceFingerprint, errorText)) {
        LegacyImportSourceResolver::cleanup(&bundle);
        return false;
    }

    LegacyImportRunContext runContext;
    QString journalError;
    if (!MigrationRunJournal::begin(bundle, auditReport, &runContext, &journalError)) {
        LegacyImportSourceResolver::cleanup(&bundle);
        if (errorText) *errorText = journalError;
        return false;
    }

    if (RuntimeLoggingControl::deepDebugEnabled()) qInfo() << "Legacy import source resolved:" << auditReport.sourceKind << auditReport.sourceDbPath;
    MigrationRunJournal::info(runContext, QStringLiteral("LEGACY_IMPORT_SOURCE_AUDIT_BEGIN"),
                                        QStringLiteral("LEGACY_IMPORT SOURCE AUDIT BEGIN"));
    MigrationRunJournal::info(runContext, QStringLiteral("LEGACY_IMPORT_MODE_DETECTED"),
                                        QStringLiteral("LEGACY_IMPORT MODE DETECTED | kind=%1 | source=%2")
                                            .arg(auditReport.sourceKind, auditReport.sourceDbPath));
    MigrationRunJournal::info(runContext, QStringLiteral("LEGACY_IMPORT_SOURCE_AUDIT_OK"),
                                        QStringLiteral("LEGACY_IMPORT SOURCE AUDIT OK | orders=%1 | services=%2 | parts=%3")
                                            .arg(auditReport.orderCount)
                                            .arg(auditReport.serviceCount)
                                            .arg(auditReport.partCount));
    for (const QString& info : auditReport.infoMessages) { if (RuntimeLoggingControl::deepDebugEnabled()) qInfo() << "Legacy preview info:" << info; MigrationRunJournal::info(runContext, QStringLiteral("audit"), info); }
    for (const QString& warning : auditReport.warningMessages) { qWarning() << "Legacy pre-import warning:" << warning; MigrationRunJournal::warning(runContext, QStringLiteral("audit"), warning); }
    for (const QString& warning : policyWarnings) { qWarning() << "Legacy policy warning:" << warning; MigrationRunJournal::warning(runContext, QStringLiteral("policy"), warning); }

    auto failAndRestore = [&](const QString& stage, const QString& message, const LegacyImportSafetySnapshot& snapshot) -> bool {
        MigrationRunJournal::stageFailed(runContext, stage, message);
        LegacyImportSafetySupport::quarantineCurrentArtifacts(snapshot, stage);
        QString restoreError;
        Database::close();
        if (snapshot.created && !LegacyImportSafetySupport::restoreSnapshot(snapshot, &restoreError)) {
            restoreFailed = true;
            MigrationRunJournal::finishFailure(runContext, QStringLiteral("restore_snapshot"), restoreError);
            if (errorText) *errorText = QStringLiteral("%1 | restore failed: %2").arg(message, restoreError);
            LegacyImportSourceResolver::cleanup(&bundle);
            return false;
        }
        MigrationRunJournal::finishFailure(runContext, stage, message);
        LegacyImportSourceResolver::cleanup(&bundle);
        if (errorText) *errorText = message;
        return false;
    };

    MigrationRunJournal::info(runContext, QStringLiteral("LEGACY_IMPORT_REAL_RUN_BEGIN"),
                                        QStringLiteral("LEGACY_IMPORT REAL RUN BEGIN | source=%1 | target=%2")
                                            .arg(auditReport.sourceDbPath, AppRuntime::dbFilePath()));

    emitProgress(progress, QStringLiteral("execute_snapshot"), QString::fromUtf8("Імпорт старої БД"), QString::fromUtf8("Створення rollback-копії поточної БД..."), 4, 8, 36, auditTotalEntities, 0, 0, 0, 0, true, false,
                 QString::fromUtf8("Виявлено у legacy БД"), auditOrders, auditServices, auditParts, auditReport.warningMessages.size() + policyWarnings.size());
    if (!handoff.closeMain(errorText)) {
        MigrationRunJournal::finishFailure(runContext, QStringLiteral("snapshot"), errorText ? *errorText : QStringLiteral("Profile handoff failed"));
        LegacyImportSourceResolver::cleanup(&bundle);
        return false;
    }
    Database::close();

    LegacyImportSafetySnapshot snapshot;
    QString snapshotError;
    if (!LegacyImportSafetySupport::createSnapshot(&snapshot, &snapshotError)) {
        MigrationRunJournal::finishFailure(runContext, QStringLiteral("snapshot"), snapshotError);
        LegacyImportSourceResolver::cleanup(&bundle);
        if (errorText) *errorText = snapshotError;
        return false;
    }
    if (!MigrationRunJournal::attachSnapshot(runContext, snapshot, &journalError)) {
        return failAndRestore(QStringLiteral("snapshot_journal"), journalError, snapshot);
    }

    if (!MigrationRestoreJournal::beginForSnapshot(snapshot.snapshotDirPath,&journalError)) {
        return failAndRestore(QStringLiteral("publication_intent"),journalError,snapshot);
    }

    publicationSnapshot=snapshot.snapshotDirPath;
    QString importError;
    emitProgress(progress, QStringLiteral("execute_import"), QString::fromUtf8("Імпорт старої БД"), QString::fromUtf8("Перенесення нарядів, робіт і запчастин у сховище..."), 5, 8, 52, auditTotalEntities, 0, 0, 0, 0, true, false,
                 QString::fromUtf8("Виявлено у legacy БД"), auditOrders, auditServices, auditParts, auditReport.warningMessages.size() + policyWarnings.size());
    if (!MigrationStagingCoordinator::importSourceIntoIsolatedTarget(bundle, runContext, &importError)) {
        return failAndRestore(QStringLiteral("import_archive"), importError, snapshot);
    }
    MigrationRunJournal::info(runContext, QStringLiteral("IMPORT_ARCHIVE_OK"), QStringLiteral("Legacy plaintext archive imported into sealed storage"));

    QString reopenError;
    emitProgress(progress, QStringLiteral("execute_reopen"), QString::fromUtf8("Імпорт старої БД"), QString::fromUtf8("Підготовка застосунку після імпорту..."), 6, 8, 68, auditTotalEntities, 0, 0, 0, 0, true, false,
                 QString::fromUtf8("Виявлено у legacy БД"), auditOrders, auditServices, auditParts, auditReport.warningMessages.size() + policyWarnings.size());
    if (!MigrationPromoteCoordinator::reopenProjectedRuntime(runContext, snapshot.snapshotDirPath, &reopenError)) {
        return failAndRestore(QStringLiteral("post_import_startup"),
                              QStringLiteral("Не вдалося повністю підготувати застосунок після імпорту legacy БД: %1").arg(reopenError),
                              snapshot);
    }

    const int postReopenOrders = scalarCount(QStringLiteral("SELECT COUNT(*) FROM work_orders"));
    const int postReopenServices = scalarCount(QStringLiteral("SELECT COUNT(*) FROM work_order_services"));
    const int postReopenParts = scalarCount(QStringLiteral("SELECT COUNT(*) FROM work_order_parts"));
    emitProgress(progress,
                 QStringLiteral("execute_reopen_ready"),
                 QString::fromUtf8("Імпорт старої БД"),
                 QString::fromUtf8("Підготовку runtime завершено; дані legacy вже відкриті у новій БД..."),
                 6,
                 8,
                 76,
                 postReopenOrders + postReopenServices + postReopenParts,
                 postReopenOrders + postReopenServices + postReopenParts,
                 0,
                 0,
                 0,
                 false,
                 false,
                 QString::fromUtf8("Вже відкрито у новій БД"),
                 postReopenOrders,
                 postReopenServices,
                 postReopenParts,
                 auditReport.warningMessages.size() + policyWarnings.size());

    LegacyImportSidecarApplyResult sidecarResult;
    QString sidecarError;
    emitProgress(progress, QStringLiteral("execute_sidecars"), QString::fromUtf8("Імпорт старої БД"), QString::fromUtf8("Оновлення каталогу і сумісних sidecar-даних..."), 7, 8, 82, postReopenOrders + postReopenServices + postReopenParts, postReopenOrders + postReopenServices + postReopenParts, 0, 0, 0, true, false,
                 QString::fromUtf8("Вже відкрито у новій БД"), postReopenOrders, postReopenServices, postReopenParts, auditReport.warningMessages.size() + policyWarnings.size());
    if (!LegacyImportSidecarSettingsSupport::applyCompatibleSidecars(bundle, &sidecarResult, &sidecarError)) {
        return failAndRestore(QStringLiteral("apply_sidecars"),
                              QStringLiteral("Не вдалося застосувати сумісні legacy sidecar-файли: %1").arg(sidecarError),
                              snapshot);
    }
    for (const QString& info : sidecarResult.infoMessages) MigrationRunJournal::info(runContext, QStringLiteral("sidecars"), info);
    for (const QString& warning : sidecarResult.warningMessages) MigrationRunJournal::warning(runContext, QStringLiteral("sidecars"), warning);

    emitProgress(progress,
                 QStringLiteral("execute_sidecars_ready"),
                 QString::fromUtf8("Імпорт старої БД"),
                 QString::fromUtf8("Каталог і сумісні sidecar-дані оновлено..."),
                 7,
                 8,
                 86,
                 scalarCount(QStringLiteral("SELECT COUNT(*) FROM work_orders"))
                    + scalarCount(QStringLiteral("SELECT COUNT(*) FROM work_order_services"))
                    + scalarCount(QStringLiteral("SELECT COUNT(*) FROM work_order_parts")),
                 scalarCount(QStringLiteral("SELECT COUNT(*) FROM parts_catalog")),
                 0,
                 0,
                 0,
                 false,
                 false,
                 QString::fromUtf8("Вже відкрито у новій БД"),
                 scalarCount(QStringLiteral("SELECT COUNT(*) FROM work_orders")),
                 scalarCount(QStringLiteral("SELECT COUNT(*) FROM work_order_services")),
                 scalarCount(QStringLiteral("SELECT COUNT(*) FROM work_order_parts")),
                 auditReport.warningMessages.size() + policyWarnings.size() + sidecarResult.warningMessages.size());

    MigrationRunJournal::info(runContext, QStringLiteral("LEGACY_IMPORT_POSTCHECK_BEGIN"),
                                        QStringLiteral("LEGACY_IMPORT POSTCHECK BEGIN"));

    CatalogImportReport autoCatalogReport;
    QString autoCatalogError;
    if (sourceEntityCount(auditReport, QStringLiteral("work_order_parts")) > 0) {
        if (Database::importCatalogFromOrders(&autoCatalogReport, &autoCatalogError)) {
            MigrationRunJournal::info(
                runContext,
                QStringLiteral("catalog_auto_import"),
                QStringLiteral("AUTO CATALOG IMPORT OK | processed=%1 | eligible=%2 | added=%3 | updated=%4 | skipped=%5 | oil=%6 | errors=%7")
                    .arg(autoCatalogReport.processedCount)
                    .arg(autoCatalogReport.eligibleCount)
                    .arg(autoCatalogReport.addedCount)
                    .arg(autoCatalogReport.updatedCount)
                    .arg(autoCatalogReport.skippedCount)
                    .arg(autoCatalogReport.skippedOilCount)
                    .arg(autoCatalogReport.errorCount));
        } else if (!autoCatalogError.trimmed().isEmpty()) {
            MigrationRunJournal::warning(runContext,
                                                   QStringLiteral("catalog_auto_import"),
                                                   QStringLiteral("AUTO CATALOG IMPORT SKIPPED: %1").arg(autoCatalogError));
        }
    }

    QStringList p1RepairWarnings;
    const bool p1Repaired = runLegacyImportP1Repairs(&p1RepairWarnings);
    for (const QString& warning : p1RepairWarnings) {
        MigrationRunJournal::warning(runContext, QStringLiteral("post_import_repair"), warning);
    }
    if (p1Repaired) {
        MigrationRunJournal::info(runContext, QStringLiteral("post_import_repair"), QStringLiteral("P1 post-import repairs applied before verify"));
    }

    LegacyImportVerifyReport verifyReport;
    QString verifyError;
    emitProgress(progress, QStringLiteral("execute_verify"), QString::fromUtf8("Імпорт старої БД"), QString::fromUtf8("Перевірка результату і фіналізація..."), 8, 8, 92, postReopenOrders + postReopenServices + postReopenParts, postReopenOrders + postReopenServices + postReopenParts, 0, 0, 0, true, false,
                 QString::fromUtf8("Вже відкрито у новій БД"), postReopenOrders, postReopenServices, postReopenParts, auditReport.warningMessages.size() + policyWarnings.size() + sidecarResult.warningMessages.size());
    if (!MigrationVerifyCoordinator::verifyCurrentStagingTarget(bundle, auditReport, &verifyReport, &verifyError)) {
        return failAndRestore(QStringLiteral("verify"), verifyError, snapshot);
    }
    for (const QString& info : verifyReport.infoMessages) MigrationRunJournal::info(runContext, QStringLiteral("verify"), info);
    for (const QString& warning : verifyReport.warningMessages) MigrationRunJournal::warning(runContext, QStringLiteral("verify"), warning);

    QMap<QString,int> imported;
    QString countError;
    {
        QSqlQuery counts(Database::db());
        counts.prepare("SELECT source_table,COUNT(*) FROM migration_record_map WHERE source_identity=? GROUP BY source_table");
        counts.addBindValue(runContext.sourceFingerprint);
        if (!counts.exec()) countError=counts.lastError().text();
        while (counts.next()) imported.insert(counts.value(0).toString(),counts.value(1).toInt());
        if (counts.lastError().isValid()) countError=counts.lastError().text();
    }
    if (!countError.isEmpty()) return failAndRestore(QStringLiteral("post_import_counts"), countError,snapshot);
    for (const auto& entity : auditReport.entities) {
        if (imported.value(entity.sourceTable) != entity.rowCount && entity.present)
            return failAndRestore(QStringLiteral("post_import_counts"), QStringLiteral("Source correspondence count mismatch for %1").arg(entity.sourceTable),snapshot);
    }

    LegacyMigrationResult localResult;
    localResult.sourceOrderCount = auditOrders;
    localResult.importedOrderCount = imported.value(QStringLiteral("work_orders"));
    localResult.sourceServiceCount = auditServices;
    localResult.importedServiceCount = imported.value(QStringLiteral("work_order_services"));
    localResult.sourcePartCount = auditParts;
    localResult.importedPartCount = imported.value(QStringLiteral("work_order_parts"));
    localResult.sourceServiceMasterCount = sourceEntityCount(auditReport, QStringLiteral("service_masters"));
    localResult.importedServiceMasterCount = imported.value(QStringLiteral("service_masters"));
    localResult.sourceOutsourceCount = sourceEntityCount(auditReport, QStringLiteral("outsource_repairs"));
    localResult.importedOutsourceCount = imported.value(QStringLiteral("outsource_repairs"));
    localResult.sourceMasterCount = sourceEntityCount(auditReport, QStringLiteral("masters"));
    localResult.importedMasterCount = imported.value(QStringLiteral("masters"));
    localResult.sourceCatalogCount = sourceEntityCount(auditReport, QStringLiteral("parts_catalog"));
    localResult.importedCatalogCount = imported.value(QStringLiteral("parts_catalog"));
    localResult.sourcePlanningCount = sourceEntityCount(auditReport, QStringLiteral("planning_calendar"));
    localResult.importedPlanningCount = imported.value(QStringLiteral("planning_calendar"));
    localResult.sourceExpenseCount = sourceEntityCount(auditReport, QStringLiteral("expenses"));
    localResult.importedExpenseCount = imported.value(QStringLiteral("expenses"));
    localResult.updatedCount = 0;
    localResult.skippedCount = 0;
    localResult.conflictCount = auditReport.findings.size() + verifyReport.warningMessages.size() + verifyReport.errorMessages.size();
    localResult.criticalErrorCount = verifyReport.errorMessages.size();
    localResult.sourceKind = auditReport.sourceKind;
    localResult.sourceDbPath = auditReport.sourceDbPath;
    localResult.targetDbPath = AppRuntime::dbFilePath();
    localResult.snapshotDirPath = snapshot.snapshotDirPath;
    localResult.auditWarnings = auditReport.warningMessages;
    localResult.verifyWarnings = verifyReport.warningMessages;
    localResult.verifyWarnings.append(p1RepairWarnings);
    localResult.verifyErrors = verifyReport.errorMessages;

    QString reportDir;
    QString reportError;
    if (!LegacyImportReportSupport::writeExecuteArtifacts(bundle, runContext, auditReport, localResult, verifyReport, &reportDir, &reportError)) {
        return failAndRestore(QStringLiteral("report_export"), reportError, snapshot);
    }
    if (!reportDir.trimmed().isEmpty()) {
        localResult.reportDirPath = reportDir;
        localResult.summaryReportPath = QDir(reportDir).filePath(QStringLiteral("MigrationSummary.md"));
        localResult.summaryTextPath = QDir(reportDir).filePath(QStringLiteral("MigrationSummary.txt"));
        localResult.validationReportPath = QDir(reportDir).filePath(QStringLiteral("MigrationValidationReport.md"));
        localResult.conflictsJsonPath = QDir(reportDir).filePath(QStringLiteral("MigrationConflicts.json"));
        localResult.conflictsCsvPath = QDir(reportDir).filePath(QStringLiteral("MigrationConflicts.csv"));
        localResult.developerReportPath = QDir(reportDir).filePath(QStringLiteral("developer_handoff.md"));
        localResult.userGuidePath = QDir(reportDir).filePath(QStringLiteral("user_instruction.md"));
        localResult.acceptanceReportPath = QDir(reportDir).filePath(QStringLiteral("acceptance_report.md"));
        localResult.testScenarioReportPath = QDir(reportDir).filePath(QStringLiteral("test_scenarios.md"));
    }
    localResult.importLogPath = runContext.logFilePath;

    MigrationRunJournal::info(runContext, QStringLiteral("IMPORT_ENTITY_WORK_ORDERS_BEGIN"), QStringLiteral("IMPORT ENTITY work_orders BEGIN"));
    MigrationRunJournal::info(runContext, QStringLiteral("IMPORT_ENTITY_WORK_ORDERS_OK"),
                                        QStringLiteral("IMPORT ENTITY work_orders OK | source=%1 | target=%2")
                                            .arg(localResult.sourceOrderCount)
                                            .arg(localResult.importedOrderCount));
    MigrationRunJournal::info(runContext, QStringLiteral("IMPORT_ENTITY_WORK_ORDER_SERVICES_BEGIN"), QStringLiteral("IMPORT ENTITY work_order_services BEGIN"));
    MigrationRunJournal::info(runContext, QStringLiteral("IMPORT_ENTITY_WORK_ORDER_SERVICES_OK"),
                                        QStringLiteral("IMPORT ENTITY work_order_services OK | source=%1 | target=%2")
                                            .arg(localResult.sourceServiceCount)
                                            .arg(localResult.importedServiceCount));
    MigrationRunJournal::info(runContext, QStringLiteral("IMPORT_ENTITY_WORK_ORDER_PARTS_BEGIN"), QStringLiteral("IMPORT ENTITY work_order_parts BEGIN"));
    MigrationRunJournal::info(runContext, QStringLiteral("IMPORT_ENTITY_WORK_ORDER_PARTS_OK"),
                                        QStringLiteral("IMPORT ENTITY work_order_parts OK | source=%1 | target=%2")
                                            .arg(localResult.sourcePartCount)
                                            .arg(localResult.importedPartCount));
    MigrationRunJournal::info(runContext, QStringLiteral("IMPORT_ENTITY_MASTERS_BEGIN"), QStringLiteral("IMPORT ENTITY masters BEGIN"));
    MigrationRunJournal::info(runContext, QStringLiteral("IMPORT_ENTITY_MASTERS_OK"),
                                        QStringLiteral("IMPORT ENTITY masters OK | source=%1 | target=%2")
                                            .arg(localResult.sourceMasterCount)
                                            .arg(localResult.importedMasterCount));
    MigrationRunJournal::info(runContext, QStringLiteral("IMPORT_ENTITY_SERVICE_MASTERS_BEGIN"), QStringLiteral("IMPORT ENTITY service_masters BEGIN"));
    MigrationRunJournal::info(runContext, QStringLiteral("IMPORT_ENTITY_SERVICE_MASTERS_OK"),
                                        QStringLiteral("IMPORT ENTITY service_masters OK | source=%1 | target=%2")
                                            .arg(localResult.sourceServiceMasterCount)
                                            .arg(localResult.importedServiceMasterCount));
    MigrationRunJournal::info(runContext, QStringLiteral("IMPORT_ENTITY_OUTSOURCE_REPAIRS_BEGIN"), QStringLiteral("IMPORT ENTITY outsource_repairs BEGIN"));
    MigrationRunJournal::info(runContext, QStringLiteral("IMPORT_ENTITY_OUTSOURCE_REPAIRS_OK"),
                                        QStringLiteral("IMPORT ENTITY outsource_repairs OK | source=%1 | target=%2")
                                            .arg(localResult.sourceOutsourceCount)
                                            .arg(localResult.importedOutsourceCount));
    MigrationRunJournal::info(runContext, QStringLiteral("IMPORT_ENTITY_PARTS_CATALOG_BEGIN"), QStringLiteral("IMPORT ENTITY parts_catalog BEGIN"));
    MigrationRunJournal::info(runContext, QStringLiteral("IMPORT_ENTITY_PARTS_CATALOG_OK"),
                                        QStringLiteral("IMPORT ENTITY parts_catalog OK | source=%1 | target=%2")
                                            .arg(localResult.sourceCatalogCount)
                                            .arg(localResult.importedCatalogCount));
    MigrationRunJournal::info(runContext, QStringLiteral("IMPORT_ENTITY_PLANNING_CALENDAR_BEGIN"), QStringLiteral("IMPORT ENTITY planning_calendar BEGIN"));
    MigrationRunJournal::info(runContext, QStringLiteral("IMPORT_ENTITY_PLANNING_CALENDAR_OK"),
                                        QStringLiteral("IMPORT ENTITY planning_calendar OK | source=%1 | target=%2")
                                            .arg(localResult.sourcePlanningCount)
                                            .arg(localResult.importedPlanningCount));
    MigrationRunJournal::info(runContext, QStringLiteral("IMPORT_ENTITY_EXPENSES_BEGIN"), QStringLiteral("IMPORT ENTITY expenses BEGIN"));
    MigrationRunJournal::info(runContext, QStringLiteral("IMPORT_ENTITY_EXPENSES_OK"),
                                        QStringLiteral("IMPORT ENTITY expenses OK | source=%1 | target=%2")
                                            .arg(localResult.sourceExpenseCount)
                                            .arg(localResult.importedExpenseCount));
    if (!localResult.summaryReportPath.trimmed().isEmpty()) {
        MigrationRunJournal::info(runContext, QStringLiteral("LEGACY_IMPORT_SUMMARY_WRITTEN"),
                                            QStringLiteral("LEGACY_IMPORT SUMMARY WRITTEN | %1").arg(localResult.summaryReportPath));
    }
    if (!localResult.validationReportPath.trimmed().isEmpty()) {
        MigrationRunJournal::info(runContext, QStringLiteral("LEGACY_IMPORT_VALIDATION_REPORT_WRITTEN"),
                                            QStringLiteral("LEGACY_IMPORT VALIDATION REPORT WRITTEN | %1").arg(localResult.validationReportPath));
    }
    if (!localResult.conflictsJsonPath.trimmed().isEmpty() || !localResult.conflictsCsvPath.trimmed().isEmpty()) {
        MigrationRunJournal::info(runContext, QStringLiteral("LEGACY_IMPORT_CONFLICTS_REPORT_WRITTEN"),
                                            QStringLiteral("LEGACY_IMPORT CONFLICTS REPORT WRITTEN | json=%1 | csv=%2")
                                                .arg(localResult.conflictsJsonPath, localResult.conflictsCsvPath));
    }

    if (!MigrationRunJournal::finishSuccess(runContext, localResult, verifyReport, &journalError)) {
        return failAndRestore(QStringLiteral("completion_journal"), journalError, snapshot);
    }
    if (result) *result = localResult;
    emitProgress(progress,
                 verifyReport.success && verifyReport.errorMessages.isEmpty() ? QStringLiteral("execute_done") : QStringLiteral("execute_warning"),
                 verifyReport.success && verifyReport.errorMessages.isEmpty() ? QString::fromUtf8("Імпорт завершено") : QString::fromUtf8("Імпорт завершено з попередженнями"),
                 verifyReport.success && verifyReport.errorMessages.isEmpty() ? QString::fromUtf8("Міграцію legacy БД завершено.") : QString::fromUtf8("Міграцію завершено, але є попередження або помилки verify."),
                 8, 8, 100,
                 localResult.importedOrderCount + localResult.importedServiceCount + localResult.importedPartCount,
                 0,
                 localResult.updatedCount,
                 localResult.skippedCount,
                 localResult.criticalErrorCount,
                 false,
                 true,
                 QString::fromUtf8("Підсумок імпорту"),
                 localResult.importedOrderCount,
                 localResult.importedServiceCount,
                 localResult.importedPartCount,
                 localResult.conflictCount);
    LegacyImportSourceResolver::cleanup(&bundle);
    return true;
    };
    const bool ok = execute();
    if (restoreFailed) return false;
    QString reopenError;
    if (!handoff.reopenMain(&reopenError)) {
        if (errorText) *errorText += QStringLiteral(" | Main workspace reopen failed: ") + reopenError;
        return false;
    }
    // Commit only after verification, success journal and owner reconnection.
    // If interrupted earlier, normal startup restores the entire old generation.
    if(ok) {
        QString pending;
        if(!MigrationRestoreJournal::pending(&pending,errorText))return false;
        if(pending.isEmpty() || pending!=publicationSnapshot) {
            if(errorText)*errorText=QStringLiteral("Publication rollback intent changed before commit");
            return false;
        }
        if(!MigrationRestoreJournal::finish(errorText))return false;
    }
    return ok;
}

bool LegacyImportExecutionSupport::runVerify(const QString& sourcePath,
                                             LegacyImportVerifyReport* report,
                                             QString* errorText)
{
    LegacyImportBundle bundle;
    QString resolveError;
    if (!LegacyImportSourceResolver::resolve(sourcePath, &bundle, &resolveError)) {
        if (errorText) *errorText = resolveError;
        return false;
    }

    const bool ok = LegacyImportVerifySupport::runVerifyAgainstCurrentWorkspace(bundle, report, errorText);
    if (report) {
        QString reportDir;
        QString reportError;
        if (LegacyImportReportSupport::writeVerifyArtifacts(sourcePath, *report, &reportDir, &reportError)) {
            report->reportDirPath = reportDir;
            report->markdownReportPath = QDir(reportDir).filePath(QStringLiteral("verify_report.md"));
            report->jsonReportPath = QDir(reportDir).filePath(QStringLiteral("verify_report.json"));
        } else {
            qWarning() << "Legacy verify report export failed:" << reportError;
        }
    }
    LegacyImportSourceResolver::cleanup(&bundle);
    return ok;
}
