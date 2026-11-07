#include "core/services/legacyimport/LegacyImportReportSupport.h"

#include "runtime/AppRuntime.h"
#include "core/services/LegacyMigrationService.h"
#include "core/services/legacyimport/LegacyImportAcceptanceSupport.h"
#include "core/services/legacyimport/LegacyImportJournalSupport.h"
#include "core/services/legacyimport/LegacyImportScenarioSupport.h"
#include "core/services/legacyimport/LegacyImportTypes.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QStringConverter>

namespace {

struct ConflictRecord {
    QString entity;
    QString key;
    QString reason;
    QString decision;
    bool manualIntervention = false;
};

QString nowStamp()
{
    return QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd_HHmmss_zzz"));
}

bool writeText(const QString& path, const QString& text, QString* errorText)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        if (errorText) *errorText = QStringLiteral("Не вдалося записати файл %1: %2").arg(path, file.errorString());
        return false;
    }
    QTextStream ts(&file);
    ts.setEncoding(QStringConverter::Utf8);
    ts << text;
    return true;
}

QString summaryLine(const QString& name, const QString& value)
{
    return QStringLiteral("- **%1:** %2\n").arg(name, value);
}

QString csvEscape(QString value)
{
    value.replace(QStringLiteral("\""), QStringLiteral("\"\""));
    return QStringLiteral("\"%1\"").arg(value);
}

QJsonObject toJson(const LegacyImportAuditReport& report)
{
    QJsonObject root;
    root.insert(QStringLiteral("source_kind"), report.sourceKind);
    root.insert(QStringLiteral("source_path"), report.sourcePath);
    root.insert(QStringLiteral("source_db_path"), report.sourceDbPath);
    root.insert(QStringLiteral("source_fingerprint"), report.sourceFingerprint);
    root.insert(QStringLiteral("sqlite_integrity_ok"), report.sqliteIntegrityOk);
    root.insert(QStringLiteral("can_execute"), report.canExecute);
    root.insert(QStringLiteral("orders"), report.orderCount);
    root.insert(QStringLiteral("services"), report.serviceCount);
    root.insert(QStringLiteral("parts"), report.partCount);

    QJsonArray entities;
    for (const LegacyImportEntityAudit& entity : report.entities) {
        QJsonObject obj;
        obj.insert(QStringLiteral("entity_name"), entity.entityName);
        obj.insert(QStringLiteral("source_table"), entity.sourceTable);
        obj.insert(QStringLiteral("required"), entity.required);
        obj.insert(QStringLiteral("present"), entity.present);
        obj.insert(QStringLiteral("row_count"), entity.rowCount);
        QJsonArray missingColumns;
        for (const QString& col : entity.missingColumns) missingColumns.append(col);
        obj.insert(QStringLiteral("missing_columns"), missingColumns);
        entities.append(obj);
    }
    root.insert(QStringLiteral("entities"), entities);

    QJsonArray schemaMap;
    for (const LegacyImportEntitySchemaMap& entity : report.schemaMap) {
        QJsonObject obj;
        obj.insert(QStringLiteral("entity_name"), entity.entityName);
        obj.insert(QStringLiteral("source_table"), entity.sourceTable);
        obj.insert(QStringLiteral("target_table"), entity.targetTable);
        obj.insert(QStringLiteral("strategy"), entity.strategy);
        obj.insert(QStringLiteral("requires_normalization"), entity.requiresNormalization);
        QJsonArray notes;
        for (const QString& note : entity.notes) notes.append(note);
        obj.insert(QStringLiteral("notes"), notes);
        QJsonArray fieldMaps;
        for (const LegacyImportFieldMap& field : entity.fieldMaps) {
            QJsonObject mapObj;
            mapObj.insert(QStringLiteral("target_field"), field.targetField);
            QJsonArray candidates;
            for (const QString& c : field.sourceCandidates) candidates.append(c);
            mapObj.insert(QStringLiteral("source_candidates"), candidates);
            mapObj.insert(QStringLiteral("transform_rule"), field.transformRule);
            mapObj.insert(QStringLiteral("required"), field.required);
            fieldMaps.append(mapObj);
        }
        obj.insert(QStringLiteral("field_maps"), fieldMaps);
        schemaMap.append(obj);
    }
    root.insert(QStringLiteral("schema_map"), schemaMap);

    QJsonArray sidecars;
    for (const QString& item : report.detectedSidecars) sidecars.append(item);
    root.insert(QStringLiteral("sidecars"), sidecars);
    QJsonArray infos; for (const QString& s : report.infoMessages) infos.append(s); root.insert(QStringLiteral("info"), infos);
    QJsonArray warnings; for (const QString& s : report.warningMessages) warnings.append(s); root.insert(QStringLiteral("warnings"), warnings);
    QJsonArray errors; for (const QString& s : report.errorMessages) errors.append(s); root.insert(QStringLiteral("errors"), errors);
    QJsonArray findings;
    for (const LegacyImportFinding& finding : report.findings) {
        QJsonObject obj;
        obj.insert(QStringLiteral("severity"), finding.severity);
        obj.insert(QStringLiteral("code"), finding.code);
        obj.insert(QStringLiteral("entity"), finding.entityName);
        obj.insert(QStringLiteral("message"), finding.message);
        obj.insert(QStringLiteral("affected_count"), finding.affectedCount);
        obj.insert(QStringLiteral("blocks_execute"), finding.blocksExecute);
        findings.append(obj);
    }
    root.insert(QStringLiteral("findings"), findings);
    return root;
}

