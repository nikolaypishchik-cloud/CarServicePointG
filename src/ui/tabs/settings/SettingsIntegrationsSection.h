#pragma once

#include <QString>
#include <QWidget>
#include <functional>

class QPushButton;

class SettingsIntegrationsSection
{
public:
    static QWidget* build(const QString& id,
                          QWidget* owner,
                          const std::function<QWidget*()>& googleSheetsBuilder,
                          const std::function<QWidget*()>& autoBackupBuilder,
                          const std::function<QWidget*()>& syncBuilder);
    static QWidget* buildDiagnosticReportPage(QWidget* owner);
    static void sendDiagnosticReport(QWidget* owner, QPushButton* sendButton);
};
