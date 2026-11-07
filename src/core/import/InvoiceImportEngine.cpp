#include "core/io/SupplierInvoiceTemplateStore.h"
#include "core/util/DateFormats.h"
#include "core/import/InvoiceImportEngine.h"
#include "core/import/InvoiceRecognitionProfiles.h"
#include "core/import/parsers/InvoiceParsers.h"
#include "core/diagnostics/RuntimeLoggingControl.h"

#include "core/services/orders/PartImportTextSupport.h"
#include "core/util/FormatUtils.h"
#include "core/util/StoreNameNormalizer.h"

#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFileInfoList>
#include <QHash>
#include <QImage>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QList>
#include <QMap>
#include <QMutex>
#include <QMutexLocker>
#if defined(CS_HAS_QT_PDF) && CS_HAS_QT_PDF
#include <QPdfDocument>
#endif
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QRegularExpressionMatchIterator>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QStringConverter>
#include <QSize>
#include <QTemporaryDir>
#include <QTextStream>
#include <QThread>
#include <QVector>

#if defined(Q_OS_WIN)
#include <windows.h>
#endif

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>


// P1 split: Invoice import monolith is preserved through ordered implementation fragments.
// Split boundaries are generated outside raw string literals; do not reorder.
#include "core/import/internal/PdfTextExtractionStage.ipp"
#include "core/import/internal/ExcelExtractionStage.ipp"
#include "core/import/internal/InvoiceVendorDetector.ipp"
#include "core/import/internal/InvoiceRowsNormalizer.ipp"
#include "core/import/internal/InvoicePriceColumnResolver.ipp"
#include "core/import/internal/InvoiceImportPersistenceStage.ipp"
#include "core/import/internal/InvoiceImportDiagnostics.ipp"

QString InvoiceImport::invoicePdfText(const QString& path, QString* errorText)
{
    return extractPdfTextViaLocalRuntimeTool(path, QCoreApplication::applicationDirPath(), nullptr, errorText);
}

bool InvoiceImport::validateInvoicePdf(const QString& path, QString* errorText)
{
    if (errorText) errorText->clear();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() < 32 || !file.read(8).startsWith("%PDF-")) {
        if (errorText) *errorText = QString::fromUtf8("Файл не є коректним PDF.");
        return false;
    }
    file.seek(qMax<qint64>(0, file.size() - 4096));
    const QByteArray tail = file.readAll();
    if (!tail.trimmed().endsWith("%%EOF") || !tail.contains("startxref")) {
        if (errorText) *errorText = QString::fromUtf8("PDF неповний або пошкоджений.");
        return false;
    }
    file.close();
    if (rejectBlockingImportProcessOnUiThread(QStringLiteral("invoice_pdf_validation"), errorText)) return false;
#if defined(CS_HAS_QT_PDF) && CS_HAS_QT_PDF
    QPdfDocument document;
    if (document.load(path) == QPdfDocument::Error::None && document.pageCount() > 0) return true;
#else
    // Bundled Poppler cannot read some Windows paths beyond MAX_PATH, while
    // Qt can. Validate identical bytes under a short temporary filename.
    QTemporaryDir validationDirectory;
    QString validationPath=path;
#if defined(Q_OS_WIN)
    if(QFileInfo(path).absoluteFilePath().size()>=240) {
        if(!validationDirectory.isValid() || !QFile::copy(path,validationDirectory.filePath("invoice.pdf"))) {
            if(errorText)*errorText=QString::fromUtf8("Не вдалося підготувати тимчасову копію PDF для перевірки.");
            return false;
        }
        validationPath=validationDirectory.filePath("invoice.pdf");
    }
#endif
    const QString appDir = QCoreApplication::applicationDirPath();
    for (const auto& exe : candidatePdfRuntimeExecutables(appDir, {QStringLiteral("pdfinfo.exe"), QStringLiteral("pdfinfo")})) {
        QProcess process;
        process.setProgram(exe);
        process.setArguments({validationPath});
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("PATH"), pdfImportRuntimeToolSearchDirs(appDir).join(QDir::listSeparator())
            + QDir::listSeparator() + env.value(QStringLiteral("PATH")));
        process.setProcessEnvironment(env);
        const auto result = runImportProcessIncrementally(process, 3000, 10000, QStringLiteral("invoice_pdf_validation"));
        if (result.started && !result.timedOut && result.exitStatus == QProcess::NormalExit && result.exitCode == 0
            && result.stdErr.trimmed().isEmpty()
            && QRegularExpression(QStringLiteral("Pages:\\s*[1-9][0-9]*")).match(QString::fromUtf8(result.stdOut)).hasMatch()) return true;
    }
#endif
    if (errorText) *errorText = QString::fromUtf8("Не вдалося перевірити структуру PDF. Потрібен доступний засіб перевірки PDF.");
    return false;
}
