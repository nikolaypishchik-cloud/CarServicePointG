#pragma once

#include <QString>

namespace GeneralSettingsService {

struct ElitCredentials {
    QString login;
    QString password;
    QString apiKey; // Legacy encrypted value, never sent to BuyerService.
    QString company = QStringLiteral("ELIT_UA");
    bool rememberPassword = false;
};

bool loadElitCredentials(ElitCredentials* credentials, QString* error = nullptr);
bool saveElitCredentials(const ElitCredentials& credentials, QString* error = nullptr);

struct ReportsPdfSettingsPayload {
    bool includeLogo = true;
    bool includePhone = true;
    bool useCustomLogoPosition = false;
    QString footerText;
};

struct PdfServiceRequisitesPayload {
    QString name;
    QString legalAddress;
    QString actualAddress;
    QString phone;
    QString email;
    QString website;
    QString edrpou;
    QString bankAccount;
};

struct DirectoriesSettingsPayload {
    QString pdfDir;
    QString invoicesDir;
    QString reportsDir;
    QString orderPhotosDir;
};

struct OpenDataBotSettingsPayload {
    bool apiEnabled = false;
    bool demoMode = false;
    QString baseUrl;
    QString apiKey;
    QString apiKeyParam;
    QString headerName;
    QString plateEndpoint;
    QString plateParam;
    QString vinEndpoint;
    QString vinParam;
    QString testPlate;
};

bool saveUpdatesAuto(bool enabled, bool* changed = nullptr);
bool loadUpdatesAuto();
bool saveReportsPdfSettings(const ReportsPdfSettingsPayload& payload, bool* changed = nullptr);
ReportsPdfSettingsPayload loadReportsPdfSettings();
PdfServiceRequisitesPayload defaultPdfServiceRequisites();
bool ensurePdfServiceRequisitesDefaults(bool* changed = nullptr);
bool savePdfServiceRequisites(const PdfServiceRequisitesPayload& payload, bool* changed = nullptr);
PdfServiceRequisitesPayload loadPdfServiceRequisites();
bool savePlanningReminderLeadMinutes(int minutes, bool* changed = nullptr);
int loadPlanningReminderLeadMinutes();
bool saveDirectoriesSettings(const DirectoriesSettingsPayload& payload, bool* changed = nullptr);
DirectoriesSettingsPayload loadDirectoriesSettings();
bool loadWhiteTablesEnabled();
QString loadLegacySourceFolder();
bool saveLegacySourceFolder(const QString& sourceFolder, bool* changed = nullptr);
bool saveOpenDataBotSettings(const OpenDataBotSettingsPayload& payload, bool* changed = nullptr);
OpenDataBotSettingsPayload loadOpenDataBotSettings();
bool saveWhiteTablesEnabled(bool enabled, bool* changed = nullptr);

} // namespace GeneralSettingsService
