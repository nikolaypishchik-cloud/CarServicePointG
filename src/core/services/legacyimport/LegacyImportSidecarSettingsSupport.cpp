#include "core/services/legacyimport/LegacyImportSidecarSettingsSupport.h"

#include "infrastructure/db/sqlite/Db.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QList>

namespace {
struct MappedSetting {
    QString legacyKey;
    QString targetKey;
    QJsonValue value;
};

bool loadJsonObject(const QString& path, QJsonObject* outObject, QString* errorText)
{
    if (!outObject) return false;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (errorText) *errorText = f.errorString();
        return false;
    }
    const QByteArray raw = f.readAll();
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        if (errorText) *errorText = pe.error != QJsonParseError::NoError ? pe.errorString() : QStringLiteral("JSON root is not an object");
        return false;
    }
    *outObject = doc.object();
    return true;
}

bool setRawJsonSetting(const QString& key, const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    return Database::setSetting(key, QString::fromUtf8(f.readAll()));
}

void applySetting(const QString& targetKey, const QJsonValue& value, LegacyImportSidecarApplyResult* result)
{
    if (targetKey.trimmed().isEmpty()) return;
    QVariant v = value.toVariant();
    if (value.isObject()) {
        v = QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));
    } else if (value.isArray()) {
        v = QString::fromUtf8(QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact));
    }
    if (Database::setSetting(targetKey, v)) {
        if (result) ++result->appliedSettings;
    }
}
}

