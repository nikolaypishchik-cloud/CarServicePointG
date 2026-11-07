#include <QtTest/QtTest>
#include "../TestSourceAssertions.h"

class RegressionP0ImportJobTests final : public QObject {
    Q_OBJECT
private slots:
    void importJobContractIsPresent();
    void importJobStructuredLifecycleIsGuarded();
    void uiAsyncReadUsesImportJobBoundary();
};

void RegressionP0ImportJobTests::importJobContractIsPresent()
{
    const QString header = cs::tests::readSourceFile("src/core/import/ImportJob.h");
    const QString source = cs::tests::readSourceFile("src/core/import/ImportJob.cpp");

    QVERIFY2(header.contains(QStringLiteral("enum class ImportJobStatus")),
             "P0 import p3 requires a structured job status enum.");
    QVERIFY2(header.contains(QStringLiteral("enum class ImportJobErrorCode")),
             "P0 import p3 requires a structured job error-code enum.");
    QVERIFY2(header.contains(QStringLiteral("struct ImportJobRequest")),
             "P0 import p3 requires a structured ImportJob request DTO.");
    QVERIFY2(header.contains(QStringLiteral("struct ImportJobProgress")),
             "P0 import p3 requires structured progress DTO.");
    QVERIFY2(header.contains(QStringLiteral("struct ImportJobError")),
             "P0 import p3 requires structured error DTO.");
    QVERIFY2(header.contains(QStringLiteral("struct ImportJobResult")),
             "P0 import p3 requires structured result DTO.");
    QVERIFY2(header.contains(QStringLiteral("void cancel()")),
             "P0 import p3 requires cancellation on ImportJob.");
    QVERIFY2(header.contains(QStringLiteral("timeoutMs")) && source.contains(QStringLiteral("QTimer::singleShot")),
             "P0 import p3 requires timeout support on ImportJob.");
    QVERIFY2(header.contains(QStringLiteral("stage")) && header.contains(QStringLiteral("percent")),
             "P0 import p3 progress/result DTOs must carry stage and percent fields.");
    QVERIFY2(header.contains(QStringLiteral("cancelRequested")),
             "P0 import p3 progress DTO must expose cancellation state.");
    QVERIFY2(header.contains(QStringLiteral("technicalDetails")),
             "P0 import p3 structured errors must preserve technical diagnostics.");
    QVERIFY2(source.contains(QStringLiteral("AsyncThreadScope::startWorker")),
             "P0 import p3 requires async worker execution behind ImportJob.");
    QVERIFY2(source.contains(QStringLiteral("progressChanged")) && source.contains(QStringLiteral("finished")),
             "P0 import p3 requires progress and terminal completion signals.");
}

void RegressionP0ImportJobTests::importJobStructuredLifecycleIsGuarded()
{
    const QString source = cs::tests::readSourceFile("src/core/import/ImportJob.cpp");

    QVERIFY2(source.contains(QStringLiteral("postProgress(jobGuard, request, m_cancelToken, 0"))
                 && source.contains(QStringLiteral("postProgress(jobGuard, request, cancelToken, 0"))
                 && source.contains(QStringLiteral("postProgress(jobGuard, request, cancelToken, 100")),
             "Progress must expose queued/running states and confirmed completion; unknown work must not invent percentages.");
    QVERIFY2(source.contains(QStringLiteral("ImportJobStatus::Cancelled"))
                 && source.contains(QStringLiteral("ImportJobErrorCode::Cancelled"))
                 && source.contains(QStringLiteral("progress.cancelRequested = true")),
             "P0 import p3 cancellation must produce structured progress and terminal error state.");
    QVERIFY2(source.contains(QStringLiteral("ImportJobStatus::TimedOut"))
                 && source.contains(QStringLiteral("ImportJobErrorCode::Timeout"))
                 && source.contains(QStringLiteral("QStringLiteral(\"timeout\")")),
             "P0 import p3 timeout must produce a structured terminal result.");
    QVERIFY2(source.contains(QStringLiteral("makeTerminalResult"))
                 && source.contains(QStringLiteral("result.error.stage = stage"))
                 && source.contains(QStringLiteral("result.error.message = message")),
             "P0 import p3 errors must be normalized through a single structured terminal-result builder.");
    QVERIFY2(source.contains(QStringLiteral("readInvoiceRows(request.filePath, &errorText, cancelToken)")),
             "ImportJob remains the single controlled async boundary over the synchronous import engine.");
}

void RegressionP0ImportJobTests::uiAsyncReadUsesImportJobBoundary()
{
    const QString wrapper = cs::tests::readSourceFile("src/ui/tabs/orders/WorkPartsDialogExcelSupport.cpp");
    const QString overlay = cs::tests::readSourceFile("src/ui/common/widgets/InvoiceImportScannerOverlay.h");
    const QString service = cs::tests::readSourceFile("src/core/import/InvoiceAttachmentPdfService.cpp");
    const QString cmake = cs::tests::readSourceFile("cmake/modules/CarServiceSourcesCore.cmake");

    QVERIFY2(wrapper.contains(QStringLiteral("core/import/ImportJob.h")),
             "WorkParts invoice UI wrapper must include the ImportJob facade, not create raw importer workers.");
    QVERIFY2(wrapper.contains(QStringLiteral("InvoiceImport::ImportJob"))
                 && wrapper.contains(QStringLiteral("ImportJobRequest")),
             "runExcelReadAsync must use ImportJob as the import boundary.");
    const QString progressDialog = cs::tests::readSourceFile("src/ui/common/widgets/InvoiceImportProgressDialog.h");
    QVERIFY2(wrapper.contains(QStringLiteral("new InvoiceImportProgressDialog"))
                 && progressDialog.contains(QStringLiteral("ImportJob::progressChanged"))
                 && progressDialog.contains(QStringLiteral("m_bar->setValue(qMax(m_bar->value(), progress.percent))"))
                 && progressDialog.contains(QStringLiteral("if (!m_active || attempt != m_attempt) return;")),
             "The shared progress dialog must consume job progress and reject stale/cancelled completion.");
    QVERIFY2(overlay.contains(QStringLiteral("setImportProgress"))
                 && overlay.contains(QStringLiteral("m_externalProgress"))
                 && overlay.contains(QStringLiteral("m_progressMessage")),
             "The import overlay must be able to display structured ImportJob progress.");
    const QString readBoundary = wrapper.left(wrapper.indexOf(QStringLiteral("void runConfirmedVatMappingParseAsync")));
    QVERIFY2(!readBoundary.contains(QStringLiteral("ExcelReadAsyncPayload"))
                 && !readBoundary.contains(QStringLiteral("AsyncThreadScope::startWorker")),
             "UI async invoice read must not keep the old local thread/payload implementation.");
    QVERIFY2(service.contains(QStringLiteral("ensureInvoicePdfAttachment"))
                 && service.contains(QStringLiteral("renderInvoiceTablePdf")),
             "P0 import p1 requires invoice PDF preparation to be extracted into src/core/import.");
    QVERIFY2(cmake.contains(QStringLiteral("src/core/import/ImportJob.cpp"))
                 && cmake.contains(QStringLiteral("src/core/import/ImportJob.h")),
             "ImportJob must be compiled into CarServiceLib.");
}

QTEST_MAIN(RegressionP0ImportJobTests)
#include "RegressionP0ImportJobTests.moc"
