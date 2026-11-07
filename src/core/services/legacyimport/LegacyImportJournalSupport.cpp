#include "core/services/legacyimport/LegacyImportJournalSupport.h"

#include "runtime/AppRuntime.h"
#include "core/services/LegacyMigrationService.h"
#include "core/services/legacyimport/LegacyImportSafetySupport.h"
#include "core/services/legacyimport/LegacyImportTypes.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QUuid>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <functional>

namespace {

QString historyFilePath()
{
    return QDir(AppRuntime::securityDir()).filePath(QStringLiteral("legacy_import_history.json"));
}

bool loadHistory(QJsonArray* items, QString* errorText = nullptr)
{
    QFile f(historyFilePath());
    if (!f.exists()) { *items = QJsonArray(); return true; }
    if (!f.open(QIODevice::ReadOnly)) {
        if (errorText) *errorText = QStringLiteral("Не вдалося прочитати журнал міграції: %1").arg(f.errorString());
        return false;
    }
    const QByteArray bytes = f.readAll();
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &parseError);
    if (f.error() != QFileDevice::NoError || parseError.error != QJsonParseError::NoError || !doc.isArray()) {
        if (errorText) *errorText = QStringLiteral("Журнал міграції пошкоджений або прочитаний не повністю. Потрібне відновлення журналу.");
        return false;
    }
    const QJsonArray parsed = doc.array();
    for (const auto& item : parsed) {
        const auto obj = item.toObject();
        if (!item.isObject() || obj.value(QStringLiteral("run_id")).toString().isEmpty()
                || obj.value(QStringLiteral("status")).toString().isEmpty()) {
            if (errorText) *errorText = QStringLiteral("Журнал міграції містить некоректний запис.");
            return false;
        }
    }
    *items = parsed;
    return true;
}

bool saveHistory(const QJsonArray& array, QString* errorText = nullptr)
{
    QSaveFile f(historyFilePath());
    const auto bytes = QJsonDocument(array).toJson(QJsonDocument::Indented);
    if (!QDir().mkpath(AppRuntime::securityDir()) || !f.open(QIODevice::WriteOnly)
            || f.write(bytes) != bytes.size() || !f.commit()) {
        if (errorText) *errorText = QStringLiteral("Не вдалося зберегти журнал міграції: %1").arg(f.errorString());
        return false;
    }
    return true;
}

bool appendLine(const LegacyImportRunContext& context, const QString& level, const QString& stage, const QString& message, QString* errorText)
{
    QFile f(context.logFilePath);
    const auto bytes = (QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)
        + " [" + level + "] [" + stage + "] " + message + "\n").toUtf8();
    if (context.logFilePath.trimmed().isEmpty() || !f.open(QIODevice::WriteOnly | QIODevice::Append)
            || f.write(bytes) != bytes.size() || !f.flush()) {
        if (errorText) *errorText = QStringLiteral("Не вдалося записати журнал операцій міграції: %1").arg(f.errorString());
        return false;
    }
    return true;
}

