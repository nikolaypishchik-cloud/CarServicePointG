#pragma once

#include <QString>
#include <QStringList>

struct LegacyImportAuditReport;
struct LegacyImportBundle;

class LegacyImportPolicySupport
{
public:
    static bool validateSourceSelection(const QString& sourcePath, QString* errorText = nullptr);
    static bool validateExecutePolicy(const LegacyImportBundle& bundle,
                                      const LegacyImportAuditReport& auditReport,
                                      QString* errorText = nullptr,
                                      QStringList* warnings = nullptr);
};