QJsonObject toJson(const LegacyImportVerifyReport& report)
{
    QJsonObject root;
    root.insert(QStringLiteral("ran"), report.ran);
    root.insert(QStringLiteral("success"), report.success);
    root.insert(QStringLiteral("source_db_path"), report.sourceDbPath);
    QJsonArray entities;
    for (const LegacyImportVerifyEntityResult& entity : report.entities) {
        QJsonObject obj;
        obj.insert(QStringLiteral("entity"), entity.entityName);
        obj.insert(QStringLiteral("source_count"), entity.sourceCount);
        obj.insert(QStringLiteral("target_count"), entity.targetCount);
        obj.insert(QStringLiteral("missing_count"), entity.missingCount);
        obj.insert(QStringLiteral("invalid_link_count"), entity.invalidLinkCount);
        obj.insert(QStringLiteral("ok"), entity.ok);
        QJsonArray msgs; for (const QString& s : entity.messages) msgs.append(s); obj.insert(QStringLiteral("messages"), msgs);
        entities.append(obj);
    }
    root.insert(QStringLiteral("entities"), entities);
    QJsonArray infos; for (const QString& s : report.infoMessages) infos.append(s); root.insert(QStringLiteral("info"), infos);
    QJsonArray warnings; for (const QString& s : report.warningMessages) warnings.append(s); root.insert(QStringLiteral("warnings"), warnings);
    QJsonArray errors; for (const QString& s : report.errorMessages) errors.append(s); root.insert(QStringLiteral("errors"), errors);
    return root;
}

QJsonObject resultToJson(const LegacyMigrationResult& result)
{
    QJsonObject root;
    root.insert(QStringLiteral("source_kind"), result.sourceKind);
    root.insert(QStringLiteral("source_db_path"), result.sourceDbPath);
    root.insert(QStringLiteral("target_db_path"), result.targetDbPath);
    root.insert(QStringLiteral("snapshot_dir_path"), result.snapshotDirPath);
    root.insert(QStringLiteral("source_orders"), result.sourceOrderCount);
    root.insert(QStringLiteral("imported_orders"), result.importedOrderCount);
    root.insert(QStringLiteral("source_services"), result.sourceServiceCount);
    root.insert(QStringLiteral("imported_services"), result.importedServiceCount);
    root.insert(QStringLiteral("source_parts"), result.sourcePartCount);
    root.insert(QStringLiteral("imported_parts"), result.importedPartCount);
    root.insert(QStringLiteral("source_service_masters"), result.sourceServiceMasterCount);
    root.insert(QStringLiteral("imported_service_masters"), result.importedServiceMasterCount);
    root.insert(QStringLiteral("source_outsource_repairs"), result.sourceOutsourceCount);
    root.insert(QStringLiteral("imported_outsource_repairs"), result.importedOutsourceCount);
    root.insert(QStringLiteral("source_masters"), result.sourceMasterCount);
    root.insert(QStringLiteral("imported_masters"), result.importedMasterCount);
    root.insert(QStringLiteral("source_catalog"), result.sourceCatalogCount);
    root.insert(QStringLiteral("imported_catalog"), result.importedCatalogCount);
    root.insert(QStringLiteral("source_planning"), result.sourcePlanningCount);
    root.insert(QStringLiteral("imported_planning"), result.importedPlanningCount);
    root.insert(QStringLiteral("source_expenses"), result.sourceExpenseCount);
    root.insert(QStringLiteral("imported_expenses"), result.importedExpenseCount);
    root.insert(QStringLiteral("updated_count"), result.updatedCount);
    root.insert(QStringLiteral("skipped_count"), result.skippedCount);
    root.insert(QStringLiteral("conflict_count"), result.conflictCount);
    root.insert(QStringLiteral("critical_error_count"), result.criticalErrorCount);
    return root;
}

QString renderCountsMarkdown(const LegacyMigrationResult& result)
{
    QString text;
    text += QStringLiteral("| Сутність | Джерело | Результат |\n");
    text += QStringLiteral("|---|---:|---:|\n");
    text += QStringLiteral("| work_orders | %1 | %2 |\n").arg(result.sourceOrderCount).arg(result.importedOrderCount);
    text += QStringLiteral("| work_order_services | %1 | %2 |\n").arg(result.sourceServiceCount).arg(result.importedServiceCount);
    text += QStringLiteral("| work_order_parts | %1 | %2 |\n").arg(result.sourcePartCount).arg(result.importedPartCount);
    text += QStringLiteral("| service_masters | %1 | %2 |\n").arg(result.sourceServiceMasterCount).arg(result.importedServiceMasterCount);
    text += QStringLiteral("| outsource_repairs | %1 | %2 |\n").arg(result.sourceOutsourceCount).arg(result.importedOutsourceCount);
    text += QStringLiteral("| masters | %1 | %2 |\n").arg(result.sourceMasterCount).arg(result.importedMasterCount);
    text += QStringLiteral("| parts_catalog | %1 | %2 |\n").arg(result.sourceCatalogCount).arg(result.importedCatalogCount);
    text += QStringLiteral("| planning_calendar | %1 | %2 |\n").arg(result.sourcePlanningCount).arg(result.importedPlanningCount);
    text += QStringLiteral("| expenses | %1 | %2 |\n").arg(result.sourceExpenseCount).arg(result.importedExpenseCount);
    return text;
}