bool LegacyImportSidecarSettingsSupport::applyCompatibleSidecars(const LegacyImportBundle& bundle,
                                                                 LegacyImportSidecarApplyResult* result,
                                                                 QString* errorText)
{
    LegacyImportSidecarApplyResult localResult;

    if (!bundle.settingsJsonPath.trimmed().isEmpty()) {
        QJsonObject obj;
        QString jsonError;
        if (!loadJsonObject(bundle.settingsJsonPath, &obj, &jsonError)) {
            localResult.warningMessages << QStringLiteral("settings.json не застосовано: %1").arg(jsonError);
        } else {
            setRawJsonSetting(QStringLiteral("legacy.import.settings.raw_json"), bundle.settingsJsonPath);
            const QList<MappedSetting> mapped = {
                {QStringLiteral("theme"), QStringLiteral("ui.theme"), obj.value(QStringLiteral("theme"))},
                {QStringLiteral("sounds_enabled"), QStringLiteral("ui/sounds_enabled"), obj.value(QStringLiteral("sounds_enabled"))},
                {QStringLiteral("volume"), QStringLiteral("settings.sounds.volume"), obj.value(QStringLiteral("volume"))},
                {QStringLiteral("ui_font_family"), QStringLiteral("settings.ui.font_family"), obj.value(QStringLiteral("ui_font_family"))},
                {QStringLiteral("ui_font_size"), QStringLiteral("settings.ui.font_size"), obj.value(QStringLiteral("ui_font_size"))},
                {QStringLiteral("ui_font_color"), QStringLiteral("settings.ui.font_color"), obj.value(QStringLiteral("ui_font_color"))},
                {QStringLiteral("ui_table_body_size"), QStringLiteral("settings.ui.table_font_size"), obj.value(QStringLiteral("ui_table_body_size"))},
                {QStringLiteral("overlay_opacity"), QStringLiteral("settings.ui.overlay_opacity"), obj.value(QStringLiteral("overlay_opacity"))},
                {QStringLiteral("anim_level"), QStringLiteral("settings.ui.anim_level"), obj.value(QStringLiteral("anim_level"))},
                {QStringLiteral("voice_enabled"), QStringLiteral("settings.voice.enabled"), obj.value(QStringLiteral("voice_enabled"))},
                {QStringLiteral("parts_markup_percent"), QStringLiteral("settings.fin.parts_markup"), obj.value(QStringLiteral("parts_markup_percent"))},
                {QStringLiteral("oil_markup_percent"), QStringLiteral("settings.fin.oil_markup"), obj.value(QStringLiteral("oil_markup_percent"))},
                {QStringLiteral("admin_parts_base_mode"), QStringLiteral("settings.parts.admin_base_mode"), obj.value(QStringLiteral("admin_parts_base_mode"))}
            };
            for (const MappedSetting& item : mapped) {
                if (item.value.isUndefined() || item.value.isNull()) continue;
                applySetting(item.targetKey, item.value, &localResult);
                Database::setSetting(QStringLiteral("legacy.import.settings.applied/%1").arg(item.legacyKey), item.targetKey);
            }

            const QJsonObject updatesObj = obj.value(QStringLiteral("updates")).toObject();
            if (!updatesObj.isEmpty()) {
                if (updatesObj.contains(QStringLiteral("auto_check"))) {
                    applySetting(QStringLiteral("settings.updates.auto"), updatesObj.value(QStringLiteral("auto_check")), &localResult);
                    Database::setSetting(QStringLiteral("legacy.import.settings.applied/updates.auto_check"), QStringLiteral("settings.updates.auto"));
                }
                if (updatesObj.contains(QStringLiteral("channel"))) applySetting(QStringLiteral("settings.updates.channel"), updatesObj.value(QStringLiteral("channel")), &localResult);
                if (updatesObj.contains(QStringLiteral("last_check"))) applySetting(QStringLiteral("settings.updates.last_check"), updatesObj.value(QStringLiteral("last_check")), &localResult);
                if (updatesObj.contains(QStringLiteral("last_seen_version"))) applySetting(QStringLiteral("settings.updates.last_seen_version"), updatesObj.value(QStringLiteral("last_seen_version")), &localResult);
                if (updatesObj.contains(QStringLiteral("manifest_url"))) applySetting(QStringLiteral("settings.updates.manifest_url"), updatesObj.value(QStringLiteral("manifest_url")), &localResult);
            }

            const QJsonObject apiObj = obj.value(QStringLiteral("api")).toObject();
            if (!apiObj.isEmpty()) {
                if (apiObj.contains(QStringLiteral("demo_mode"))) applySetting(QStringLiteral("settings.int.odb.demo_mode"), apiObj.value(QStringLiteral("demo_mode")), &localResult);
                const QJsonObject odb = apiObj.value(QStringLiteral("opendatabot")).toObject();
                if (!odb.isEmpty()) {
                    if (odb.contains(QStringLiteral("api_key"))) applySetting(QStringLiteral("settings.int.odb.api_key"), odb.value(QStringLiteral("api_key")), &localResult);
                    if (odb.contains(QStringLiteral("base_url"))) applySetting(QStringLiteral("settings.int.odb.base_url"), odb.value(QStringLiteral("base_url")), &localResult);
                    if (odb.contains(QStringLiteral("plate_endpoint"))) applySetting(QStringLiteral("settings.int.odb.plate_endpoint"), odb.value(QStringLiteral("plate_endpoint")), &localResult);
                    if (odb.contains(QStringLiteral("plate_param"))) applySetting(QStringLiteral("settings.int.odb.plate_param"), odb.value(QStringLiteral("plate_param")), &localResult);
                    if (odb.contains(QStringLiteral("api_key_param"))) applySetting(QStringLiteral("settings.int.odb.api_key_param"), odb.value(QStringLiteral("api_key_param")), &localResult);
                    if (odb.contains(QStringLiteral("header"))) applySetting(QStringLiteral("settings.int.odb.header_name"), odb.value(QStringLiteral("header")), &localResult);
                }
            }

            if (obj.contains(QStringLiteral("percents")) && obj.value(QStringLiteral("percents")).isObject()) {
                applySetting(QStringLiteral("payroll_percents"), obj.value(QStringLiteral("percents")), &localResult);
                Database::setSetting(QStringLiteral("legacy.import.settings.applied/percents"), QStringLiteral("payroll_percents"));
            }
            if (obj.contains(QStringLiteral("outsource_companies"))) {
                applySetting(QStringLiteral("settings.outsource.companies"), obj.value(QStringLiteral("outsource_companies")), &localResult);
            }
            if (obj.contains(QStringLiteral("outsource_company_markups"))) {
                applySetting(QStringLiteral("settings.outsource.company_markups"), obj.value(QStringLiteral("outsource_company_markups")), &localResult);
            }
            if (obj.contains(QStringLiteral("parts_provider"))) {
                applySetting(QStringLiteral("settings.parts.provider"), obj.value(QStringLiteral("parts_provider")), &localResult);
            }
            localResult.infoMessages << QStringLiteral("settings.json застосовано частково: %1 сумісних ключів").arg(localResult.appliedSettings);
        }
    }

    if (!bundle.uiStateJsonPath.trimmed().isEmpty()) {
        QJsonObject obj;
        QString jsonError;
        if (!loadJsonObject(bundle.uiStateJsonPath, &obj, &jsonError)) {
            localResult.warningMessages << QStringLiteral("ui_state.json не застосовано: %1").arg(jsonError);
        } else {
            setRawJsonSetting(QStringLiteral("legacy.import.ui_state.raw_json"), bundle.uiStateJsonPath);
            const QJsonObject tables = obj.value(QStringLiteral("tables")).toObject();
            auto applyTableState = [&](const QString& legacyKey, const QString& targetKey) {
                const QJsonValue value = tables.value(legacyKey);
                if (!value.isObject()) return;
                const QString compact = QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));
                if (Database::setSetting(targetKey, compact)) {
                    ++localResult.appliedUiStateEntries;
                    Database::setSetting(QStringLiteral("legacy.import.ui_state.applied/%1").arg(legacyKey), targetKey);
                }
            };
            applyTableState(QStringLiteral("reports:outsource"), QStringLiteral("legacy.ui_state.reports.outsource.columns"));
            applyTableState(QStringLiteral("reports:parts_purchase"), QStringLiteral("legacy.ui_state.reports.parts_purchase.columns"));
            localResult.infoMessages << QStringLiteral("ui_state.json застосовано частково: %1 таблиць").arg(localResult.appliedUiStateEntries);
        }
    }

    if (result) *result = localResult;
    Q_UNUSED(errorText);
    return true;
}
