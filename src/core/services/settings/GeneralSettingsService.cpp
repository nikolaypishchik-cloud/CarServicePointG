#include "core/services/settings/GeneralSettingsService.h"

#include "infrastructure/db/sqlite/Db.h"
#include "core/cloudsync/CloudSyncCredentials.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QtGlobal>

namespace GeneralSettingsService {

namespace {

QString trimmedSetting(const QString& key, const QString& defaultValue)
{
    QString value = Database::getSetting(key, QString()).toString().trimmed();
    if (value.isEmpty()) {
        value = defaultValue;
        Database::setSetting(key, value);
    }
    return value;
}

bool seedDefaultIfEmpty(const QString& key, const QString& defaultValue)
{
    if (!Database::getSetting(key, QString()).toString().trimmed().isEmpty()) return false;
    Database::setSetting(key, defaultValue);
    return true;
}

} // namespace

bool loadElitCredentials(ElitCredentials* credentials, QString* error)
{
    if (error) error->clear();
    if (!credentials) return false;
    *credentials = {};
    const QString stored = QSettings().value(QStringLiteral("integrations/elit/credentials_dpapi")).toString();
    if (stored.isEmpty()) return true;
    const QString plain = CloudSyncCredentials::unprotectToken(stored, error);
    if (plain.isEmpty()) return false;
    const auto document = QJsonDocument::fromJson(plain.toUtf8());
    const auto object = document.object();
    if (!document.isObject() || !object.value(QStringLiteral("login")).isString()
        || !object.value(QStringLiteral("password")).isString()
        || !object.value(QStringLiteral("apiKey")).isString()) {
        if (error) *error = QString::fromUtf8("Не вдалося прочитати збережені облікові дані Еліт.");
        return false;
    }
    credentials->login = object.value(QStringLiteral("login")).toString();
    credentials->password = object.value(QStringLiteral("password")).toString();
    credentials->apiKey = object.value(QStringLiteral("apiKey")).toString();
    const QString company = object.value(QStringLiteral("company")).toString().trimmed();
    if (!company.isEmpty()) credentials->company = company;
    credentials->rememberPassword = object.value(QStringLiteral("rememberPassword"))
        .toBool(!credentials->password.isEmpty());
    if (!credentials->rememberPassword) credentials->password.clear();
    return true;
}

bool saveElitCredentials(const ElitCredentials& credentials, QString* error)
{
    if (error) error->clear();
    const QJsonObject object{{QStringLiteral("login"), credentials.login.trimmed()},
                             {QStringLiteral("password"), credentials.rememberPassword ? credentials.password : QString()},
                             {QStringLiteral("apiKey"), credentials.apiKey},
                             {QStringLiteral("company"), credentials.company.trimmed().isEmpty()
                                 ? QStringLiteral("ELIT_UA") : credentials.company.trimmed()},
                             {QStringLiteral("rememberPassword"), credentials.rememberPassword}};
    const QString protectedValue = CloudSyncCredentials::protectToken(
        QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact)), error);
    if (protectedValue.isEmpty()) return false;
    QSettings settings;
    settings.setValue(QStringLiteral("integrations/elit/credentials_dpapi"), protectedValue);
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        if (error) *error = QString::fromUtf8("Не вдалося зберегти облікові дані Еліт.");
        return false;
    }
    return true;
}

bool saveUpdatesAuto(bool enabled, bool* changed)
{
    const bool hasChanges = Database::getSetting(QStringLiteral("settings.updates.auto"), true).toBool() != enabled;
    if (changed) *changed = hasChanges;
    Database::setSetting(QStringLiteral("settings.updates.auto"), enabled);
    return true;
}


bool loadUpdatesAuto()
{
    return Database::getSetting(QStringLiteral("settings.updates.auto"), true).toBool();
}

