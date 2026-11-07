#include <QtTest/QtTest>

#include "core/crm/VehiclePreviewProvider.h"

class VehiclePreviewProviderTests final : public QObject {
    Q_OBJECT

private slots:
    void audiQ7MatchesDifferentAssetYear();
    void audiA6DoesNotMatchAudiQ7();
    void brandCanBeResolvedFromModelText();
};

static bool containsBaseName(const QStringList& paths, const QString& expectedPart)
{
    for (const QString& path : paths) {
        if (path.contains(expectedPart, Qt::CaseInsensitive)) return true;
    }
    return false;
}

void VehiclePreviewProviderTests::audiQ7MatchesDifferentAssetYear()
{
    const QStringList paths = VehiclePreviewProvider::candidateResourcePaths(QStringLiteral("Audi"),
                                                                             QStringLiteral("Q7"),
                                                                             2008,
                                                                             QString());
    QVERIFY2(!paths.isEmpty(), "Audi Q7 must match local preview assets even when the asset year differs.");
    QVERIFY2(containsBaseName(paths, QStringLiteral("Audi_Q7_")),
             qPrintable(QStringLiteral("Expected Audi_Q7 asset, got: %1").arg(paths.join(QStringLiteral("; ")))));
}

void VehiclePreviewProviderTests::audiA6DoesNotMatchAudiQ7()
{
    const QStringList paths = VehiclePreviewProvider::candidateResourcePaths(QStringLiteral("Audi"),
                                                                             QStringLiteral("A6"),
                                                                             0,
                                                                             QString());
    QVERIFY2(!paths.isEmpty(), "Audi A6 test requires local Audi A6 preview assets.");
    QVERIFY2(!containsBaseName(paths, QStringLiteral("Audi_Q7_")),
             qPrintable(QStringLiteral("Audi A6 must not match Audi Q7 assets: %1").arg(paths.join(QStringLiteral("; ")))));
}

void VehiclePreviewProviderTests::brandCanBeResolvedFromModelText()
{
    for (const auto& description : QStringList{
             QStringLiteral("Audi Q7 2008 3.0 Дизель"),
             QStringLiteral("Audi Q7 2008 3.0 дизель"),
             QStringLiteral("Audi Q7 2008 3.0 БЕНЗИН")}) {
    const QStringList paths = VehiclePreviewProvider::candidateResourcePaths(QString(),
                                                                             description,
                                                                             2008,
                                                                             QString());
    QVERIFY2(!paths.isEmpty(), "Brand must be resolved from full model text.");
    QVERIFY2(containsBaseName(paths, QStringLiteral("Audi_Q7_")),
             qPrintable(QStringLiteral("Expected Audi_Q7 asset from full model text, got: %1").arg(paths.join(QStringLiteral("; ")))));
    }
}

QTEST_MAIN(VehiclePreviewProviderTests)
#include "VehiclePreviewProviderTests.moc"
