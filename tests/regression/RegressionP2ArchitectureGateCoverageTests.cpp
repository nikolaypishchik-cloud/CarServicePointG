#include <QtTest/QtTest>

#include <QFile>
#include <QTextStream>
#include "../TestSourceAssertions.h"

class RegressionP2ArchitectureGateCoverageTests final : public QObject {
    Q_OBJECT

private slots:
    void gateContainsP2P5Rules()
    {
        QFile file(cs::tests::projectRoot()+QStringLiteral("/scripts/check_architecture.py"));
        QVERIFY2(file.open(QIODevice::ReadOnly | QIODevice::Text), "scripts/check_architecture.py must be present");
        const QString text = QString::fromUtf8(file.readAll());

        QVERIFY(text.contains(QStringLiteral("Database::db() directly")));
        QVERIFY(text.contains(QStringLiteral("Qt SQL query/database primitives")));
        QVERIFY(text.contains(QStringLiteral("waitForFinished")));
        QVERIFY(text.contains(QStringLiteral("Service layer must not show QMessageBox")));
        QVERIFY(text.contains(QStringLiteral("Repository layer must not perform UI/notification side effects")));
    }

    void adrDocumentsCriticalPipelines()
    {
        QFile file(cs::tests::projectRoot()+QStringLiteral("/docs/architecture/ADR-0001-architecture-boundaries-and-pipelines.md"));
        QVERIFY2(file.open(QIODevice::ReadOnly | QIODevice::Text), "P2 architecture ADR must be present");
        const QString text = QString::fromUtf8(file.readAll());

        QVERIFY(text.contains(QStringLiteral("Transaction boundary")));
        QVERIFY(text.contains(QStringLiteral("Import pipeline")));
        QVERIFY(text.contains(QStringLiteral("Update pipeline")));
        QVERIFY(text.contains(QStringLiteral("Backup/recovery pipeline")));
    }
};

QTEST_MAIN(RegressionP2ArchitectureGateCoverageTests)
#include "RegressionP2ArchitectureGateCoverageTests.moc"