QString renderAuditMarkdown(const LegacyImportAuditReport& report)
{
    QString text;
    text += QStringLiteral("# Legacy import preview report\n\n");
    text += summaryLine(QStringLiteral("Джерело"), report.sourcePath);
    text += summaryLine(QStringLiteral("Тип"), report.sourceKind);
    text += summaryLine(QStringLiteral("Primary DB"), report.sourceDbPath);
    text += summaryLine(QStringLiteral("Fingerprint"), report.sourceFingerprint);
    text += summaryLine(QStringLiteral("Integrity"), report.sqliteIntegrityOk ? QStringLiteral("OK") : QStringLiteral("FAIL"));
    text += summaryLine(QStringLiteral("Execute mode"), report.canExecute ? QStringLiteral("ALLOW") : QStringLiteral("BLOCK"));
    text += summaryLine(QStringLiteral("Нарядів"), QString::number(report.orderCount));
    text += summaryLine(QStringLiteral("Робіт"), QString::number(report.serviceCount));
    text += summaryLine(QStringLiteral("Запчастин"), QString::number(report.partCount));
    text += summaryLine(QStringLiteral("Sidecar-файли"), report.detectedSidecars.isEmpty() ? QStringLiteral("немає") : report.detectedSidecars.join(QStringLiteral(", ")));

    text += QStringLiteral("\n## Аудит таблиць\n");
    text += QStringLiteral("| Entity | Source table | Required | Present | Rows | Missing columns |\n");
    text += QStringLiteral("|---|---|---:|---:|---:|---|\n");
    for (const LegacyImportEntityAudit& entity : report.entities) {
        text += QStringLiteral("| %1 | %2 | %3 | %4 | %5 | %6 |\n")
                .arg(entity.entityName,
                     entity.sourceTable,
                     entity.required ? QStringLiteral("yes") : QStringLiteral("no"),
                     entity.present ? QStringLiteral("yes") : QStringLiteral("no"),
                     QString::number(entity.rowCount),
                     entity.missingColumns.join(QStringLiteral(", ")));
    }

    text += QStringLiteral("\n## Schema map\n");
    for (const LegacyImportEntitySchemaMap& entity : report.schemaMap) {
        text += QStringLiteral("- **%1**: `%2 -> %3` | strategy=%4 | normalization=%5\n")
                .arg(entity.entityName,
                     entity.sourceTable,
                     entity.targetTable,
                     entity.strategy,
                     entity.requiresNormalization ? QStringLiteral("yes") : QStringLiteral("no"));
        for (const LegacyImportFieldMap& field : entity.fieldMaps) {
            text += QStringLiteral("  - `%1` <= [%2] | %3%4\n")
                    .arg(field.targetField,
                         field.sourceCandidates.join(QStringLiteral(", ")),
                         field.transformRule,
                         field.required ? QStringLiteral(" | required") : QString());
        }
        for (const QString& note : entity.notes) {
            text += QStringLiteral("  - note: %1\n").arg(note);
        }
    }

    text += QStringLiteral("\n## Findings\n");
    if (report.findings.isEmpty()) text += QStringLiteral("- Немає додаткових findings.\n");
    for (const LegacyImportFinding& finding : report.findings) {
        text += QStringLiteral("- [%1] %2 (%3): %4")
                .arg(finding.severity, finding.code, finding.entityName, finding.message);
        if (finding.affectedCount >= 0) text += QStringLiteral(" | affected=%1").arg(finding.affectedCount);
        if (finding.blocksExecute) text += QStringLiteral(" | blocks_execute");
        text += QLatin1Char('\n');
    }

    text += QStringLiteral("\n## Info\n");
    if (report.infoMessages.isEmpty()) text += QStringLiteral("- Немає.\n");
    for (const QString& s : report.infoMessages) text += QStringLiteral("- %1\n").arg(s);
    text += QStringLiteral("\n## Warnings\n");
    if (report.warningMessages.isEmpty()) text += QStringLiteral("- Немає.\n");
    for (const QString& s : report.warningMessages) text += QStringLiteral("- %1\n").arg(s);
    text += QStringLiteral("\n## Errors\n");
    if (report.errorMessages.isEmpty()) text += QStringLiteral("- Немає.\n");
    for (const QString& s : report.errorMessages) text += QStringLiteral("- %1\n").arg(s);
    return text;
}