ReportsPdfSettingsPayload loadReportsPdfSettings()
{
    ReportsPdfSettingsPayload payload;
    payload.includeLogo = Database::getSetting(QStringLiteral("settings.reports.pdf.include_logo"), true).toBool();
    payload.includePhone = Database::getSetting(QStringLiteral("settings.reports.pdf.include_phone"), true).toBool();
    payload.useCustomLogoPosition = Database::getSetting(QStringLiteral("settings.reports.pdf.use_custom_logo_position"), false).toBool();
    payload.footerText = Database::getSetting(QStringLiteral("settings.reports.pdf.footer_text"), QString()).toString().trimmed();
    return payload;
}

PdfServiceRequisitesPayload defaultPdfServiceRequisites()
{
    PdfServiceRequisitesPayload payload;
    payload.name = QStringLiteral("CarServicePointG");
    payload.legalAddress = QString::fromUtf8("м. Кам’янське.");
    payload.actualAddress = QString::fromUtf8("м. Кам’янське.");
    payload.phone = QStringLiteral("+380673026636");
    payload.email = QStringLiteral("");
    payload.website = QStringLiteral("www.carservicepointg.com.ua");
    payload.edrpou = QStringLiteral("3085600579");
    payload.bankAccount = QStringLiteral("UA973052990000026007050648322");
    return payload;
}

bool ensurePdfServiceRequisitesDefaults(bool* changed)
{
    const auto defaults = defaultPdfServiceRequisites();
    bool hasChanges = false;
    hasChanges = seedDefaultIfEmpty(QStringLiteral("settings.pdf.service.name"), defaults.name) || hasChanges;
    hasChanges = seedDefaultIfEmpty(QStringLiteral("settings.pdf.service.legal_address"), defaults.legalAddress) || hasChanges;
    hasChanges = seedDefaultIfEmpty(QStringLiteral("settings.pdf.service.actual_address"), defaults.actualAddress) || hasChanges;
    hasChanges = seedDefaultIfEmpty(QStringLiteral("settings.pdf.service.phone"), defaults.phone) || hasChanges;
    hasChanges = seedDefaultIfEmpty(QStringLiteral("settings.pdf.service.email"), defaults.email) || hasChanges;
    hasChanges = seedDefaultIfEmpty(QStringLiteral("settings.pdf.service.website"), defaults.website) || hasChanges;
    hasChanges = seedDefaultIfEmpty(QStringLiteral("settings.pdf.service.edrpou"), defaults.edrpou) || hasChanges;
    hasChanges = seedDefaultIfEmpty(QStringLiteral("settings.pdf.service.bank_account"), defaults.bankAccount) || hasChanges;
    if (changed) *changed = hasChanges;
    return true;
}

PdfServiceRequisitesPayload loadPdfServiceRequisites()
{
    const auto defaults = defaultPdfServiceRequisites();
    PdfServiceRequisitesPayload payload;
    payload.name = trimmedSetting(QStringLiteral("settings.pdf.service.name"), defaults.name);
    payload.legalAddress = trimmedSetting(QStringLiteral("settings.pdf.service.legal_address"), defaults.legalAddress);
    payload.actualAddress = trimmedSetting(QStringLiteral("settings.pdf.service.actual_address"), defaults.actualAddress);
    payload.phone = trimmedSetting(QStringLiteral("settings.pdf.service.phone"), defaults.phone);
    payload.email = trimmedSetting(QStringLiteral("settings.pdf.service.email"), defaults.email);
    payload.website = trimmedSetting(QStringLiteral("settings.pdf.service.website"), defaults.website);
    payload.edrpou = trimmedSetting(QStringLiteral("settings.pdf.service.edrpou"), defaults.edrpou);
    payload.bankAccount = trimmedSetting(QStringLiteral("settings.pdf.service.bank_account"), defaults.bankAccount);
    return payload;
}

