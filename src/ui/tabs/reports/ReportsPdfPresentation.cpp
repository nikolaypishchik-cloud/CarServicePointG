#include "ui/tabs/reports/ReportsPdfPresentation.h"
#include <QDesktopServices>
#include <QDir>
#include <QMessageBox>
#include <QUrl>

namespace ReportsPdfPresentation {
bool openExportedPdf(QWidget* parent, const ReportsPdfRenderer::ExportResult& result)
{
    if (!result.success || result.path.trimmed().isEmpty()) {
        if (parent) {
            QMessageBox::warning(parent,
                                 QString::fromUtf8("PDF"),
                                 result.errorText.trimmed().isEmpty()
                                     ? QString::fromUtf8("Не вдалося підготувати PDF")
                                     : result.errorText);
        }
        return false;
    }

    const bool opened = QDesktopServices::openUrl(QUrl::fromLocalFile(result.path));
    if (!opened && parent) {
        QMessageBox::warning(parent, QString::fromUtf8("PDF збережено"),
                             QString::fromUtf8("PDF збережено, але не вдалося відкрити автоматично:\n%1").arg(QDir::toNativeSeparators(result.path)));
    }
    return opened;
}

}