QString renderVerifyMarkdown(const LegacyImportVerifyReport& report)
{
    QString text;
    text += QStringLiteral("# Legacy import verify report\n\n");
    text += summaryLine(QStringLiteral("Source DB"), report.sourceDbPath);
    text += summaryLine(QStringLiteral("Verify success"), report.success ? QStringLiteral("YES") : QStringLiteral("NO"));
    text += QStringLiteral("\n## Entity results\n");
    text += QStringLiteral("| Entity | Source | Target | Missing | Invalid | Status | Notes |\n");
    text += QStringLiteral("|---|---:|---:|---:|---:|---|---|\n");
    for (const LegacyImportVerifyEntityResult& entity : report.entities) {
        text += QStringLiteral("| %1 | %2 | %3 | %4 | %5 | %6 | %7 |\n")
                .arg(entity.entityName,
                     QString::number(entity.sourceCount),
                     QString::number(entity.targetCount),
                     QString::number(entity.missingCount),
                     QString::number(entity.invalidLinkCount),
                     entity.ok ? QStringLiteral("OK") : QStringLiteral("FAIL"),
                     entity.messages.join(QStringLiteral("; ")));
    }
    text += QStringLiteral("\n## Warnings\n");
    if (report.warningMessages.isEmpty()) text += QStringLiteral("- Немає.\n");
    for (const QString& s : report.warningMessages) text += QStringLiteral("- %1\n").arg(s);
    text += QStringLiteral("\n## Errors\n");
    if (report.errorMessages.isEmpty()) text += QStringLiteral("- Немає.\n");
    for (const QString& s : report.errorMessages) text += QStringLiteral("- %1\n").arg(s);
    return text;
}

QString renderScenarioMarkdown(const QVector<LegacyImportScenarioItem>& items)
{
    QString text;
    text += QStringLiteral("# Legacy import test scenarios\n\n");
    for (const LegacyImportScenarioItem& item : items) {
        text += QStringLiteral("- **%1 %2** — %3 | %4 | automated=%5\n")
                .arg(item.id,
                     item.title,
                     item.status,
                     item.note,
                     item.automated ? QStringLiteral("yes") : QStringLiteral("no"));
    }
    return text;
}

QString renderAcceptanceMarkdown(const QVector<LegacyImportAcceptanceItem>& items)
{
    QString text;
    text += QStringLiteral("# Legacy import acceptance report\n\n");
    for (const LegacyImportAcceptanceItem& item : items) {
        text += QStringLiteral("- **%1 %2** — %3 | %4\n")
                .arg(item.id, item.title, item.status, item.note);
    }
    return text;
}

QString renderValidationMarkdown(const LegacyMigrationResult& migrationResult,
                                 const LegacyImportVerifyReport& verifyReport)
{
    QString text;
    text += QStringLiteral("# MigrationValidationReport\n\n");
    text += summaryLine(QStringLiteral("Verify success"), verifyReport.success ? QStringLiteral("YES") : QStringLiteral("NO"));
    text += summaryLine(QStringLiteral("Source DB"), migrationResult.sourceDbPath);
    text += summaryLine(QStringLiteral("Target DB"), migrationResult.targetDbPath);
    text += QStringLiteral("\n## Кількісна звірка\n");
    text += renderCountsMarkdown(migrationResult);
    text += QStringLiteral("\n## Post-check entity results\n");
    for (const LegacyImportVerifyEntityResult& entity : verifyReport.entities) {
        text += QStringLiteral("- **%1**: source=%2 target=%3 missing=%4 invalid=%5 status=%6")
                .arg(entity.entityName)
                .arg(entity.sourceCount)
                .arg(entity.targetCount)
                .arg(entity.missingCount)
                .arg(entity.invalidLinkCount)
                .arg(entity.ok ? QStringLiteral("OK") : QStringLiteral("FAIL"));
        if (!entity.messages.isEmpty()) text += QStringLiteral(" | %1").arg(entity.messages.join(QStringLiteral("; ")));
        text += QLatin1Char('\n');
    }
    text += QStringLiteral("\n## Verify info\n");
    if (verifyReport.infoMessages.isEmpty()) text += QStringLiteral("- Немає.\n");
    for (const QString& s : verifyReport.infoMessages) text += QStringLiteral("- %1\n").arg(s);
    text += QStringLiteral("\n## Verify warnings\n");
    if (verifyReport.warningMessages.isEmpty()) text += QStringLiteral("- Немає.\n");
    for (const QString& s : verifyReport.warningMessages) text += QStringLiteral("- %1\n").arg(s);
    text += QStringLiteral("\n## Verify errors\n");
    if (verifyReport.errorMessages.isEmpty()) text += QStringLiteral("- Немає.\n");
    for (const QString& s : verifyReport.errorMessages) text += QStringLiteral("- %1\n").arg(s);
    return text;
}

