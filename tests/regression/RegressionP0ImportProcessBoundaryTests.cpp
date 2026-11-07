#include <QtTest/QtTest>
#include "../TestSourceAssertions.h"

class RegressionP0ImportProcessBoundaryTests final : public QObject {
    Q_OBJECT
private slots:
    void uiImportLayerDoesNotLaunchExternalProcesses();
    void uiImportLayerDoesNotExposeSynchronousImporter();
    void coreImportLayerOwnsExternalProcessFallbacksBehindNeutralApi();
};

static void assertNoProcessLauncherTokens(const char* logicalPath)
{
    const QString content = cs::tests::readSourceFile(logicalPath);
    const QStringList banned = {
        QStringLiteral("#include <QProcess>"),
        QStringLiteral("QProcess"),
        QStringLiteral("waitForFinished("),
        QStringLiteral("powershell.exe"),
        QStringLiteral("PowerShell"),
        QStringLiteral("python.exe"),
        QStringLiteral("py.exe"),
        QStringLiteral("readExcelRowsViaPowerShell")
    };
    for (const QString& token : banned) {
        QVERIFY2(!content.contains(token),
                 qPrintable(QStringLiteral("P0 import p2 violation: %1 must not contain direct process/import launcher token '%2'.")
                            .arg(QString::fromUtf8(logicalPath), token)));
    }
}

void RegressionP0ImportProcessBoundaryTests::uiImportLayerDoesNotLaunchExternalProcesses()
{
    assertNoProcessLauncherTokens("src/ui/tabs/orders/WorkPartsDialogExcelSupport.h");
    assertNoProcessLauncherTokens("src/ui/tabs/orders/WorkPartsDialogExcelSupport.cpp");
    assertNoProcessLauncherTokens("src/core/import/InvoiceAttachmentPdfService.cpp");
    assertNoProcessLauncherTokens("src/ui/tabs/orders/CurrentOrdersPageLocalSupport.cpp");
    assertNoProcessLauncherTokens("src/ui/tabs/orders/WorkPartsImportCoordinator.cpp");
}

void RegressionP0ImportProcessBoundaryTests::uiImportLayerDoesNotExposeSynchronousImporter()
{
    const QString uiHeader = cs::tests::readSourceFile("src/ui/tabs/orders/WorkPartsDialogExcelSupport.h");
    const QString uiWrapper = cs::tests::readSourceFile("src/ui/tabs/orders/WorkPartsDialogExcelSupport.cpp");
    const QString coordinator = cs::tests::readSourceFile("src/ui/tabs/orders/WorkPartsImportCoordinator.cpp");

    QVERIFY2(uiHeader.contains(QStringLiteral("core/import/InvoiceImportParsing.h")),
             "UI import compatibility header may expose parser/data DTOs only through the process-free parsing facade.");
    QVERIFY2(!uiHeader.contains(QStringLiteral("InvoiceImportEngine.h"))
                 && !uiHeader.contains(QStringLiteral("readInvoiceRows")),
             "P0 import p2 violation: UI header must not expose the synchronous importer entry point.");
    QVERIFY2(uiWrapper.contains(QStringLiteral("core/import/ImportJob.h"))
                 && uiWrapper.contains(QStringLiteral("InvoiceImport::ImportJob")),
             "P0 import p2 requires UI import code to cross the process boundary only through ImportJob.");
    QVERIFY2(!uiWrapper.contains(QStringLiteral("readInvoiceRows"))
                 && !coordinator.contains(QStringLiteral("readInvoiceRows")),
             "P0 import p2 violation: UI import code must not call the synchronous importer directly.");
}

void RegressionP0ImportProcessBoundaryTests::coreImportLayerOwnsExternalProcessFallbacksBehindNeutralApi()
{
    const QString parsingHeader = cs::tests::readSourceFile("src/core/import/InvoiceImportParsing.h");
    const QString engineHeader = cs::tests::readSourceFile("src/core/import/InvoiceImportEngine.h");
    const QString engine = cs::tests::readSourceFile("src/core/import/InvoiceImportEngine.cpp");
    const QString attachmentServiceHeader = cs::tests::readSourceFile("src/core/import/InvoiceAttachmentPdfService.h");

    QVERIFY2(parsingHeader.contains(QStringLiteral("parseImportedPartDrafts"))
                 && parsingHeader.contains(QStringLiteral("parsePdfInvoiceText"))
                 && !parsingHeader.contains(QStringLiteral("readInvoiceRows"))
                 && !parsingHeader.contains(QStringLiteral("QProcess"))
                 && !parsingHeader.contains(QStringLiteral("PowerShell"))
                 && !parsingHeader.contains(QStringLiteral("python.exe")),
             "The UI-facing parser facade must stay process-free.");
    QVERIFY2(engineHeader.contains(QStringLiteral("ExcelReadResult readInvoiceRows")),
             "The public synchronous importer API must remain isolated in the core import engine header.");
    QVERIFY2(!engineHeader.contains(QStringLiteral("readExcelRowsViaPowerShell")),
             "PowerShell-specific API names must not be exposed to UI/application callers.");
    QVERIFY2(attachmentServiceHeader.contains(QStringLiteral("ensureInvoicePdfAttachment")),
             "P0 import p1 requires invoice PDF attachment preparation to be owned by src/core/import.");
    QVERIFY2(engine.contains(QStringLiteral("#include <QProcess>"))
                 && engine.contains(QStringLiteral("QProcess process")),
             "External-process fallbacks are still available, but only inside src/core/import.");
}

QTEST_MAIN(RegressionP0ImportProcessBoundaryTests)
#include "RegressionP0ImportProcessBoundaryTests.moc"
