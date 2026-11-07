#include "ui/tabs/orders/WorkPartsDialogExcelSupport.h"

#include "core/import/ImportJob.h"
#include "core/import/UniversalInvoiceColumnDetector.h"
#include "runtime/AsyncThreadScope.h"
#include "core/diagnostics/StructuredDiagnosticsLogger.h"
#include "ui/common/widgets/InvoiceImportProgressDialog.h"

#include <QApplication>
#include <QCoreApplication>
#include <QFileInfo>
#include <QObject>
#include <QPointer>
#include <QRect>
#include <QTimer>
#include <QThread>
#include <QtGlobal>

#include <atomic>
#include <functional>
#include <memory>
#include <exception>

namespace WorkPartsDialogExcelSupport {

// P0 Import p1: invoice parsing/PDF attachment preparation now lives in src/core/import/InvoiceAttachmentPdfService.
// This UI module keeps only the dialog-owned scanner overlay and async job callback wiring.

void runExcelReadAsync(QWidget* owner,
                       const QString& progressText,
                       const QString& filePath,
                       std::function<void(const ExcelReadResult&, const QString&)> onDone,
                       bool showOverlay)
{
    Q_UNUSED(progressText);
    Q_UNUSED(showOverlay); // Every entry point uses the same cancellable window.
    auto* job = new InvoiceImport::ImportJob(owner ? static_cast<QObject*>(owner) : qApp);
    const QPointer<QWidget> guard(owner);
    QPointer<InvoiceImport::ImportJob> jobGuard(job);
    auto* progress = new InvoiceImportProgressDialog(owner, job,
        [guard, jobGuard, onDone = std::move(onDone)](const InvoiceImport::ImportJobResult& result) {
            if (jobGuard) jobGuard->deleteLater();
            if (!guard) return;
            onDone(result.data, result.ok() ? QString() : cancelledReadMarker());
        });
    // Keep the window alive through its callback, then release it with the job.
    QObject::connect(job, &QObject::destroyed, progress, &QObject::deleteLater);
    if (owner) QObject::connect(owner, &QObject::destroyed, job, [jobGuard] { if (jobGuard) jobGuard->cancel(); });
    InvoiceImport::ImportJobRequest request;
    request.filePath = filePath; request.timeoutMs = 60000;
    progress->start(request);
}



void runConfirmedVatMappingParseAsync(QWidget* owner,
                                      const QList<QStringList>& rows,
                                      const InvoiceImport::InvoiceColumnMappingPreview& confirmedMapping,
                                      const QString& documentDate,
                                      const NormalizeTextFn& normalizeSentence,
                                      const ParseLooseNumberFn& parseLooseNumber,
                                      const MoneyTextFn& moneyText,
                                      std::function<void(const QList<ImportedPartDraft>&, const QString&)> onDone)
{
    const QPointer<QWidget> guard(owner);
    auto cancelToken = std::make_shared<std::atomic_bool>(false);
    auto draftsResult = std::make_shared<QList<ImportedPartDraft>>();
    auto errorResult = std::make_shared<QString>();

    QObject* workerOwner = owner ? static_cast<QObject*>(owner) : static_cast<QObject*>(qApp);
    if (owner) {
        QObject::connect(owner, &QObject::destroyed, owner, [cancelToken]() {
            cancelToken->store(true);
        });
    }

    QThread* thread = AsyncThreadScope::startWorker(
        workerOwner,
        QStringLiteral("invoice_manual_vat_mapping_parse_worker"),
        cancelToken,
        [rows, confirmedMapping, documentDate, normalizeSentence, parseLooseNumber, moneyText, draftsResult, errorResult]
        (const std::shared_ptr<std::atomic_bool>& token) mutable {
            if (AsyncThreadScope::isCancellationRequested(token)) {
                *errorResult = QString::fromUtf8("Ручний mapping імпорту скасовано до старту фонового розбору.");
                return;
            }
            if (QCoreApplication::instance() && QThread::currentThread() == QCoreApplication::instance()->thread()) {
                *errorResult = QString::fromUtf8("Ручний mapping заблоковано: розбір не має виконуватися в UI-потоці.");
                return;
            }
            QString error;
            *draftsResult = InvoiceImport::UniversalInvoiceColumnDetector::parseWithConfirmedVatMapping(
                rows,
                confirmedMapping,
                normalizeSentence,
                parseLooseNumber,
                moneyText,
                &error,
                documentDate);
            *errorResult = error;
            if (AsyncThreadScope::isCancellationRequested(token)) {
                draftsResult->clear();
                *errorResult = QString::fromUtf8("Ручний mapping імпорту скасовано після фонового розбору.");
            }
        });

    QObject::connect(thread, &QThread::finished, qApp,
                     [guard, cancelToken, draftsResult, errorResult, onDone = std::move(onDone)]() mutable {
        if (cancelToken->load() || !guard) return;
        onDone(*draftsResult, *errorResult);
    });
    thread->start();
}

} // namespace WorkPartsDialogExcelSupport