QString renderDeveloperMarkdown(const LegacyImportAuditReport& auditReport,
                                const LegacyMigrationResult& migrationResult,
                                const LegacyImportVerifyReport& verifyReport,
                                const QVector<LegacyImportScenarioItem>& scenarios,
                                const QVector<LegacyImportAcceptanceItem>& acceptance,
                                const LegacyImportRunContext& runContext)
{
    QString text;
    text += QStringLiteral("# Developer handoff: legacy import\n\n");
    text += summaryLine(QStringLiteral("Run id"), runContext.runId);
    text += summaryLine(QStringLiteral("Import log"), runContext.logFilePath);
    text += summaryLine(QStringLiteral("Source fingerprint"), runContext.sourceFingerprint);
    text += summaryLine(QStringLiteral("Source DB"), migrationResult.sourceDbPath);
    text += summaryLine(QStringLiteral("Target DB"), migrationResult.targetDbPath);
    text += summaryLine(QStringLiteral("Snapshot dir"), migrationResult.snapshotDirPath);
    text += summaryLine(QStringLiteral("Conflicts"), QString::number(migrationResult.conflictCount));
    text += summaryLine(QStringLiteral("Critical verify errors"), QString::number(migrationResult.criticalErrorCount));
    text += QStringLiteral("\n## Count matrix\n");
    text += renderCountsMarkdown(migrationResult);
    text += QStringLiteral("\n## Migration map\n");
    for (const LegacyImportEntitySchemaMap& entity : auditReport.schemaMap) {
        text += QStringLiteral("- **%1**: `%2 -> %3` | strategy=%4 | normalization=%5\n")
                .arg(entity.entityName,
                     entity.sourceTable,
                     entity.targetTable,
                     entity.strategy,
                     entity.requiresNormalization ? QStringLiteral("yes") : QStringLiteral("no"));
        for (const LegacyImportFieldMap& field : entity.fieldMaps) {
            text += QStringLiteral("  - `%1` <= [%2] | %3%4\n")
                    .arg(field.targetField,
                         field.sourceCandidates.join(QStringLiteral(", ")),
                         field.transformRule,
                         field.required ? QStringLiteral(" | required") : QString());
        }
    }
    text += QStringLiteral("\n## Audit findings\n");
    if (auditReport.findings.isEmpty()) text += QStringLiteral("- Немає findings.\n");
    for (const LegacyImportFinding& finding : auditReport.findings) {
        text += QStringLiteral("- [%1] %2: %3\n").arg(finding.severity, finding.code, finding.message);
    }
    text += QStringLiteral("\n## Verify warnings\n");
    if (verifyReport.warningMessages.isEmpty()) text += QStringLiteral("- Немає.\n");
    for (const QString& s : verifyReport.warningMessages) text += QStringLiteral("- %1\n").arg(s);
    text += QStringLiteral("\n## Verify errors\n");
    if (verifyReport.errorMessages.isEmpty()) text += QStringLiteral("- Немає.\n");
    for (const QString& s : verifyReport.errorMessages) text += QStringLiteral("- %1\n").arg(s);
    text += QStringLiteral("\n## Test scenarios\n");
    text += renderScenarioMarkdown(scenarios);
    text += QStringLiteral("\n## Acceptance\n");
    text += renderAcceptanceMarkdown(acceptance);
    return text;
}

QString renderUserGuideMarkdown(const LegacyImportAuditReport& auditReport, const QString& executeReportDir)
{
    QString text;
    text += QStringLiteral("# Інструкція користувача: безпечний імпорт legacy БД\n\n");
    text += QStringLiteral("1. Відкрийте Налаштування → Google Drive / backup import.\n");
    text += QStringLiteral("2. Оберіть зовнішній legacy backup (*.zip, *.db, *.sqlite, *.sqlite3).\n");
    text += QStringLiteral("3. Дочекайтеся preview/audit mode. Якщо execute mode заблокований, усуньте критичні конфлікти з preview report.\n");
    text += QStringLiteral("4. Підтвердьте execute mode лише після перевірки counts, warnings і sidecar-файлів.\n");
    text += QStringLiteral("5. Після завершення перевірте MigrationSummary, MigrationValidationReport і MigrationConflicts у каталозі:\n   `%1`\n").arg(executeReportDir);
    text += QStringLiteral("6. Перезапустіть застосунок і вручну перевірте відкриття нарядів, пошук, звіти, planning і аутсорс.\n");
    text += QStringLiteral("\nPreview result for this source: execute=%1, orders=%2, services=%3, parts=%4.\n")
            .arg(auditReport.canExecute ? QStringLiteral("ALLOW") : QStringLiteral("BLOCK"))
            .arg(auditReport.orderCount)
            .arg(auditReport.serviceCount)
            .arg(auditReport.partCount);
    return text;
}

