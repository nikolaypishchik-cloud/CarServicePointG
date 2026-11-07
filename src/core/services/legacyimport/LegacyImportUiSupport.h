#pragma once

#include <QString>

struct LegacyImportAuditReport;
struct LegacyImportVerifyReport;
struct LegacyMigrationResult;

class LegacyImportUiSupport
{
public:
    static QString buildPreviewDialogText(const LegacyImportAuditReport& report, const QString& reportDirPath);
    static QString buildSuccessDialogText(const LegacyMigrationResult& result, const QString& reportDirPath);
    static QString buildBlockedDialogText(const LegacyImportAuditReport& report, const QString& reportDirPath);
};