bool savePdfServiceRequisites(const PdfServiceRequisitesPayload& payload, bool* changed)
{
    const auto current = loadPdfServiceRequisites();
    PdfServiceRequisitesPayload normalized;
    normalized.name = payload.name.trimmed();
    normalized.legalAddress = payload.legalAddress.trimmed();
    normalized.actualAddress = payload.actualAddress.trimmed();
    normalized.phone = payload.phone.trimmed();
    normalized.email = payload.email.trimmed();
    normalized.website = payload.website.trimmed();
    normalized.edrpou = payload.edrpou.trimmed();
    normalized.bankAccount = payload.bankAccount.trimmed();

    const bool hasChanges =
        current.name != normalized.name ||
        current.legalAddress != normalized.legalAddress ||
        current.actualAddress != normalized.actualAddress ||
        current.phone != normalized.phone ||
        current.email != normalized.email ||
        current.website != normalized.website ||
        current.edrpou != normalized.edrpou ||
        current.bankAccount != normalized.bankAccount;
    if (changed) *changed = hasChanges;

    Database::setSetting(QStringLiteral("settings.pdf.service.name"), normalized.name);
    Database::setSetting(QStringLiteral("settings.pdf.service.legal_address"), normalized.legalAddress);
    Database::setSetting(QStringLiteral("settings.pdf.service.actual_address"), normalized.actualAddress);
    Database::setSetting(QStringLiteral("settings.pdf.service.phone"), normalized.phone);
    Database::setSetting(QStringLiteral("settings.pdf.service.email"), normalized.email);
    Database::setSetting(QStringLiteral("settings.pdf.service.website"), normalized.website);
    Database::setSetting(QStringLiteral("settings.pdf.service.edrpou"), normalized.edrpou);
    Database::setSetting(QStringLiteral("settings.pdf.service.bank_account"), normalized.bankAccount);
    return true;
}

int loadPlanningReminderLeadMinutes()
{
    const int value = Database::getSetting(QStringLiteral("settings.planning.reminder_lead_minutes"), 20).toInt();
    return qBound(1, value, 240);
}

bool savePlanningReminderLeadMinutes(int minutes, bool* changed)
{
    const int normalized = qBound(1, minutes, 240);
    const int current = loadPlanningReminderLeadMinutes();
    const bool hasChanges = current != normalized;
    if (changed) *changed = hasChanges;
    Database::setSetting(QStringLiteral("settings.planning.reminder_lead_minutes"), normalized);
    return true;
}

bool saveReportsPdfSettings(const ReportsPdfSettingsPayload& payload, bool* changed)
{
    const bool hasChanges =
        Database::getSetting(QStringLiteral("settings.reports.pdf.include_logo"), true).toBool() != payload.includeLogo ||
        Database::getSetting(QStringLiteral("settings.reports.pdf.include_phone"), true).toBool() != payload.includePhone ||
        Database::getSetting(QStringLiteral("settings.reports.pdf.use_custom_logo_position"), false).toBool() != payload.useCustomLogoPosition ||
        Database::getSetting(QStringLiteral("settings.reports.pdf.footer_text"), QString()).toString().trimmed() != payload.footerText.trimmed();
    if (changed) *changed = hasChanges;

    Database::setSetting(QStringLiteral("settings.reports.pdf.include_logo"), payload.includeLogo);
    Database::setSetting(QStringLiteral("settings.reports.pdf.include_phone"), payload.includePhone);
    Database::setSetting(QStringLiteral("settings.reports.pdf.use_custom_logo_position"), payload.useCustomLogoPosition);
    Database::setSetting(QStringLiteral("settings.reports.pdf.footer_text"), payload.footerText.trimmed());
    return true;
}


DirectoriesSettingsPayload loadDirectoriesSettings()
{
    DirectoriesSettingsPayload payload;
    payload.pdfDir = Database::getSetting(QStringLiteral("settings.int.directories.pdf_dir"), QString()).toString().trimmed();
    payload.invoicesDir = Database::getSetting(QStringLiteral("settings.int.directories.invoices_dir"), QString()).toString().trimmed();
    payload.reportsDir = Database::getSetting(QStringLiteral("settings.int.directories.reports_dir"), QString()).toString().trimmed();
    payload.orderPhotosDir = Database::getSetting(QStringLiteral("settings.int.directories.order_photos_dir"), QString()).toString().trimmed();
    return payload;
}