QVector<ConflictRecord> collectConflictRecords(const LegacyImportAuditReport& auditReport,
                                               const LegacyImportVerifyReport& verifyReport)
{
    QVector<ConflictRecord> records;
    for (const LegacyImportFinding& finding : auditReport.findings) {
        ConflictRecord rec;
        rec.entity = finding.entityName;
        rec.key = finding.code;
        rec.reason = finding.message;
        rec.decision = finding.blocksExecute
            ? QStringLiteral("execute blocked until manual resolution")
            : QStringLiteral("preserve source value and continue with logged normalization/check");
        rec.manualIntervention = finding.blocksExecute;
        records.push_back(rec);
    }

    int idx = 1;
    for (const QString& message : verifyReport.errorMessages) {
        ConflictRecord rec;
        rec.entity = QStringLiteral("verify");
        rec.key = QStringLiteral("verify_error_%1").arg(idx++);
        rec.reason = message;
        rec.decision = QStringLiteral("post-import manual correction required");
        rec.manualIntervention = true;
        records.push_back(rec);
    }

    idx = 1;
    for (const QString& message : verifyReport.warningMessages) {
        ConflictRecord rec;
        rec.entity = QStringLiteral("verify_warning");
        rec.key = QStringLiteral("verify_warning_%1").arg(idx++);
        rec.reason = message;
        rec.decision = QStringLiteral("review recommended; migration kept source semantics where possible");
        rec.manualIntervention = false;
        records.push_back(rec);
    }
    return records;
}

QJsonArray conflictsToJson(const QVector<ConflictRecord>& records)
{
    QJsonArray array;
    for (const ConflictRecord& rec : records) {
        QJsonObject obj;
        obj.insert(QStringLiteral("entity"), rec.entity);
        obj.insert(QStringLiteral("key"), rec.key);
        obj.insert(QStringLiteral("reason"), rec.reason);
        obj.insert(QStringLiteral("decision"), rec.decision);
        obj.insert(QStringLiteral("manual_intervention"), rec.manualIntervention);
        array.append(obj);
    }
    return array;
}

QString conflictsToCsv(const QVector<ConflictRecord>& records)
{
    QString text = QStringLiteral("entity,key,reason,decision,manual_intervention\n");
    for (const ConflictRecord& rec : records) {
        text += csvEscape(rec.entity) + QLatin1Char(',')
             + csvEscape(rec.key) + QLatin1Char(',')
             + csvEscape(rec.reason) + QLatin1Char(',')
             + csvEscape(rec.decision) + QLatin1Char(',')
             + csvEscape(rec.manualIntervention ? QStringLiteral("yes") : QStringLiteral("no")) + QLatin1Char('\n');
    }
    return text;
}

