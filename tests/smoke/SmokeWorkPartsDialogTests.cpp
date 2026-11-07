#include <QtTest/QtTest>
#include "../TestSourceAssertions.h"

static bool navigationStartIsLogged(const QString& source)
{
    return source.contains(QStringLiteral("logExcelNavigationState(table, ExcelNavigationStateStep::Begin,"))
        && source.contains(QStringLiteral("case ExcelNavigationStateStep::Begin: return QStringLiteral(\"begin\");"))
        && source.contains(QStringLiteral("QStringLiteral(\"NAV_STATE\")"));
}

class SmokeWorkPartsDialogTests final : public QObject {
    Q_OBJECT
private slots:
    void criticalRegressionGuardsRemainWired();
    void missingNavigationStartIsRejected()
    {
        const QString source = cs::tests::inlineEditorImplementation();
        QVERIFY(navigationStartIsLogged(source));
        QString broken = source;
        broken.replace(QStringLiteral("logExcelNavigationState(table, ExcelNavigationStateStep::Begin,"), QStringLiteral("removed_begin("));
        QVERIFY(!navigationStartIsLogged(broken));
    }
};

void SmokeWorkPartsDialogTests::criticalRegressionGuardsRemainWired()
{
    cs::tests::requireContains("src/ui/tabs/orders/WorkPartsPersistenceCoordinator.cpp", "commitPendingEdits");
    QVERIFY(navigationStartIsLogged(cs::tests::inlineEditorImplementation()));
    cs::tests::requireContains("src/ui/tabs/orders/WorkPartsDialogUiBuilder.cpp", "SAVE_QUEUE");
}

QTEST_MAIN(SmokeWorkPartsDialogTests)
#include "SmokeWorkPartsDialogTests.moc"
