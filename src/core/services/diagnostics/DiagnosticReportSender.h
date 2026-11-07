#pragma once

#include <QString>
#include <QStringList>

class DiagnosticReportSender
{
public:
    struct Result {
        bool sent = false;
        bool localCopySaved = false;
        QString recipient;
        QString sender;
        QString reportPath;
        QString message;
        QString errorText;
        QStringList attachments;
    };

    static QString recipientEmail();
    static QString senderEmail();
    static Result send();
    static Result createLocalReportOnly(const QString& reason);
};
