#include <QtTest/QtTest>
#include <QDirIterator>
#include "../TestSourceAssertions.h"

class RegressionP0ImportNoUiWaitTests final : public QObject {
    Q_OBJECT
private slots:
    void uiLayerHasNoBlockingWaitForFinishedCalls();
    void importEngineRejectsBlockingExternalProcessWaitsOnUiThread();
    void importJobKeepsBlockingImporterBehindWorkerThread();
};

void RegressionP0ImportNoUiWaitTests::uiLayerHasNoBlockingWaitForFinishedCalls()
{
    const QDir uiRoot(cs::tests::projectRoot() + QStringLiteral("/src/ui"));
    QVERIFY2(uiRoot.exists(), "src/ui must exist for the P0 import p4 guard.");
    QDirIterator it(uiRoot.absolutePath(),
                    QStringList() << QStringLiteral("*.cpp") << QStringLiteral("*.h") << QStringLiteral("*.inc") << QStringLiteral("*.hpp"),
                    QDir::Files,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        QFile file(path);
        QVERIFY2(file.open(QIODevice::ReadOnly | QIODevice::Text),
                 qPrintable(QStringLiteral("Unable to open UI source: %1").arg(path)));
        const QString content = QString::fromUtf8(file.readAll());
        QVERIFY2(!content.contains(QStringLiteral("waitForFinished(")),
                 qPrintable(QStringLiteral("P0 import p4 violation: UI source must not call waitForFinished(): %1").arg(path)));
    }
}

void RegressionP0ImportNoUiWaitTests::importEngineRejectsBlockingExternalProcessWaitsOnUiThread()
{
    const QString engine = cs::tests::invoiceEngineImplementation();
    QVERIFY2(engine.contains(QStringLiteral("rejectBlockingImportProcessOnUiThread")),
             "Invoice import engine must have an explicit P0 p4 UI-thread wait guard.");
    QVERIFY2(engine.contains(QStringLiteral("QThread::currentThread() != app->thread()")),
             "The P0 p4 guard must compare current thread against the application/UI thread.");
    QVERIFY2(engine.contains(QStringLiteral("P0_import_p4_no_blocking_process_wait_on_ui_thread")),
             "The P0 p4 guard must log a stable diagnostic marker.");
    QVERIFY2(engine.contains(QStringLiteral("pdf_direct_python"))
                 && engine.contains(QStringLiteral("pdf_powershell_fallback"))
                 && engine.contains(QStringLiteral("spreadsheet_python_fallback"))
                 && engine.contains(QStringLiteral("excel_powershell_fallback")),
             "Every external invoice-import fallback must pass through the UI-thread blocking wait guard.");
}

void RegressionP0ImportNoUiWaitTests::importJobKeepsBlockingImporterBehindWorkerThread()
{
    const QString job = cs::tests::readSourceFile("src/core/import/ImportJob.cpp");
    QVERIFY2(job.contains(QStringLiteral("AsyncThreadScope::startWorker")),
             "InvoiceImport::ImportJob must keep synchronous engine work behind a worker thread.");
    QVERIFY2(job.contains(QStringLiteral("readInvoiceRows(request.filePath")),
             "ImportJob should remain the only UI-facing path to the synchronous invoice import engine.");
    QVERIFY2(job.contains(QStringLiteral("P0_import_p4_import_job_worker_thread_guard")),
             "ImportJob must fail closed if the synchronous importer ever runs on the application/UI thread.");
}

QTEST_MAIN(RegressionP0ImportNoUiWaitTests)
#include "RegressionP0ImportNoUiWaitTests.moc"
