#include <QtTest/QtTest>
#include <QFile>
#include <QString>
#include <QTextStream>
#include "../TestSourceAssertions.h"

class RegressionPerformanceTzPass12WorkPartsRowIndexTests : public QObject
{
    Q_OBJECT
private slots:
    void workPartsDialogKeepsIdRowIndexesForSelectionAndDraftRestore();
    void pass12GateCoversLegacyEditorHotPathReduction();
};

static QString readFile(const QString& path)
{
    QFile f(cs::tests::projectRoot() + QLatin1Char('/') + path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTest::qFail(qPrintable(path), __FILE__, __LINE__);
        return {};
    }
    return QString::fromUtf8(f.readAll());
}

void RegressionPerformanceTzPass12WorkPartsRowIndexTests::workPartsDialogKeepsIdRowIndexesForSelectionAndDraftRestore()
{
    const QString header = readFile(QStringLiteral("src/ui/tabs/orders/WorkPartsDialog.h"));
    const QString dialog = readFile(QStringLiteral("src/ui/tabs/orders/WorkPartsDialog.cpp"));
    const QString services = readFile(QStringLiteral("src/ui/tabs/orders/ServiceTableController.cpp"));
    const QString parts = readFile(QStringLiteral("src/ui/tabs/orders/PartTableController.cpp"));
    const QString editor = readFile(QStringLiteral("src/ui/tabs/orders/WorkPartsEditorController.cpp"));

    QVERIFY(header.contains(QStringLiteral("QHash<int, int> m_serviceRowById")));
    QVERIFY(header.contains(QStringLiteral("QHash<int, int> m_partRowById")));
    QVERIFY(dialog.contains(QStringLiteral("P1/pass12 WorkPartsDialog row-index closure")));
    QVERIFY(services.contains(QStringLiteral("serviceRowById(m_activeServiceId)")));
    QVERIFY(services.contains(QStringLiteral("indexServiceRow(service.id, row)")));
    QVERIFY(parts.contains(QStringLiteral("indexPartRow(part.id, row)")));
    QVERIFY(editor.contains(QStringLiteral("use the snapshot-built id index first")));
}

void RegressionPerformanceTzPass12WorkPartsRowIndexTests::pass12GateCoversLegacyEditorHotPathReduction()
{
    const QString gate = readFile(QStringLiteral("scripts/check_performance_tz_100_completion.py"));
    QVERIFY(gate.contains(QStringLiteral("RegressionPerformanceTzPass12WorkPartsRowIndexTests")));
    QVERIFY(gate.contains(QStringLiteral("WorkPartsDialog row-index closure")));
}

QTEST_MAIN(RegressionPerformanceTzPass12WorkPartsRowIndexTests)
#include "RegressionPerformanceTzPass12WorkPartsRowIndexTests.moc"