QString renderSummaryMarkdown(const LegacyImportBundle& bundle,
                              const LegacyImportRunContext& runContext,
                              const LegacyImportAuditReport& auditReport,
                              const LegacyMigrationResult& migrationResult,
                              const LegacyImportVerifyReport& verifyReport,
                              const QString& reportDir)
{
    QString text;
    text += QStringLiteral("# MigrationSummary\n\n");
    text += summaryLine(QStringLiteral("Start UTC"), runContext.runId);
    text += summaryLine(QStringLiteral("Generated UTC"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    text += summaryLine(QStringLiteral("Source path"), auditReport.sourcePath);
    text += summaryLine(QStringLiteral("Source kind"), bundle.sourceKind);
    text += summaryLine(QStringLiteral("Source DB"), migrationResult.sourceDbPath);
    text += summaryLine(QStringLiteral("Target DB"), migrationResult.targetDbPath);
    text += summaryLine(QStringLiteral("Snapshot dir"), migrationResult.snapshotDirPath);
    text += summaryLine(QStringLiteral("Run id"), runContext.runId);
    text += summaryLine(QStringLiteral("Import log"), runContext.logFilePath);
    text += summaryLine(QStringLiteral("Preview report"), QDir(reportDir).filePath(QStringLiteral("preview_report.md")));
    text += summaryLine(QStringLiteral("Verify report"), QDir(reportDir).filePath(QStringLiteral("verify_report.md")));
    text += summaryLine(QStringLiteral("Validation report"), QDir(reportDir).filePath(QStringLiteral("MigrationValidationReport.md")));
    text += summaryLine(QStringLiteral("Conflicts JSON"), QDir(reportDir).filePath(QStringLiteral("MigrationConflicts.json")));
    text += summaryLine(QStringLiteral("Conflicts CSV"), QDir(reportDir).filePath(QStringLiteral("MigrationConflicts.csv")));
    text += summaryLine(QStringLiteral("Updated rows"), QString::number(migrationResult.updatedCount));
    text += summaryLine(QStringLiteral("Skipped rows"), QString::number(migrationResult.skippedCount));
    text += summaryLine(QStringLiteral("Conflicts"), QString::number(migrationResult.conflictCount));
    text += summaryLine(QStringLiteral("Critical errors"), QString::number(migrationResult.criticalErrorCount));
    text += summaryLine(QStringLiteral("Verify success"), verifyReport.success ? QStringLiteral("YES") : QStringLiteral("NO"));
    text += QStringLiteral("\n## Source -> result counts\n");
    text += renderCountsMarkdown(migrationResult);
    text += QStringLiteral("\n## Audit warnings\n");
    if (migrationResult.auditWarnings.isEmpty()) text += QStringLiteral("- Немає.\n");
    for (const QString& s : migrationResult.auditWarnings) text += QStringLiteral("- %1\n").arg(s);
    text += QStringLiteral("\n## Verify warnings\n");
    if (migrationResult.verifyWarnings.isEmpty()) text += QStringLiteral("- Немає.\n");
    for (const QString& s : migrationResult.verifyWarnings) text += QStringLiteral("- %1\n").arg(s);
    text += QStringLiteral("\n## Verify errors\n");
    if (migrationResult.verifyErrors.isEmpty()) text += QStringLiteral("- Немає.\n");
    for (const QString& s : migrationResult.verifyErrors) text += QStringLiteral("- %1\n").arg(s);
    return text;
}

bool ensureDir(QString* dirPath, const QString& preferredName, QString* errorText)
{
    const QString root = LegacyImportReportSupport::reportRootDir();
    QDir().mkpath(root);
    const QString dir = QDir(root).filePath(preferredName);
    if (!QDir().mkpath(dir)) {
        if (errorText) *errorText = QStringLiteral("Не вдалося створити каталог звітів: %1").arg(dir);
        return false;
    }
    if (dirPath) *dirPath = dir;
    return true;
}

} // namespace

QString LegacyImportReportSupport::reportRootDir()
{
    return QDir(AppRuntime::logsDir()).filePath(QStringLiteral("legacy_import_reports"));
}

bool LegacyImportReportSupport::writePreviewArtifacts(const LegacyImportAuditReport& auditReport,
                                                      QString* reportDirPath,
                                                      QString* errorText)
{
    QString dir;
    if (!ensureDir(&dir, QStringLiteral("preview_%1").arg(nowStamp()), errorText)) return false;

    if (!writeText(QDir(dir).filePath(QStringLiteral("preview_report.md")), renderAuditMarkdown(auditReport), errorText)) return false;
    if (!writeText(QDir(dir).filePath(QStringLiteral("preview_report.json")), QString::fromUtf8(QJsonDocument(toJson(auditReport)).toJson(QJsonDocument::Indented)), errorText)) return false;

    if (reportDirPath) *reportDirPath = dir;
    return true;
}

bool LegacyImportReportSupport::writeVerifyArtifacts(const QString& sourcePath,
                                                     const LegacyImportVerifyReport& verifyReport,
                                                     QString* reportDirPath,
                                                     QString* errorText)
{
    Q_UNUSED(sourcePath)
    QString dir;
    if (!ensureDir(&dir, QStringLiteral("verify_%1").arg(nowStamp()), errorText)) return false;
    if (!writeText(QDir(dir).filePath(QStringLiteral("verify_report.md")), renderVerifyMarkdown(verifyReport), errorText)) return false;
    if (!writeText(QDir(dir).filePath(QStringLiteral("verify_report.json")), QString::fromUtf8(QJsonDocument(toJson(verifyReport)).toJson(QJsonDocument::Indented)), errorText)) return false;
    if (reportDirPath) *reportDirPath = dir;
    return true;
}

bool LegacyImportReportSupport::writeExecuteArtifacts(const LegacyImportBundle& bundle,
                                                      const LegacyImportRunContext& runContext,
                                                      const LegacyImportAuditReport& auditReport,
                                                      const LegacyMigrationResult& migrationResult,
                                                      const LegacyImportVerifyReport& verifyReport,
                                                      QString* reportDirPath,
                                                      QString* errorText)
{
    QString dir;
    if (!ensureDir(&dir, QStringLiteral("execute_%1").arg(runContext.runId.isEmpty() ? nowStamp() : runContext.runId), errorText)) return false;

    const QVector<LegacyImportScenarioItem> scenarios = LegacyImportScenarioSupport::buildScenarioMatrix(&auditReport, &migrationResult, &verifyReport);
    const QVector<LegacyImportAcceptanceItem> acceptance = LegacyImportAcceptanceSupport::evaluate(&auditReport, &migrationResult, &verifyReport);
    const QVector<ConflictRecord> conflicts = collectConflictRecords(auditReport, verifyReport);

    const QString auditMd = renderAuditMarkdown(auditReport);
    const QString verifyMd = renderVerifyMarkdown(verifyReport);
    const QString validationMd = renderValidationMarkdown(migrationResult, verifyReport);
    const QString summaryMd = renderSummaryMarkdown(bundle, runContext, auditReport, migrationResult, verifyReport, dir);
    const QString scenarioMd = renderScenarioMarkdown(scenarios);
    const QString acceptanceMd = renderAcceptanceMarkdown(acceptance);
    const QString developerMd = renderDeveloperMarkdown(auditReport, migrationResult, verifyReport, scenarios, acceptance, runContext);
    const QString userGuideMd = renderUserGuideMarkdown(auditReport, dir);
    const QString conflictsCsv = conflictsToCsv(conflicts);
    const QString conflictsJson = QString::fromUtf8(QJsonDocument(conflictsToJson(conflicts)).toJson(QJsonDocument::Indented));

    if (!writeText(QDir(dir).filePath(QStringLiteral("preview_report.md")), auditMd, errorText)) return false;
    if (!writeText(QDir(dir).filePath(QStringLiteral("preview_report.json")), QString::fromUtf8(QJsonDocument(toJson(auditReport)).toJson(QJsonDocument::Indented)), errorText)) return false;
    if (!writeText(QDir(dir).filePath(QStringLiteral("verify_report.md")), verifyMd, errorText)) return false;
    if (!writeText(QDir(dir).filePath(QStringLiteral("verify_report.json")), QString::fromUtf8(QJsonDocument(toJson(verifyReport)).toJson(QJsonDocument::Indented)), errorText)) return false;
    if (!writeText(QDir(dir).filePath(QStringLiteral("MigrationValidationReport.md")), validationMd, errorText)) return false;
    if (!writeText(QDir(dir).filePath(QStringLiteral("MigrationSummary.md")), summaryMd, errorText)) return false;
    if (!writeText(QDir(dir).filePath(QStringLiteral("MigrationSummary.txt")), summaryMd, errorText)) return false;
    if (!writeText(QDir(dir).filePath(QStringLiteral("MigrationConflicts.csv")), conflictsCsv, errorText)) return false;
    if (!writeText(QDir(dir).filePath(QStringLiteral("MigrationConflicts.json")), conflictsJson, errorText)) return false;
    if (!writeText(QDir(dir).filePath(QStringLiteral("test_scenarios.md")), scenarioMd, errorText)) return false;
    if (!writeText(QDir(dir).filePath(QStringLiteral("acceptance_report.md")), acceptanceMd, errorText)) return false;
    if (!writeText(QDir(dir).filePath(QStringLiteral("developer_handoff.md")), developerMd, errorText)) return false;
    if (!writeText(QDir(dir).filePath(QStringLiteral("user_instruction.md")), userGuideMd, errorText)) return false;

    // explicit aliases requested by deep audit / acceptance docs
    if (!writeText(QDir(dir).filePath(QStringLiteral("MigrationAlgorithmAudit.md")), developerMd, errorText)) return false;
    if (!writeText(QDir(dir).filePath(QStringLiteral("MigrationIntegrityReport.md")), validationMd, errorText)) return false;
    if (!writeText(QDir(dir).filePath(QStringLiteral("MigrationAcceptanceChecklist.md")), acceptanceMd, errorText)) return false;

    // backward-compatible legacy lowercase aliases
    if (!writeText(QDir(dir).filePath(QStringLiteral("migration_summary.md")), summaryMd, errorText)) return false;
    if (!writeText(QDir(dir).filePath(QStringLiteral("migration_validation_report.md")), validationMd, errorText)) return false;
    if (!writeText(QDir(dir).filePath(QStringLiteral("migration_conflicts.csv")), conflictsCsv, errorText)) return false;
    if (!writeText(QDir(dir).filePath(QStringLiteral("migration_conflicts.json")), conflictsJson, errorText)) return false;

    QJsonObject summary;
    summary.insert(QStringLiteral("run_id"), runContext.runId);
    summary.insert(QStringLiteral("source_path"), auditReport.sourcePath);
    summary.insert(QStringLiteral("source_kind"), migrationResult.sourceKind);
    summary.insert(QStringLiteral("log_file"), runContext.logFilePath);
    summary.insert(QStringLiteral("summary_report"), QDir(dir).filePath(QStringLiteral("MigrationSummary.md")));
    summary.insert(QStringLiteral("summary_text"), QDir(dir).filePath(QStringLiteral("MigrationSummary.txt")));
    summary.insert(QStringLiteral("validation_report"), QDir(dir).filePath(QStringLiteral("MigrationValidationReport.md")));
    summary.insert(QStringLiteral("algorithm_audit"), QDir(dir).filePath(QStringLiteral("MigrationAlgorithmAudit.md")));
    summary.insert(QStringLiteral("integrity_report"), QDir(dir).filePath(QStringLiteral("MigrationIntegrityReport.md")));
    summary.insert(QStringLiteral("acceptance_checklist"), QDir(dir).filePath(QStringLiteral("MigrationAcceptanceChecklist.md")));
    summary.insert(QStringLiteral("conflicts_json"), QDir(dir).filePath(QStringLiteral("MigrationConflicts.json")));
    summary.insert(QStringLiteral("conflicts_csv"), QDir(dir).filePath(QStringLiteral("MigrationConflicts.csv")));
    summary.insert(QStringLiteral("developer_handoff"), QDir(dir).filePath(QStringLiteral("developer_handoff.md")));
    summary.insert(QStringLiteral("user_instruction"), QDir(dir).filePath(QStringLiteral("user_instruction.md")));
    summary.insert(QStringLiteral("migration_result"), resultToJson(migrationResult));
    summary.insert(QStringLiteral("audit"), toJson(auditReport));
    summary.insert(QStringLiteral("verify"), toJson(verifyReport));
    if (!writeText(QDir(dir).filePath(QStringLiteral("migration_summary.json")), QString::fromUtf8(QJsonDocument(summary).toJson(QJsonDocument::Indented)), errorText)) return false;

    if (reportDirPath) *reportDirPath = dir;
    return true;
}
