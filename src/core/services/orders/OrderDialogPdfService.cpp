#include "core/services/orders/OrderDialogPdfService.h"

#include "core/services/storage/StorageDirectoriesGuard.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QDesktopServices>
#include <QObject>
#include <QTemporaryFile>
#include <QUrl>
#include <QWidget>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSettings>

namespace {
static QString orderPdfPreviewPath(int orderNumber, QString* errorText, bool partsReport, const QDate& createdOn)
{
    if (!createdOn.isValid()) {
        if (errorText) *errorText = QObject::tr("Не визначено дату створення наряду для назви PDF.");
        return {};
    }
    const QString fileName = QStringLiteral("%1_%2%3.pdf")
        .arg(orderNumber > 0 ? orderNumber : 0).arg(createdOn.toString("dd.MM.yyyy"),
            partsReport ? QStringLiteral("_parts_report") : QString());
    return StorageDirectoriesGuard::managedFilePath(StorageDirectoriesGuard::StorageKind::OrderPdf,
                                                   orderNumber,
                                                   QStringLiteral("order"),
                                                   fileName,
                                                   errorText);
}
}

OrderDialogPdfService::PreviewResult OrderDialogPdfService::exportPreviewAndOpen(int orderNumber, QWidget* owner, const ExportCallback& exportCallback, bool partsReport, const QDate& createdOn, bool openAfterExport)
{
    PreviewResult result;
    QString pathError;
    result.outFile = orderPdfPreviewPath(orderNumber, &pathError, partsReport, createdOn);
    if (result.outFile.trimmed().isEmpty()) {
        result.error = pathError.trimmed().isEmpty()
            ? StorageDirectoriesGuard::missingDirectoriesMessage()
            : pathError;
        return result;
    }

    const QFileInfo fi(result.outFile);
    QDir dir(fi.absolutePath());
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        result.error = QObject::tr("Не вдалося створити папку для PDF-перегляду:\n%1")
                           .arg(QDir::toNativeSeparators(fi.absolutePath()));
        return result;
    }

    QTemporaryFile probe(dir.filePath(QStringLiteral(".__cs_order_pdf_probe_XXXXXX.tmp")));
    probe.setAutoRemove(true);
    if (!probe.open()) {
        result.error = QObject::tr("Немає прав на запис у папку PDF-перегляду:\n%1")
                           .arg(QDir::toNativeSeparators(fi.absolutePath()));
        return result;
    }
    probe.close();

    QString exportError;
    if (!exportCallback || !exportCallback(result.outFile, &exportError)) {
        result.error = exportError.trimmed().isEmpty()
            ? QObject::tr("PDF наряду не був коректно сформований:\n%1").arg(QDir::toNativeSeparators(result.outFile))
            : exportError;
        return result;
    }

    const QFileInfo exportedInfo(result.outFile);
    if (!exportedInfo.exists() || exportedInfo.size() <= 0) {
        result.error = QObject::tr("PDF наряду не був коректно сформований:\n%1")
                           .arg(QDir::toNativeSeparators(result.outFile));
        return result;
    }

    result.ok = true;
    result.opened = openAfterExport && openPreviewFile(result.outFile, owner);
    return result;
}

bool OrderDialogPdfService::openPreviewFile(const QString& outFile, QWidget* owner)
{
    const QString absPath = QFileInfo(outFile).absoluteFilePath();
    if (!QFileInfo(absPath).isFile()) return false;
#ifdef Q_OS_WIN
    // Avoid Acrobat's external Font Capture failure without changing file associations.
    QStringList candidates;
    for (const QString& hive : {QStringLiteral("HKEY_CURRENT_USER"), QStringLiteral("HKEY_LOCAL_MACHINE")}) {
        QSettings paths(hive + QStringLiteral("\\Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\msedge.exe"), QSettings::NativeFormat);
        candidates << paths.value(QStringLiteral(".")).toString();
    }
    for (const char* root : {"ProgramFiles(x86)", "ProgramFiles", "LOCALAPPDATA"})
        candidates << QDir(qEnvironmentVariable(root)).filePath(QStringLiteral("Microsoft/Edge/Application/msedge.exe"));
    for (const QString& browser : candidates) {
        if (browser.isEmpty() || !QFileInfo(browser).isFile()) continue;
        QProcess process;
        auto environment = QProcessEnvironment::systemEnvironment();
        for (const QString& key : environment.keys())
            if (key.startsWith(QStringLiteral("QT_"))) environment.remove(key);
        process.setProcessEnvironment(environment);
        process.setProgram(browser);
        process.setArguments({QUrl::fromLocalFile(absPath).toString()});
        process.setWorkingDirectory(QFileInfo(browser).absolutePath());
        if (process.startDetached()) return true;
    }
#endif
    const auto topLevels = QApplication::topLevelWidgets();
    for (QWidget* w : topLevels) {
        if (!w || w == owner) continue;
        if (!w->isVisible()) continue;
        if (!w->isModal()) continue;
        if (owner && (w == owner->window() || w->isAncestorOf(owner) || owner->isAncestorOf(w))) {
            return false;
        }
    }
    return QDesktopServices::openUrl(QUrl::fromLocalFile(absPath));
}
