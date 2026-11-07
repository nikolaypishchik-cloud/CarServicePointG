#include "infrastructure/db/sqlite/DbCanonicalRewriteFinalizeSupport.h"

#include "infrastructure/db/sqlite/DbFacadeSupport.h"
#include "infrastructure/db/sqlite/DbSettingsSqlSupport.h"
#include "infrastructure/db/sqlite/DbCanonicalRewriteExecutionSupport.h"
#include "infrastructure/db/sqlite/LegacyCanonicalAncillaryImportSupport.h"
#include "infrastructure/db/sqlite/LegacyCanonicalImportNormalization.h"
#include "core/services/OrderDetailIntegrityValidator.h"

#include <QDateTime>
#include <QSqlError>
#include <QSqlQuery>

bool restoreCanonicalRewriteWorkspaceSettings(QSqlDatabase database,
                                             const QMap<QString, QVariant>& workspaceSettingsSnapshot,
                                             QString* errorText)
{
    if (!database.isValid()) {
        if (errorText) *errorText = QStringLiteral("Canonical rewrite restore settings failed: invalid database");
        return false;
    }
    if (workspaceSettingsSnapshot.isEmpty()) return true;
    const QString restoreSql = QStringLiteral(
        "INSERT INTO settings(\"key\", value) VALUES(?, ?) "
        "ON CONFLICT(\"key\") DO UPDATE SET value=excluded.value");
    QSqlQuery restoreSettings(database);
    if (!restoreSettings.prepare(restoreSql)) {
        if (errorText) {
            *errorText = QStringLiteral("restore workspace settings after canonical rewrite prepare failed: %1")
                             .arg(restoreSettings.lastError().text());
        }
        return false;
    }

    for (auto it = workspaceSettingsSnapshot.cbegin(); it != workspaceSettingsSnapshot.cend(); ++it) {
        restoreSettings.bindValue(0, it.key());
        restoreSettings.bindValue(1, it.value());
        if (!restoreSettings.exec()) {
            if (errorText) {
                *errorText = QStringLiteral("restore workspace settings after canonical rewrite: %1")
                                 .arg(restoreSettings.lastError().text());
            }
            return false;
        }
        restoreSettings.finish();
        restoreSettings.clear();
        if (!restoreSettings.prepare(restoreSql)) {
            if (errorText) {
                *errorText = QStringLiteral("restore workspace settings after canonical rewrite prepare failed: %1")
                                 .arg(restoreSettings.lastError().text());
            }
            return false;
        }
    }
    return true;
}

bool finalizeCanonicalRewriteImportedWorkspace(QSqlDatabase database,
                                              const QString& fingerprint,
                                              const QString& sourcePath,
                                              QString* errorText)
{
    if (!database.isValid()) {
        if (errorText) *errorText = QStringLiteral("Canonical rewrite finalization failed: invalid database");
        return false;
    }

    QSqlQuery q(database);
    if (!q.exec("UPDATE work_orders SET color=COALESCE(NULLIF(color,''), NULLIF(car_color,''), '')")) {
        if (errorText) *errorText = QStringLiteral("Canonical rewrite work_orders.color update failed: %1").arg(q.lastError().text());
        return false;
    }
    if (!q.exec("UPDATE work_orders SET car_color=COALESCE(NULLIF(car_color,''), NULLIF(color,''), '')")) {
        if (errorText) *errorText = QStringLiteral("Canonical rewrite work_orders.car_color update failed: %1").arg(q.lastError().text());
        return false;
    }

    QString normalizationError;
    if (!LegacyCanonicalImportNormalization::finalizeCanonicalImportedRelations(database, &normalizationError)) {
        if (errorText) *errorText = QStringLiteral("canonical import normalization failed: %1").arg(normalizationError);
        return false;
    }
    if (!LegacyCanonicalAncillaryImportSupport::finalizeImportedAncillaryData(database, &normalizationError)) {
        if (errorText) *errorText = QStringLiteral("ancillary canonical import normalization failed: %1").arg(normalizationError);
        return false;
    }
    if (!OrderDetailIntegrityValidator::ensureCanonicalDetailSchema(database, &normalizationError)
        || !OrderDetailIntegrityValidator::repairCanonicalOrderUids(database, &normalizationError)
        || !OrderDetailIntegrityValidator::validateAfterImport(database, &normalizationError)) {
        if (errorText) *errorText = QStringLiteral("order detail integrity validation failed: %1").arg(normalizationError);
        return false;
    }

    const QMap<QString, QVariant> completionSettings{
        {QStringLiteral("db_canonical_rewrite_done"), QStringLiteral("1")},
        {QStringLiteral("db_canonical_rewrite_fingerprint"), fingerprint},
        {QStringLiteral("db_canonical_rewrite_source"), sourcePath},
        {QStringLiteral("db_canonical_rewrite_at"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}
    };
    if (!restoreCanonicalRewriteWorkspaceSettings(database, completionSettings, errorText)) return false;

    return upsertActiveOrdersIntoRegistryForWorkspace(database, errorText);
}
