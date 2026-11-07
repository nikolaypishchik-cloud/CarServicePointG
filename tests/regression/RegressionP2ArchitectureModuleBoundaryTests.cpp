#include <QtTest/QtTest>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include "../TestSourceAssertions.h"

class RegressionP2ArchitectureModuleBoundaryTests : public QObject {
    Q_OBJECT
private slots:
    void sourceDoesNotTextIncludeCppFiles();
    void convertedModulesAreCMakeSources();
    void modulePreludesExistForConvertedOwners();
};

static QStringList p2CollectFiles(const QString& root, const QStringList& suffixes)
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

static QString readTextFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    return QString::fromUtf8(file.readAll());
}

void RegressionP2ArchitectureModuleBoundaryTests::sourceDoesNotTextIncludeCppFiles()
{
    const QString root = cs::tests::projectRoot();
    const QStringList files = p2CollectFiles(root + QStringLiteral("/src"),
                                             {QStringLiteral(".cpp"), QStringLiteral(".h"), QStringLiteral(".hpp"), QStringLiteral(".ipp")});
    const QRegularExpression cppInclude(QStringLiteral(R"(^\s*#\s*include\s*[<\"][^>\"]+\.cpp[>\"]\s*$)"),
                                        QRegularExpression::MultilineOption);
    for (const QString& path : files) {
        const QString text = readTextFile(path);
        QVERIFY2(!text.isEmpty() || QFileInfo(path).size() == 0,
                 qPrintable(QStringLiteral("Unable to read %1").arg(path)));
        const auto match = cppInclude.match(text);
        QVERIFY2(!match.hasMatch(),
                 qPrintable(QStringLiteral("Direct .cpp text-include remains in %1").arg(path)));
    }
}

void RegressionP2ArchitectureModuleBoundaryTests::convertedModulesAreCMakeSources()
{
    const QString root = cs::tests::projectRoot();
    const QString cmake = cs::tests::compiledSourceInventory();
    QVERIFY2(!cmake.isEmpty(), "CMakeLists.txt must be readable");

    const QStringList files = p2CollectFiles(root + QStringLiteral("/src"), {QStringLiteral(".cpp")});
    int converted = 0;
    for (const QString& path : files) {
        const QString text = readTextFile(path);
        if (!text.contains(QStringLiteral("P2 architecture migration:"))) {
            continue;
        }
        ++converted;
        QString rel = QDir(root).relativeFilePath(path).replace(QLatin1Char('\\'), QLatin1Char('/'));
        QVERIFY2(cs::tests::cmakeListsSource(cmake, rel),
                 qPrintable(QStringLiteral("Converted implementation module is not a CMake source: %1").arg(rel)));
    }
    // 53 migrated fragments plus the header-defined WorkPartsPartEditServices boundary.
    QCOMPARE(converted, 54);
}

void RegressionP2ArchitectureModuleBoundaryTests::modulePreludesExistForConvertedOwners()
{
    const QString root = cs::tests::projectRoot();
    const QStringList preludes = p2CollectFiles(root + QStringLiteral("/src"), {QStringLiteral("ModulePrelude.h")});
    QVERIFY2(preludes.size() >= 15,
             qPrintable(QStringLiteral("Expected generated module preludes for owner roots, found %1").arg(preludes.size())));
    for (const QString& path : preludes) {
        const QString text = readTextFile(path);
        QVERIFY2(text.contains(QStringLiteral("P2 architecture module prelude")),
                 qPrintable(QStringLiteral("Invalid module prelude marker in %1").arg(path)));
    }
}

QTEST_MAIN(RegressionP2ArchitectureModuleBoundaryTests)
#include "RegressionP2ArchitectureModuleBoundaryTests.moc"