bool updateHistory(const LegacyImportRunContext& context,
                   const QString& status,
                   const QString& stage,
                   const QString& note,
                   const std::function<void(QJsonObject&)>& extra, QString* errorText)
{
    QJsonArray items;
    if (!loadHistory(&items, errorText)) return false;
    bool updated = false;
    for (int i = 0; i < items.size(); ++i) {
        if (!items.at(i).isObject()) continue;
        QJsonObject obj = items.at(i).toObject();
        if (obj.value(QStringLiteral("run_id")).toString() != context.runId) continue;
        obj.insert(QStringLiteral("status"), status);
        obj.insert(QStringLiteral("stage"), stage);
        obj.insert(QStringLiteral("note"), note);
        obj.insert(QStringLiteral("updated_at"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
        if (extra) extra(obj);
        items.replace(i, obj);
        updated = true;
        break;
    }
    if (!updated) {
        if (errorText) *errorText = QStringLiteral("Запис міграції не знайдено в журналі: %1").arg(context.runId);
        return false;
    }
    return saveHistory(items, errorText);
}

}

bool LegacyImportJournalSupport::ensureCanImportFingerprint(const QString& sourceFingerprint, QString* errorText)
{
    const QString fp = sourceFingerprint.trimmed();
    if (fp.isEmpty()) return true;

    QJsonArray items;
    if (!loadHistory(&items, errorText)) return false;
    for (const QJsonValue& value : items) {
        if (!value.isObject()) continue;
        const QJsonObject obj = value.toObject();
        if (obj.value(QStringLiteral("fingerprint")).toString() == fp
                && obj.value(QStringLiteral("status")).toString() == QStringLiteral("success")) {
            if (errorText) {
                *errorText = QStringLiteral("Це legacy джерело вже було успішно імпортоване раніше. Повторний небезпечний імпорт заблоковано.");
            }
            return false;
        }
    }
    return true;
}

bool LegacyImportJournalSupport::beginRun(const LegacyImportBundle& bundle,
                                          const LegacyImportAuditReport& auditReport,
                                          LegacyImportRunContext* context,
                                          QString* errorText)
{
    QJsonArray items;
    if (!loadHistory(&items, errorText)) return false;
    if (!QDir().mkpath(AppRuntime::logsDir()) || !QDir().mkpath(AppRuntime::securityDir())) {
        if (errorText) *errorText = QStringLiteral("Не вдалося створити каталоги журналу міграції.");
        return false;
    }
    const QString runId = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd_HHmmss_zzz"))
            + QStringLiteral("_") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString logFile = QDir(AppRuntime::logsDir()).filePath(QStringLiteral("legacy_import_%1.log").arg(runId));
    QFile f(logFile);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        if (errorText) *errorText = QStringLiteral("Не вдалося створити migration log: %1").arg(logFile);
        return false;
    }
    f.close();

    LegacyImportRunContext local;
    local.runId = runId;
    local.logFilePath = logFile;
    local.sourceFingerprint = auditReport.sourceFingerprint;
    if (!appendLine(local, QStringLiteral("INFO"), QStringLiteral("LEGACY_IMPORT_BEGIN"),
               QStringLiteral("LEGACY_IMPORT BEGIN | source=%1 | kind=%2 | db=%3")
               .arg(bundle.sourcePath, bundle.sourceKind, auditReport.sourceDbPath), errorText)) return false;

    QJsonObject entry;
    entry.insert(QStringLiteral("run_id"), runId);
    entry.insert(QStringLiteral("fingerprint"), auditReport.sourceFingerprint);
    entry.insert(QStringLiteral("source_path"), bundle.sourcePath);
    entry.insert(QStringLiteral("source_kind"), bundle.sourceKind);
    entry.insert(QStringLiteral("source_db_path"), auditReport.sourceDbPath);
    entry.insert(QStringLiteral("status"), QStringLiteral("running"));
    entry.insert(QStringLiteral("stage"), QStringLiteral("begin"));
    entry.insert(QStringLiteral("created_at"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    entry.insert(QStringLiteral("updated_at"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    entry.insert(QStringLiteral("log_file"), logFile);
    items.append(entry);
    if (!saveHistory(items, errorText)) return false;

    if (context) *context = local;
    return true;
}

bool LegacyImportJournalSupport::logInfo(const LegacyImportRunContext& context, const QString& stage, const QString& message, QString* errorText)
{
    if (!appendLine(context, QStringLiteral("INFO"), stage, message, errorText)) return false;
    return updateHistory(context, QStringLiteral("running"), stage, message, {}, errorText);
}

bool LegacyImportJournalSupport::logWarning(const LegacyImportRunContext& context, const QString& stage, const QString& message, QString* errorText)
{
    if (!appendLine(context, QStringLiteral("WARN"), stage, message, errorText)) return false;
    return updateHistory(context, QStringLiteral("running"), stage, message, {}, errorText);
}

bool LegacyImportJournalSupport::logError(const LegacyImportRunContext& context, const QString& stage, const QString& message, QString* errorText)
{
    if (!appendLine(context, QStringLiteral("ERROR"), stage, message, errorText)) return false;
    return updateHistory(context, QStringLiteral("failed"), stage, message, {}, errorText);
}

bool LegacyImportJournalSupport::attachSnapshot(const LegacyImportRunContext& context, const LegacyImportSafetySnapshot& snapshot, QString* errorText)
{
    if (!appendLine(context, QStringLiteral("INFO"), QStringLiteral("LEGACY_IMPORT_SNAPSHOT"),
               QStringLiteral("LEGACY_IMPORT backup created | snapshot=%1").arg(snapshot.snapshotDirPath), errorText)) return false;
    return updateHistory(context, QStringLiteral("running"), QStringLiteral("snapshot"), snapshot.snapshotDirPath,
                  [&](QJsonObject& obj) { obj.insert(QStringLiteral("snapshot_dir"), snapshot.snapshotDirPath); }, errorText);
}

bool LegacyImportJournalSupport::finishSuccess(const LegacyImportRunContext& context,
                                               const LegacyMigrationResult& result,
                                               const LegacyImportVerifyReport& verifyReport, QString* errorText)
{
    if (!appendLine(context, QStringLiteral("INFO"), QStringLiteral("LEGACY_IMPORT_COMPLETION_PREPARED"),
               QStringLiteral("LEGACY_IMPORT COMPLETION PREPARED (history commit pending) | orders=%1/%2 services=%3/%4 parts=%5/%6 warnings=%7 errors=%8")
               .arg(result.sourceOrderCount)
               .arg(result.importedOrderCount)
               .arg(result.sourceServiceCount)
               .arg(result.importedServiceCount)
               .arg(result.sourcePartCount)
               .arg(result.importedPartCount)
               .arg(verifyReport.warningMessages.size())
               .arg(verifyReport.errorMessages.size()), errorText)) return false;
    return updateHistory(context, QStringLiteral("success"), QStringLiteral("success"), QStringLiteral("ok"),
                  [&](QJsonObject& obj) {
                      obj.insert(QStringLiteral("source_orders"), result.sourceOrderCount);
                      obj.insert(QStringLiteral("imported_orders"), result.importedOrderCount);
                      obj.insert(QStringLiteral("source_services"), result.sourceServiceCount);
                      obj.insert(QStringLiteral("imported_services"), result.importedServiceCount);
                      obj.insert(QStringLiteral("source_parts"), result.sourcePartCount);
                      obj.insert(QStringLiteral("imported_parts"), result.importedPartCount);
                      obj.insert(QStringLiteral("source_service_masters"), result.sourceServiceMasterCount);
                      obj.insert(QStringLiteral("imported_service_masters"), result.importedServiceMasterCount);
                      obj.insert(QStringLiteral("source_outsource_repairs"), result.sourceOutsourceCount);
                      obj.insert(QStringLiteral("imported_outsource_repairs"), result.importedOutsourceCount);
                      obj.insert(QStringLiteral("source_masters"), result.sourceMasterCount);
                      obj.insert(QStringLiteral("imported_masters"), result.importedMasterCount);
                      obj.insert(QStringLiteral("source_catalog"), result.sourceCatalogCount);
                      obj.insert(QStringLiteral("imported_catalog"), result.importedCatalogCount);
                      obj.insert(QStringLiteral("source_planning"), result.sourcePlanningCount);
                      obj.insert(QStringLiteral("imported_planning"), result.importedPlanningCount);
                      obj.insert(QStringLiteral("source_expenses"), result.sourceExpenseCount);
                      obj.insert(QStringLiteral("imported_expenses"), result.importedExpenseCount);
                      obj.insert(QStringLiteral("warning_count"), verifyReport.warningMessages.size());
                      obj.insert(QStringLiteral("error_count"), verifyReport.errorMessages.size());
                      obj.insert(QStringLiteral("conflict_count"), result.conflictCount);
                      obj.insert(QStringLiteral("snapshot_dir"), result.snapshotDirPath);
                      obj.insert(QStringLiteral("report_dir"), result.reportDirPath);
                      obj.insert(QStringLiteral("summary_report"), result.summaryReportPath);
                      obj.insert(QStringLiteral("validation_report"), result.validationReportPath);
                      obj.insert(QStringLiteral("conflicts_json"), result.conflictsJsonPath);
                      obj.insert(QStringLiteral("conflicts_csv"), result.conflictsCsvPath);
                  }, errorText);
}

bool LegacyImportJournalSupport::finishFailure(const LegacyImportRunContext& context, const QString& failureStage, const QString& message, QString* errorText)
{
    if (!appendLine(context, QStringLiteral("ERROR"), QStringLiteral("LEGACY_IMPORT_FAILED"),
               QStringLiteral("LEGACY_IMPORT FAILED | stage=%1 | %2").arg(failureStage, message), errorText)) return false;
    return updateHistory(context, QStringLiteral("failed"), failureStage, message, {}, errorText);
}
