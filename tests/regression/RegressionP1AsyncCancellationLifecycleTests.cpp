#include "../TestSourceAssertions.h"
#include <QFile>
#include <QIODevice>
#include <QString>

#include <iostream>

namespace {

QString readFile(const QString& path)
{
    QFile f(cs::tests::projectRoot() + QLatin1Char('/') + path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        std::cerr << "Cannot open " << path.toStdString() << "\n";
        return QString();
    }
    return QString::fromUtf8(f.readAll());
}

bool contains(const QString& text, const QString& needle)
{
    return text.contains(needle, Qt::CaseSensitive);
}

int countOf(const QString& text, const QString& needle)
{
    int count = 0;
    qsizetype pos = 0;
    while ((pos = text.indexOf(needle, pos, Qt::CaseSensitive)) >= 0) {
        ++count;
        pos += needle.size();
    }
    return count;
}

} // namespace

int main()
{
    const QString asyncScope = readFile(QStringLiteral("src/runtime/AsyncThreadScope.h"));
    const QString partsRuntime = readFile(QStringLiteral("src/ui/tabs/parts/PartsPageRuntime.cpp"));
    const QString partsCpp = readFile(QStringLiteral("src/ui/tabs/parts/PartsPage.cpp"));
    const QString importJob = readFile(QStringLiteral("src/core/import/ImportJob.cpp"));
    const QString importEngineH = readFile(QStringLiteral("src/core/import/InvoiceImportEngine.h"));
    const QString importEngine = cs::tests::invoiceEngineImplementation();
    const QString workPartsAsync = readFile(QStringLiteral("src/ui/tabs/orders/WorkPartsDialogExcelSupport.cpp"));

    if (asyncScope.isEmpty() || partsRuntime.isEmpty() || partsCpp.isEmpty() ||
        importJob.isEmpty() || importEngineH.isEmpty() || importEngine.isEmpty() || workPartsAsync.isEmpty()) {
        return 1;
    }

    if (!contains(asyncScope, QStringLiteral("isCancellationRequested")) ||
        !contains(asyncScope, QStringLiteral("isInterruptionRequested")) ||
        !contains(asyncScope, QStringLiteral("worker_cancelled_before_start"))) {
        std::cerr << "P1 async p5 requires a canonical cancellation/interruption check in AsyncThreadScope.\n";
        return 2;
    }

    const QRegularExpression guardedRead(QStringLiteral(R"(if \(cancelled\(\)\)[\s\S]*?return;[\s\S]*?PartsQueryService::loadScopedRows\([^;]+;\s*if \(cancelled\(\)\)[\s\S]*?return;[\s\S]*?if \(!cancelled\(\)\)\s*\{\s*\*result = std::move\(localResult\);)"));
    QString missingCancel = partsRuntime;
    missingCancel.replace("if (cancelled())", "if (false)");
    QString unguardedPublish = partsRuntime;
    unguardedPublish.replace("if (!cancelled())", "if (true)");
    if (!contains(partsRuntime, QStringLiteral("AsyncThreadScope::isCancellationRequested(cancelToken)")) ||
        !guardedRead.match(partsRuntime).hasMatch() || guardedRead.match(missingCancel).hasMatch() || guardedRead.match(unguardedPublish).hasMatch() ||
        !contains(partsRuntime, QStringLiteral("AppRuntimeService::closeThreadConnection()"))) {
        std::cerr << "PartsPage refresh worker must check cancellation before/after long DB phases and close thread DB connection.\n";
        return 3;
    }

    if (!contains(importJob, QStringLiteral("AsyncThreadScope::isCancellationRequested(cancelToken)")) ||
        !contains(importJob, QStringLiteral("readInvoiceRows(request.filePath, &errorText, cancelToken)")) ||
        !contains(importJob, QStringLiteral("cancelled_before_read"))) {
        std::cerr << "ImportJob must pass cancellation token into the import engine and check before heavy reading/parsing.\n";
        return 4;
    }

    if (!contains(importEngineH, QStringLiteral("std::shared_ptr<std::atomic_bool>")) ||
        !contains(importEngine, QStringLiteral("before_detect_type")) ||
        !contains(importEngine, QStringLiteral("before_parser_dispatch")) ||
        !contains(importEngine, QStringLiteral("after_parser_return")) ||
        !contains(importEngine, QStringLiteral("incremental process cancelled"))) {
        std::cerr << "Invoice import engine must expose cancellation-aware read and interrupt external process waits.\n";
        return 5;
    }

    if (!contains(partsCpp, QStringLiteral("cancelActiveRefreshForOwnerShutdown(\"destructor\")")) ||
        !contains(partsCpp, QStringLiteral("m_refreshWorker->requestInterruption()")) ||
        !contains(partsRuntime, QStringLiteral("parts_page_toggle_paid_cancel_refresh")) ||
        !contains(partsRuntime, QStringLiteral("m_refreshQueued = true")) ||
        !contains(partsRuntime, QStringLiteral("if (m_refreshInProgress || m_refreshWorker)"))) {
        std::cerr << "P1 async p6 scenarios must be guarded: close during refresh, toggle paid during refresh, repeated refresh.\n";
        return 6;
    }

    const auto progress = cs::tests::readSourceFile("src/ui/common/widgets/InvoiceImportProgressDialog.h");
    if (!cs::tests::invoiceOwnerCancellationWired(workPartsAsync, progress, importJob)
        || !cs::tests::invoiceCancellationRejectsBrokenLinks(workPartsAsync, progress, importJob)) {
        std::cerr << "Import during dialog close must cancel and reject late results.\n";
        return 7;
    }

    std::cout << "P1 async lifecycle p5-p6 cancellation and scenario guards verified.\n";
    return 0;
}