QString loadLegacySourceFolder()
{
    return Database::getSetting(QStringLiteral("settings.int.gsheets.source_folder"), QString()).toString().trimmed();
}

bool saveDirectoriesSettings(const DirectoriesSettingsPayload& payload, bool* changed)
{
    const QString newPdfDir = payload.pdfDir.trimmed();
    const QString newInvoicesDir = payload.invoicesDir.trimmed();
    const QString newReportsDir = payload.reportsDir.trimmed();
    const QString newOrderPhotosDir = payload.orderPhotosDir.trimmed();
    const bool hasChanges =
        Database::getSetting(QStringLiteral("settings.int.directories.pdf_dir"), QString()).toString().trimmed() != newPdfDir ||
        Database::getSetting(QStringLiteral("settings.int.directories.invoices_dir"), QString()).toString().trimmed() != newInvoicesDir ||
        Database::getSetting(QStringLiteral("settings.int.directories.reports_dir"), QString()).toString().trimmed() != newReportsDir ||
        Database::getSetting(QStringLiteral("settings.int.directories.order_photos_dir"), QString()).toString().trimmed() != newOrderPhotosDir;
    if (changed) *changed = hasChanges;

    Database::setSetting(QStringLiteral("settings.int.directories.pdf_dir"), newPdfDir);
    Database::setSetting(QStringLiteral("settings.int.directories.invoices_dir"), newInvoicesDir);
    Database::setSetting(QStringLiteral("settings.int.directories.reports_dir"), newReportsDir);
    Database::setSetting(QStringLiteral("settings.int.directories.order_photos_dir"), newOrderPhotosDir);
    return true;
}

bool saveLegacySourceFolder(const QString& sourceFolder, bool* changed)
{
    const QString normalized = sourceFolder.trimmed();
    const bool hasChanges = Database::getSetting(QStringLiteral("settings.int.gsheets.source_folder"), QString()).toString().trimmed() != normalized;
    if (changed) *changed = hasChanges;
    Database::setSetting(QStringLiteral("settings.int.gsheets.source_folder"), normalized);
    return true;
}


OpenDataBotSettingsPayload loadOpenDataBotSettings()
{
    OpenDataBotSettingsPayload payload;
    payload.demoMode = Database::getSetting(QStringLiteral("settings.int.odb.demo_mode"), false).toBool();
    payload.apiKey = Database::getSetting(QStringLiteral("settings.int.odb.api_key"), QString()).toString().trimmed();
    payload.apiEnabled = Database::getSetting(QStringLiteral("settings.int.odb.api_enabled"),
                                              payload.demoMode || !payload.apiKey.isEmpty()).toBool();
    payload.baseUrl = Database::getSetting(QStringLiteral("settings.int.odb.base_url"), QStringLiteral("https://opendatabot.com/api/v4")).toString().trimmed();
    payload.apiKeyParam = Database::getSetting(QStringLiteral("settings.int.odb.api_key_param"), QStringLiteral("apiKey")).toString().trimmed();
    payload.headerName = Database::getSetting(QStringLiteral("settings.int.odb.header_name"), QString()).toString().trimmed();
    payload.plateEndpoint = Database::getSetting(QStringLiteral("settings.int.odb.plate_endpoint"), QStringLiteral("transport")).toString().trimmed();
    payload.plateParam = Database::getSetting(QStringLiteral("settings.int.odb.plate_param"), QStringLiteral("number")).toString().trimmed();
    payload.vinEndpoint = Database::getSetting(QStringLiteral("settings.int.odb.vin_endpoint"), QStringLiteral("transport")).toString().trimmed();
    payload.vinParam = Database::getSetting(QStringLiteral("settings.int.odb.vin_param"), QStringLiteral("vin")).toString().trimmed();
    payload.testPlate = Database::getSetting(QStringLiteral("settings.int.odb.test_plate"), QStringLiteral("AE6190XC")).toString().trimmed();
    return payload;
}

