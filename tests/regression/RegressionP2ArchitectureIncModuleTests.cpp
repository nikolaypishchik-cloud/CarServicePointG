#include <QtTest/QtTest>
#include <QDirIterator>
#include "../TestSourceAssertions.h"

static bool repairAdvisorOwnerBoundary(const QString& name, const QString& implementation, const QString& owner)
{
    return name == "WorkPartsRepairAdvisorFlow.cpp"
        && cs::tests::sourceIncludesDirective(implementation, "ui/tabs/orders/WorkPartsDialog.h")
        && implementation.contains("void WorkPartsDialog::openRepairAdvisorDialog()")
        && owner.contains("void openRepairAdvisorDialog();");
}

class RegressionP2ArchitectureIncModuleTests : public QObject {
    Q_OBJECT
private slots:
    void legacyIncFilesAreRemoved();
    void disconnectedOwnerBoundaryIsRejected()
    {
        const auto source = cs::tests::readSourceFile("src/ui/tabs/orders/WorkPartsRepairAdvisorFlow.cpp");
        const auto owner = cs::tests::readSourceFile("src/ui/tabs/orders/WorkPartsDialog.h");
        QVERIFY(repairAdvisorOwnerBoundary("WorkPartsRepairAdvisorFlow.cpp", source, owner));
        QVERIFY(!repairAdvisorOwnerBoundary("Other.cpp", source, owner));
        QVERIFY(!repairAdvisorOwnerBoundary("WorkPartsRepairAdvisorFlow.cpp", QString(), owner));
        QVERIFY(!repairAdvisorOwnerBoundary("WorkPartsRepairAdvisorFlow.cpp", source, QString()));
    }
    void sourceDoesNotIncludeIncFiles();
    void convertedImplementationModulesHaveBoundaries();
};

static QStringList collectFiles(const QString& root, const QStringList& suffixes)
{
    QStringList files;
    QDirIterator it(root, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        const QFileInfo info(path);
        if (!info.isFile()) continue;
        for (const QString& suffix : suffixes) {
            if (info.fileName().endsWith(suffix, Qt::CaseInsensitive)) {
                files << path;
                break;
            }
        }
    }
    return files;
}

void RegressionP2ArchitectureIncModuleTests::legacyIncFilesAreRemoved()
{
    const QString root = cs::tests::projectRoot();
    const QStringList files = collectFiles(root + QStringLiteral("/src"), {QStringLiteral(".inc")});
    QVERIFY2(files.isEmpty(), qPrintable(QStringLiteral("Legacy .inc files must be removed: %1").arg(files.join(QStringLiteral(", ")))));
}

void RegressionP2ArchitectureIncModuleTests::sourceDoesNotIncludeIncFiles()
{
    const QString root = cs::tests::projectRoot();
    const QStringList files = collectFiles(root + QStringLiteral("/src"), {QStringLiteral(".cpp"), QStringLiteral(".h"), QStringLiteral(".hpp"), QStringLiteral(".ipp")});
    for (const QString& path : files) {
        QFile file(path);
        QVERIFY2(file.open(QIODevice::ReadOnly | QIODevice::Text), qPrintable(QStringLiteral("Unable to open %1").arg(path)));
        const QString text = QString::fromUtf8(file.readAll());
        QVERIFY2(!text.contains(QStringLiteral(".inc\"")), qPrintable(QStringLiteral("Legacy .inc include remains in %1").arg(path)));
    }
}

void RegressionP2ArchitectureIncModuleTests::convertedImplementationModulesHaveBoundaries()
{
    const QString root = cs::tests::projectRoot();
    const QStringList files = collectFiles(root + QStringLiteral("/src"), {QStringLiteral(".cpp")});
    int converted = 0;
    for (const QString& path : files) {
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
        const QString text = QString::fromUtf8(file.readAll());
        if (text.contains(QStringLiteral("P2 architecture migration:"))) {
            ++converted;
            const QString headerPath = QFileInfo(path).absolutePath() + QLatin1Char('/') + QFileInfo(path).completeBaseName() + QStringLiteral(".h");
            const bool declaredOwnerMember = repairAdvisorOwnerBoundary(QFileInfo(path).fileName(), text,
                cs::tests::readSourceFile("src/ui/tabs/orders/WorkPartsDialog.h"));
            QVERIFY2(QFileInfo::exists(headerPath) || declaredOwnerMember,
                     qPrintable(QStringLiteral("Converted module has no boundary declaration: %1").arg(path)));
        }
    }
    // 53 migrated fragments plus the header-defined WorkPartsPartEditServices boundary.
    QCOMPARE(converted, 54);
}

QTEST_MAIN(RegressionP2ArchitectureIncModuleTests)
#include "RegressionP2ArchitectureIncModuleTests.moc"
