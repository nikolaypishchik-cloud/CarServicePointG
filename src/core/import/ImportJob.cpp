#include "core/import/ImportJob.h"
#include "core/diagnostics/RuntimeLoggingControl.h"

#include "core/import/InvoiceImportEngine.h"
#include "core/import/InvoiceAttachmentPdfService.h"
#include "core/import/UniversalInvoiceColumnDetector.h"
#include "core/import/InvoiceRecognitionProfiles.h"
#include "core/util/FormatUtils.h"
#include "runtime/AsyncThreadScope.h"
#include "runtime/RuntimeJobDispatcher.h"
#include "core/diagnostics/StructuredDiagnosticsLogger.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QMetaObject>
#include <QTimer>
#include <QDebug>
#include <QThread>
#include <QMetaType>
#include <QDateTime>

#include <exception>
#include <utility>

namespace InvoiceImport {

namespace {

QString defaultCorrelationId(const QString& filePath)
{
    const QString base = QFileInfo(filePath).fileName().trimmed();
    return QStringLiteral("invoice_import:%1:%2")
        .arg(base.isEmpty() ? QStringLiteral("unknown") : base,
             QString::number(QDateTime::currentMSecsSinceEpoch()));
}

ImportJobResult makeTerminalResult(const ImportJobRequest& request,
                                   ImportJobStatus status,
                                   ImportJobErrorCode code,
                                   const QString& message,
                                   qint64 elapsedMs = 0,
                                   const QString& stage = QStringLiteral("finished"),
                                   int percent = 100)
{
    ImportJobResult result;
    result.correlationId = request.correlationId;
    result.filePath = request.filePath;
    result.status = status;
    result.stage = stage;
    result.percent = qBound(0, percent, 100);
    result.elapsedMs = elapsedMs;
    result.error.code = code;
    result.error.stage = stage;
    result.error.message = message;
    return result;
}

void postProgress(const QPointer<ImportJob>& job,
                  const ImportJobRequest& request,
                  const std::shared_ptr<std::atomic_bool>& token,
                  int percent,
                  const QString& stage,
                  const QString& message = QString(),
                  bool cancelRequested = false)
{
    if (job.isNull()) return;
    ImportJobProgress progress;
    progress.correlationId = request.correlationId;
    progress.filePath = request.filePath;
    progress.stage = stage;
    progress.message = message;
    progress.percent = qBound(0, percent, 100);
    progress.cancelRequested = cancelRequested;
    QMetaObject::invokeMethod(job.data(), [job, progress, token]() {
        if (job.isNull() || !job->isRunning() || (token && token->load())) return;
        emit job->progressChanged(progress);
    }, Qt::QueuedConnection);
}

static double parseLooseNumberForImportJob(QString text, bool* ok = nullptr)
{
    text = text.trimmed();
    text.remove(QChar(0x00A0));
    text.remove(QLatin1Char(' '));
    text.replace(QLatin1Char(','), QLatin1Char('.'));
    return text.toDouble(ok);
}

static QString normalizeImportJobText(const QString& text)
{
    return text.simplified();
}

static bool importJobSourceIsSpreadsheetInvoice(const QString& filePath)
{
    const QString ext = QFileInfo(filePath).suffix().trimmed().toLower();
    return ext == QStringLiteral("xlsx") || ext == QStringLiteral("xls") || ext == QStringLiteral("xlsm") ||
           ext == QStringLiteral("xltx") || ext == QStringLiteral("xltm") || ext == QStringLiteral("csv") ||
           ext == QStringLiteral("txt");
}

} // namespace

ImportJob::ImportJob(QObject* parent)
    : QObject(parent)
{
    qRegisterMetaType<InvoiceImport::ImportJobProgress>("InvoiceImport::ImportJobProgress");
    qRegisterMetaType<InvoiceImport::ImportJobResult>("InvoiceImport::ImportJobResult");
}

ImportJob::~ImportJob()
{
    detachWorkerForOwnerShutdown();
}

bool ImportJob::start(const ImportJobRequest& rawRequest)
{
    if (m_running) {
        return false;
    }

    ++m_generation;
    const quint64 generation = m_generation;
    m_running = true;
    m_status = ImportJobStatus::Running;
    m_elapsed.restart();
    m_request = rawRequest;
    if (m_request.correlationId.trimmed().isEmpty()) {
        m_request.correlationId = defaultCorrelationId(m_request.filePath);
    }

    const QFileInfo fi(m_request.filePath);
    if (m_request.filePath.trimmed().isEmpty() || !fi.exists() || !fi.isFile()) {
        const ImportJobResult result = makeTerminalResult(
            m_request,
            ImportJobStatus::Failed,
            ImportJobErrorCode::InvalidRequest,
            QString::fromUtf8("Файл накладної не знайдено або шлях порожній: %1").arg(m_request.filePath),
            0,
            QStringLiteral("invalid_request"),
            100);
        QTimer::singleShot(0, this, [this, generation, result]() {
            if (generation == m_generation) emitTerminalResult(result);
        });
        return false;
    }

    const auto runtimeTicket = RuntimeJob::RuntimeJobDispatcher::instance().startLatest(
        RuntimeJob::Family::Import,
        QStringLiteral("ImportJob:%1").arg(m_request.correlationId),
        qMax(1000, m_request.timeoutMs),
        70);
    m_cancelToken = runtimeTicket.cancelToken;
    const auto result = std::make_shared<ImportJobResult>();
    result->correlationId = m_request.correlationId;
    result->filePath = m_request.filePath;
    result->status = ImportJobStatus::Running;
    result->stage = QStringLiteral("queued");
    result->percent = 0;

    const QPointer<ImportJob> jobGuard(this);
    const ImportJobRequest request = m_request;
    postProgress(jobGuard, request, m_cancelToken, 0, QStringLiteral("queued"), QString::fromUtf8("Імпорт накладної поставлено у чергу."));

    m_thread = AsyncThreadScope::startWorker(this,
                                             QStringLiteral("invoice_import_job_worker"),
                                             m_cancelToken,
                                             [jobGuard, request, result, runtimeTicket](const std::shared_ptr<std::atomic_bool>& cancelToken) {
        RuntimeJob::RuntimeJobDispatcher::instance().markRunning(runtimeTicket);
        result->stage = QStringLiteral("started");
        result->percent = 0;
        postProgress(jobGuard, request, cancelToken, 0, QStringLiteral("started"), QString::fromUtf8("Почато читання накладної."));
        if (AsyncThreadScope::isCancellationRequested(cancelToken)) {
            result->status = ImportJobStatus::Cancelled;
            result->stage = QStringLiteral("cancelled");
            result->percent = 100;
            result->error.code = ImportJobErrorCode::Cancelled;
            result->error.stage = result->stage;
            result->error.message = QString::fromUtf8("Імпорт накладної скасовано до старту читання.");
            postProgress(jobGuard, request, cancelToken, 100, result->stage, result->error.message, true);
            return;
        }

        try {
            if (QCoreApplication::instance() &&
                QThread::currentThread() == QCoreApplication::instance()->thread()) {
                result->status = ImportJobStatus::Failed;
                result->stage = QStringLiteral("failed");
                result->percent = 100;
                result->error.code = ImportJobErrorCode::EngineError;
                result->error.stage = result->stage;
                result->error.message = QString::fromUtf8("P0 import p4: ImportJob не має виконувати синхронний importer у UI-потоці.");
                result->error.technicalDetails = QStringLiteral("P0_import_p4_import_job_worker_thread_guard");
                postProgress(jobGuard, request, cancelToken, 100, result->stage, result->error.message);
                return;
            }

            QString errorText;
            result->stage = QStringLiteral("reading");
            result->percent = 0;
            postProgress(jobGuard, request, cancelToken, 0, result->stage, QString::fromUtf8("Читання та розпізнавання файлу накладної."));
            if (AsyncThreadScope::isCancellationRequested(cancelToken)) {
                result->status = ImportJobStatus::Cancelled;
                result->stage = QStringLiteral("cancelled_before_read");
                result->percent = 100;
                result->error.code = ImportJobErrorCode::Cancelled;
                result->error.stage = result->stage;
                result->error.message = QString::fromUtf8("Імпорт накладної скасовано перед читанням файлу.");
                postProgress(jobGuard, request, cancelToken, 100, result->stage, result->error.message, true);
                return;
            }
            result->data = readInvoiceRows(request.filePath, &errorText, cancelToken);
            if (AsyncThreadScope::isCancellationRequested(cancelToken)) {
                result->status = ImportJobStatus::Cancelled;
                result->stage = QStringLiteral("cancelled");
                result->percent = 100;
                result->error.code = ImportJobErrorCode::Cancelled;
                result->error.stage = result->stage;
                result->error.message = QString::fromUtf8("Імпорт накладної скасовано.");
                postProgress(jobGuard, request, cancelToken, 100, result->stage, result->error.message, true);
                return;
            }
            if (errorText.trimmed().isEmpty() && result->data.rows.isEmpty() && result->data.parsedPartDrafts.isEmpty())
                errorText = QString::fromUtf8("У файлі не знайдено позицій накладної. Перевірте файл або оберіть іншу накладну.");
            if (!errorText.trimmed().isEmpty()) {
                result->status = ImportJobStatus::Failed;
                result->stage = QStringLiteral("failed");
                result->percent = 100;
                result->error.code = ImportJobErrorCode::EngineError;
                result->error.stage = result->stage;
                result->error.message = errorText.trimmed();
                result->error.technicalDetails = QStringLiteral("InvoiceImportEngine::readInvoiceRows returned an error.");
                postProgress(jobGuard, request, cancelToken, 100, result->stage, result->error.message);
                return;
            }

            result->stage = QStringLiteral("detecting_columns");
            result->percent = 50;
            postProgress(jobGuard, request, cancelToken, 50, result->stage, QString::fromUtf8("Визначення колонок, перевірка ПДВ-ціни та сум у фоні."));
            if (!result->data.rows.isEmpty()) {
                const bool sourceRequiresMapping = result->data.manualMappingRequired;
                const auto sourcePreview = result->data.columnMappingPreview;
                UniversalInvoiceDetectionResult detection;
                if (sourceRequiresMapping) {
                    detection.performed = true;
                    detection.columnMappingPreview = UniversalInvoiceColumnDetector::detect(
                        result->data.rows, result->data.highlightedColumns,
                        [](QString s, bool* ok) { return parseLooseNumberForImportJob(s, ok); });
                } else {
                    detection = UniversalInvoiceColumnDetector::analyzeAndParse(
                    result->data.rows,
                    result->data.highlightedColumns,
                    [](const QString& s) { return normalizeImportJobText(s); },
                    [](QString s, bool* ok) { return parseLooseNumberForImportJob(s, ok); },
                    [](double value) { return FormatUtils::money(value, 2); },
                    result->data.documentDate);
                }
                result->data.columnMappingPreview = detection.columnMappingPreview;
                result->data.parsedPartDrafts = detection.parsedPartDrafts;
                result->data.automaticImportError = detection.automaticImportError;
                result->data.automaticImportAllowed = detection.automaticImportAllowed;
                result->data.manualMappingRequired = detection.manualMappingRequired;
                result->data.columnDetectionPerformed = detection.performed;
                if (sourceRequiresMapping) {
                    if (sourcePreview.headerRow >= 0) result->data.columnMappingPreview = sourcePreview;
                    else result->data.columnMappingPreview.warnings.append(sourcePreview.warnings);
                    result->data.columnMappingPreview.priceWithVatConfirmed = false;
                    result->data.columnMappingPreview.automaticAllowed = false;
                    result->data.columnMappingPreview.manualColumnSelectionRequired = true;
                    result->data.manualMappingRequired = true;
                    result->data.automaticImportAllowed = false;
                    result->data.parsedPartDrafts.clear();
                    result->data.automaticImportError = QString::fromUtf8("Підтвердьте колонки нового формату PDF.");
                }
                auto& data = result->data;
                auto& preview = data.columnMappingPreview;
                preview.templateKey = InvoiceRecognitionProfiles::keyFor(data.rows, preview.headerRow, data.documentNumber);
                InvoiceColumnMappingPreview saved;
                if (InvoiceRecognitionProfiles::load(preview.templateKey, &saved)) {
                    saved.headerRow = preview.headerRow;
                    QString profileError;
                    const bool valid = InvoiceRecognitionProfiles::validateRows(data.rows, saved,
                        [](QString s, bool* ok) { return parseLooseNumberForImportJob(s, ok); }, &profileError);
                    data.parsedPartDrafts.clear();
                    if (valid) data.parsedPartDrafts = UniversalInvoiceColumnDetector::parseWithConfirmedVatMapping(
                        data.rows, saved, [](const QString& s) { return normalizeImportJobText(s); },
                        [](QString s, bool* ok) { return parseLooseNumberForImportJob(s, ok); },
                        [](double v) { return FormatUtils::money(v, 2); }, &profileError, data.documentDate);
                    data.automaticImportAllowed = valid && profileError.isEmpty() && !data.parsedPartDrafts.isEmpty();
                    data.manualMappingRequired = !data.automaticImportAllowed;
                    data.automaticImportError = profileError;
                    preview = saved;
                    preview.automaticAllowed = data.automaticImportAllowed;
                    if (!data.automaticImportAllowed) {
                        preview.templateApplied = false;
                        preview.priceWithVatConfirmed = false;
                        preview.warnings.append(profileError);
                    }
                }
            }

            if (importJobSourceIsSpreadsheetInvoice(request.filePath)) {
                result->stage = QStringLiteral("rendering_canonical_pdf");
                result->percent = 75;
                postProgress(jobGuard, request, cancelToken, 75, result->stage, QString::fromUtf8("Створення PDF накладної у єдиному форматі."));
                QString pdfError;
                const QString detectedSupplier = result->data.parsedPartDrafts.isEmpty()
                    ? QString()
                    : result->data.parsedPartDrafts.first().store.trimmed();
                result->data.canonicalPdfAttachmentPath = ensureInvoicePdfAttachmentFromRows(
                    request.filePath,
                    result->data,
                    result->data.documentNumber,
                    result->data.documentDate,
                    detectedSupplier,
                    &pdfError);
                result->data.canonicalPdfAttachmentError = pdfError.trimmed();
                if (result->data.canonicalPdfAttachmentPath.trimmed().isEmpty() && !result->data.canonicalPdfAttachmentError.isEmpty()) {
                    qWarning().noquote() << QStringLiteral("IMPORT_JOB canonical_pdf_prepare_failed source=")
                                         << QFileInfo(request.filePath).fileName()
                                         << QStringLiteral(" error=")
                                         << result->data.canonicalPdfAttachmentError;
                }
            }
            if (AsyncThreadScope::isCancellationRequested(cancelToken)) {
                result->status = ImportJobStatus::Cancelled;
                result->stage = QStringLiteral("cancelled_after_detection");
                result->percent = 100;
                result->error.code = ImportJobErrorCode::Cancelled;
                result->error.stage = result->stage;
                result->error.message = QString::fromUtf8("Імпорт накладної скасовано після фонового визначення колонок.");
                postProgress(jobGuard, request, cancelToken, 100, result->stage, result->error.message, true);
                return;
            }
            result->status = ImportJobStatus::Succeeded;
            result->stage = QStringLiteral("finished");
            result->percent = 100;
            postProgress(jobGuard, request, cancelToken, 100, result->stage, QString::fromUtf8("Імпорт накладної завершено."));
        } catch (const std::exception& ex) {
            result->status = ImportJobStatus::Failed;
            result->stage = QStringLiteral("exception");
            result->percent = 100;
            result->error.code = ImportJobErrorCode::Exception;
            result->error.stage = result->stage;
            result->error.message = QString::fromUtf8("Непередбачена помилка імпорту накладної.");
            result->error.technicalDetails = QString::fromUtf8(ex.what());
            StructuredDiagnosticsLogger::logException(
                QStringLiteral("core.import.ImportJob"),
                QStringLiteral("import_exception_std"),
                result->error.message,
                result->error.technicalDetails,
                {{QStringLiteral("correlationId"), request.correlationId},
                 {QStringLiteral("filePath"), request.filePath},
                 {QStringLiteral("stage"), result->stage},
                 {QStringLiteral("status"), QStringLiteral("failed_not_success")}});
            postProgress(jobGuard, request, cancelToken, 100, result->stage, result->error.technicalDetails);
        } catch (...) {
            result->status = ImportJobStatus::Failed;
            result->stage = QStringLiteral("exception");
            result->percent = 100;
            result->error.code = ImportJobErrorCode::Exception;
            result->error.stage = result->stage;
            result->error.message = QString::fromUtf8("Невідома помилка імпорту накладної.");
            result->error.technicalDetails = QStringLiteral("unknown_non_std_exception");
            StructuredDiagnosticsLogger::logException(
                QStringLiteral("core.import.ImportJob"),
                QStringLiteral("import_exception_unknown"),
                result->error.message,
                result->error.technicalDetails,
                {{QStringLiteral("correlationId"), request.correlationId},
                 {QStringLiteral("filePath"), request.filePath},
                 {QStringLiteral("stage"), result->stage},
                 {QStringLiteral("status"), QStringLiteral("failed_not_success")}});
            postProgress(jobGuard, request, cancelToken, 100, result->stage, result->error.message);
        }
    });

    QObject::connect(m_thread.data(), &QThread::finished, this, [this, generation, result, runtimeTicket]() {
        if (generation != m_generation) {
            RuntimeJob::RuntimeJobDispatcher::instance().finish(runtimeTicket, RuntimeJob::Status::Cancelled);
            return;
        }
        result->elapsedMs = m_elapsed.isValid() ? m_elapsed.elapsed() : 0;
        if (AsyncThreadScope::isCancellationRequested(m_cancelToken) && result->status == ImportJobStatus::Running) {
            result->status = ImportJobStatus::Cancelled;
            result->stage = QStringLiteral("cancelled");
            result->percent = 100;
            result->error.code = ImportJobErrorCode::Cancelled;
            result->error.stage = result->stage;
            result->error.message = QString::fromUtf8("Імпорт накладної скасовано.");
        }
        if (result->status == ImportJobStatus::Running) {
            result->status = ImportJobStatus::Succeeded;
            result->stage = QStringLiteral("finished");
            result->percent = 100;
        }
        const RuntimeJob::Status runtimeStatus = (result->status == ImportJobStatus::Succeeded)
            ? RuntimeJob::Status::Succeeded
            : (result->status == ImportJobStatus::Failed ? RuntimeJob::Status::Failed : RuntimeJob::Status::Cancelled);
        RuntimeJob::RuntimeJobDispatcher::instance().finish(runtimeTicket, runtimeStatus);
        emitTerminalResult(*result);
    });

    if (m_request.timeoutMs > 0) {
        QTimer::singleShot(m_request.timeoutMs, this, [this, generation]() {
            if (generation != m_generation || !m_running) {
                return;
            }
            requestStop(ImportJobStatus::TimedOut,
                        ImportJobErrorCode::Timeout,
                        QString::fromUtf8("Імпорт накладної перевищив ліміт часу."));
        });
    }

    if (m_thread) {
        m_thread->start();
    }
    return true;
}

void ImportJob::cancel()
{
    if (!m_running) {
        if (m_cancelToken) {
            m_cancelToken->store(true);
        }
        return;
    }
    requestStop(ImportJobStatus::Cancelled,
                ImportJobErrorCode::Cancelled,
                QString::fromUtf8("Імпорт накладної скасовано користувачем."));
}

void ImportJob::detachWorkerForOwnerShutdown()
{
    if (m_cancelToken) {
        m_cancelToken->store(true);
    }
    if (m_thread) {
        qWarning().noquote() << QStringLiteral("IMPORT_JOB owner_shutdown_detach correlationId=") << m_request.correlationId;
        QObject::disconnect(m_thread, nullptr, this, nullptr);
        m_thread->requestInterruption();
        m_thread.clear();
    }
    m_running = false;
    if (m_status == ImportJobStatus::Running) {
        m_status = ImportJobStatus::Cancelled;
    }
    ++m_generation;
}

void ImportJob::requestStop(ImportJobStatus status, ImportJobErrorCode code, const QString& message)
{
    if (m_cancelToken) {
        m_cancelToken->store(true);
    }
    if (m_thread) {
        m_thread->requestInterruption();
    }
    RuntimeJob::RuntimeJobDispatcher::instance().cancelFamily(RuntimeJob::Family::Import);

    const QString stage = (status == ImportJobStatus::TimedOut)
        ? QStringLiteral("timeout")
        : QStringLiteral("cancelled");

    ImportJobProgress progress;
    progress.correlationId = m_request.correlationId;
    progress.filePath = m_request.filePath;
    progress.stage = stage;
    progress.message = message;
    progress.percent = 100;
    progress.cancelRequested = true;
    emit progressChanged(progress);

    ImportJobResult result = makeTerminalResult(m_request,
                                                status,
                                                code,
                                                message,
                                                m_elapsed.isValid() ? m_elapsed.elapsed() : 0,
                                                stage,
                                                100);
    emitTerminalResult(result);
}

void ImportJob::emitTerminalResult(const ImportJobResult& result)
{
    if (!m_running && m_status != ImportJobStatus::Running) {
        if (result.status != ImportJobStatus::Failed || result.error.code != ImportJobErrorCode::InvalidRequest) {
            return;
        }
    }
    ++m_generation;
    m_running = false;
    m_status = result.status;
    if (RuntimeLoggingControl::deepDebugEnabled()) qInfo().noquote() << QStringLiteral("IMPORT_JOB finished correlationId=") << result.correlationId
                      << QStringLiteral(" status=") << static_cast<int>(result.status)
                      << QStringLiteral(" stage=") << result.stage
                      << QStringLiteral(" percent=") << result.percent
                      << QStringLiteral(" rows=") << result.data.rows.size()
                      << QStringLiteral(" elapsedMs=") << result.elapsedMs
                      << QStringLiteral(" errorCode=") << static_cast<int>(result.error.code)
                      << QStringLiteral(" error=") << result.error.message.left(500);
    emit finished(result);
}

} // namespace InvoiceImport
