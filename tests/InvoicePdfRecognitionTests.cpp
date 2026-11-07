#include <QtTest/QtTest>
#include "TestSourceAssertions.h"

class InvoicePdfRecognitionTests final : public QObject {
    Q_OBJECT
private slots:
    void criticalRegressionGuardsRemainWired();
    void scannedPdfOcrFallbackGuardsRemainWired();
    void missingOcrContractsAreRejected();
};

static bool ocrContractPresent(const QString& engine)
{
    return engine.contains(QStringLiteral("renderPdfPagesViaQtPdfForOcr"))
        && engine.contains(QStringLiteral("-RenderedImageDir"))
        && engine.contains(QStringLiteral("windows-media-image-ocr"))
        && engine.contains(QStringLiteral("Test-IsWindowsAppsShim"));
}

static bool packagedRuntimeIsComplete(const QString& installer, const QString& inventory)
{
    return installer.contains(QStringLiteral("Source: \"{#RuntimeDir}\\*\"; DestDir: \"{app}\"; Flags: ignoreversion recursesubdirs"))
        && inventory.contains(QStringLiteral("\"install_ocr_runtime.ps1\""))
        && inventory.contains(QStringLiteral("REQUIRED |= {\"scripts/\" + name for name in RUNTIME_SCRIPTS}"));
}

void InvoicePdfRecognitionTests::criticalRegressionGuardsRemainWired()
{
    cs::tests::requireAnyContains("src/core/import/InvoiceAttachmentPdfService.cpp", {"Еліт", "invoice", "pdf"});
    cs::tests::requireContains("src/core/services/orders/CurrentOrdersInvoiceImportService.cpp", "InvoiceImportResult");
}

void InvoicePdfRecognitionTests::scannedPdfOcrFallbackGuardsRemainWired()
{
    QVERIFY(ocrContractPresent(cs::tests::invoiceEngineImplementation()));
    QVERIFY(packagedRuntimeIsComplete(
        cs::tests::readSourceFile("CarService_PointG_v2_1.iss"),
        cs::tests::readSourceFile("scripts/check_runtime_package.py")));
}

void InvoicePdfRecognitionTests::missingOcrContractsAreRejected()
{
    const auto engine = cs::tests::invoiceEngineImplementation();
    for (const auto& token : {"renderPdfPagesViaQtPdfForOcr", "-RenderedImageDir",
                              "windows-media-image-ocr", "Test-IsWindowsAppsShim"}) {
        auto broken = engine;
        broken.replace(QString::fromLatin1(token), QStringLiteral("removed"));
        QVERIFY(!ocrContractPresent(broken));
    }
    const auto installer = cs::tests::readSourceFile("CarService_PointG_v2_1.iss");
    auto inventory = cs::tests::readSourceFile("scripts/check_runtime_package.py");
    QVERIFY(!packagedRuntimeIsComplete(QString(), inventory));
    inventory.replace(QStringLiteral("install_ocr_runtime.ps1"), QStringLiteral("removed"));
    QVERIFY(!packagedRuntimeIsComplete(installer, inventory));
}

QTEST_MAIN(InvoicePdfRecognitionTests)
#include "InvoicePdfRecognitionTests.moc"
