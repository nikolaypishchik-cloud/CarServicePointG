#include <QtTest/QtTest>
#include "../TestSourceAssertions.h"

class RegressionP0ImportIncrementalProcessIoTests final : public QObject {
    Q_OBJECT
private slots:
    void importEngineUsesIncrementalProcessDrain();
    void externalImportFallbacksUseSharedIncrementalRunner();
};

void RegressionP0ImportIncrementalProcessIoTests::importEngineUsesIncrementalProcessDrain()
{
    const QString engine = cs::tests::invoiceEngineImplementation();

    QVERIFY2(engine.contains(QStringLiteral("runImportProcessIncrementally")),
             "P0 import p5 requires a shared incremental QProcess runner for invoice import fallbacks.");
    QVERIFY2(engine.contains(QStringLiteral("drainImportProcessChannels")),
             "P0 import p5 requires stdout/stderr draining while the process is still running.");
    QVERIFY2(engine.contains(QStringLiteral("waitForReadyRead")),
             "P0 import p5 requires incremental wait/read cycles instead of waiting until process exit.");
    QVERIFY2(engine.contains(QStringLiteral("incremental process finished")),
             "P0 import p5 requires structured logging of drained stdout/stderr byte counts.");
}

void RegressionP0ImportIncrementalProcessIoTests::externalImportFallbacksUseSharedIncrementalRunner()
{
    const QString engine = cs::tests::invoiceEngineImplementation();

    const QStringList operations = {
        QStringLiteral("pdf_direct_python"),
        QStringLiteral("pdf_powershell_fallback"),
        QStringLiteral("spreadsheet_python_fallback"),
        QStringLiteral("excel_powershell_fallback")
    };
    for (const QString& operation : operations) {
        QVERIFY2(engine.contains(QStringLiteral("runImportProcessIncrementally(process"))
                     && engine.contains(operation),
                 qPrintable(QStringLiteral("P0 import p5 violation: %1 must use incremental stdout/stderr reading.").arg(operation)));
    }

    QVERIFY2(!engine.contains(QStringLiteral("process.waitForFinished(")),
             "P0 import p5 violation: import engine must not wait for process finish before draining stdout/stderr.");
}

QTEST_MAIN(RegressionP0ImportIncrementalProcessIoTests)
#include "RegressionP0ImportIncrementalProcessIoTests.moc"
