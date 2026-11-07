#pragma once

#include "core/import/InvoiceImportTypes.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QElapsedTimer>
#include <QThread>

#include <atomic>
#include <memory>

namespace InvoiceImport {

enum class ImportJobStatus {
    Idle,
    Running,
    Succeeded,
    Failed,
    Cancelled,
    TimedOut
};

enum class ImportJobErrorCode {
    None,
    InvalidRequest,
    EngineError,
    Cancelled,
    Timeout,
    Exception
};

struct ImportJobRequest {
    QString filePath;
    QString correlationId;
    int timeoutMs = 60000;
    QString sourceHint;
};

struct ImportJobProgress {
    QString correlationId;
    QString filePath;
    QString stage;
    QString message;
    int percent = 0;
    bool cancelRequested = false;
};

struct ImportJobError {
    ImportJobErrorCode code = ImportJobErrorCode::None;
    QString stage;
    QString message;
    QString technicalDetails;

    bool hasError() const { return code != ImportJobErrorCode::None || !message.trimmed().isEmpty(); }
};

struct ImportJobResult {
    QString correlationId;
    QString filePath;
    ImportJobStatus status = ImportJobStatus::Idle;
    QString stage;
    int percent = 0;
    ExcelReadResult data;
    ImportJobError error;
    qint64 elapsedMs = 0;

    bool ok() const { return status == ImportJobStatus::Succeeded && !error.hasError(); }
};

// P0 Import p3: one controlled import job contract for async execution,
// progress, cancel, timeout, structured result DTO and structured errors.
class ImportJob final : public QObject {
    Q_OBJECT
public:
    explicit ImportJob(QObject* parent = nullptr);
    ~ImportJob() override;

    bool start(const ImportJobRequest& request);
    void cancel();

    bool isRunning() const { return m_running; }
    ImportJobStatus status() const { return m_status; }

signals:
    void progressChanged(const InvoiceImport::ImportJobProgress& progress);
    void finished(const InvoiceImport::ImportJobResult& result);

private:
    void emitTerminalResult(const ImportJobResult& result);
    void detachWorkerForOwnerShutdown();
    void requestStop(ImportJobStatus status, ImportJobErrorCode code, const QString& message);

    ImportJobRequest m_request;
    QPointer<QThread> m_thread;
    std::shared_ptr<std::atomic_bool> m_cancelToken;
    QElapsedTimer m_elapsed;
    quint64 m_generation = 0;
    bool m_running = false;
    ImportJobStatus m_status = ImportJobStatus::Idle;
};

} // namespace InvoiceImport

Q_DECLARE_METATYPE(InvoiceImport::ImportJobProgress)
Q_DECLARE_METATYPE(InvoiceImport::ImportJobResult)