bool saveOpenDataBotSettings(const OpenDataBotSettingsPayload& payload, bool* changed)
{
    const bool hasChanges =
        Database::getSetting(QStringLiteral("settings.int.odb.api_enabled"), false).toBool() != payload.apiEnabled ||
        Database::getSetting(QStringLiteral("settings.int.odb.demo_mode"), false).toBool() != payload.demoMode ||
        Database::getSetting(QStringLiteral("settings.int.odb.base_url"), QStringLiteral("https://opendatabot.com/api/v4")).toString().trimmed() != payload.baseUrl.trimmed() ||
        Database::getSetting(QStringLiteral("settings.int.odb.api_key"), QString()).toString().trimmed() != payload.apiKey.trimmed() ||
        Database::getSetting(QStringLiteral("settings.int.odb.api_key_param"), QStringLiteral("apiKey")).toString().trimmed() != payload.apiKeyParam.trimmed() ||
        Database::getSetting(QStringLiteral("settings.int.odb.header_name"), QString()).toString().trimmed() != payload.headerName.trimmed() ||
        Database::getSetting(QStringLiteral("settings.int.odb.plate_endpoint"), QStringLiteral("transport")).toString().trimmed() != payload.plateEndpoint.trimmed() ||
        Database::getSetting(QStringLiteral("settings.int.odb.plate_param"), QStringLiteral("number")).toString().trimmed() != payload.plateParam.trimmed() ||
        Database::getSetting(QStringLiteral("settings.int.odb.vin_endpoint"), QStringLiteral("transport")).toString().trimmed() != payload.vinEndpoint.trimmed() ||
        Database::getSetting(QStringLiteral("settings.int.odb.vin_param"), QStringLiteral("vin")).toString().trimmed() != payload.vinParam.trimmed() ||
        Database::getSetting(QStringLiteral("settings.int.odb.test_plate"), QStringLiteral("AE6190XC")).toString().trimmed() != payload.testPlate.trimmed();
    if (changed) *changed = hasChanges;

    Database::setSetting(QStringLiteral("settings.int.odb.api_enabled"), payload.apiEnabled);
    Database::setSetting(QStringLiteral("settings.int.odb.demo_mode"), payload.demoMode);
    Database::setSetting(QStringLiteral("settings.int.odb.base_url"), payload.baseUrl.trimmed());
    Database::setSetting(QStringLiteral("settings.int.odb.api_key"), payload.apiKey.trimmed());
    Database::setSetting(QStringLiteral("settings.int.odb.api_key_param"), payload.apiKeyParam.trimmed());
    Database::setSetting(QStringLiteral("settings.int.odb.header_name"), payload.headerName.trimmed());
    Database::setSetting(QStringLiteral("settings.int.odb.plate_endpoint"), payload.plateEndpoint.trimmed());
    Database::setSetting(QStringLiteral("settings.int.odb.plate_param"), payload.plateParam.trimmed());
    Database::setSetting(QStringLiteral("settings.int.odb.vin_endpoint"), payload.vinEndpoint.trimmed());
    Database::setSetting(QStringLiteral("settings.int.odb.vin_param"), payload.vinParam.trimmed());
    Database::setSetting(QStringLiteral("settings.int.odb.test_plate"), payload.testPlate.trimmed());
    return true;
}


bool loadWhiteTablesEnabled()
{
    return Database::getSetting(QStringLiteral("settings.ui.white_tables"), true).toBool();
}

bool saveWhiteTablesEnabled(bool enabled, bool* changed)
{
    const bool hasChanges = Database::getSetting(QStringLiteral("settings.ui.white_tables"), true).toBool() != enabled;
    if (changed) *changed = hasChanges;
    Database::setSetting(QStringLiteral("settings.ui.white_tables"), enabled);
    return true;
}

} // namespace GeneralSettingsService
