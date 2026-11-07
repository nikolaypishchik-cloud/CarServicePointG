#pragma once

#include <QString>
#include <QDate>

#include <functional>

class QWidget;

class OrderDialogPdfService
{
public:
    struct PreviewResult {
        bool ok = false;
        QString outFile;
        QString error;
        bool opened = false;
    };

    using ExportCallback = std::function<bool(const QString& outFile, QString* errorText)>;

    static PreviewResult exportPreviewAndOpen(int orderNumber, QWidget* owner, const ExportCallback& exportCallback, bool partsReport = false, const QDate& createdOn = {}, bool openAfterExport = true);
    static bool openPreviewFile(const QString& outFile, QWidget* owner);
};
