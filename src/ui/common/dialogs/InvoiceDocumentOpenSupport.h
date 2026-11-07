#pragma once

#include "core/import/InvoiceAttachmentPdfService.h"
#include "core/import/InvoiceImportEngine.h"
#include "core/services/AccessControlService.h"
#include "runtime/AsyncThreadScope.h"

#include <QFileInfo>
#include <QWidget>
#include <functional>

namespace InvoiceDocumentOpenSupport {

// Viewing a legacy invoice must not replace its stored source or block the UI.
inline void prepareAsync(QWidget* owner, const QString& source, const QString& number,
                         const QString& date, const QString& supplier,
                         std::function<void(const QString&, const QString&)> done)
{
    if (!owner || owner->property("csInvoiceOpenPending").toBool()) return;
    owner->setProperty("csInvoiceOpenPending", true);
    const QPointer<QWidget> guard(owner);
    const QString scope = AccessControlService::currentScopeKey();
    const auto token = std::make_shared<std::atomic_bool>(false);
    struct Result { QString path; QString error; };
    const auto result = std::make_shared<Result>();
    auto* thread = AsyncThreadScope::startWorker(owner, QStringLiteral("invoice_document_open"), token,
        [source, number, date, supplier, result](const auto& cancel) {
            if (AsyncThreadScope::isCancellationRequested(cancel)) return;
            if (QFileInfo(source).suffix().compare(QStringLiteral("pdf"), Qt::CaseInsensitive) == 0) {
                if (InvoiceImport::validateInvoicePdf(source, &result->error))
                    result->path = QFileInfo(source).absoluteFilePath();
            } else {
                result->path = InvoiceImport::ensureInvoicePdfAttachment(source, number, date, supplier, &result->error);
            }
            if (!result->path.isEmpty()
                && QFileInfo(result->path).suffix().compare(QStringLiteral("pdf"), Qt::CaseInsensitive) != 0) {
                result->path.clear();
                result->error = QString::fromUtf8("Не вдалося підготувати PDF накладної.");
            }
        });
    QObject::connect(thread, &QThread::finished, owner,
        [guard, scope, token, result, done = std::move(done)] {
            if (!guard) return;
            guard->setProperty("csInvoiceOpenPending", false);
            if (token->load() || scope != AccessControlService::currentScopeKey()) return;
            if (result->path.isEmpty() && result->error.isEmpty())
                result->error = QString::fromUtf8("Не вдалося підготувати PDF накладної.");
            done(result->path, result->error);
        });
    thread->start();
}

} // namespace InvoiceDocumentOpenSupport
