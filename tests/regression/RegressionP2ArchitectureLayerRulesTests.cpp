\
    #include <QtTest/QtTest>
    #include <QDirIterator>
    #include <QFile>
    #include <QFileInfo>
    #include <QRegularExpression>
    #include "../TestSourceAssertions.h"

    class RegressionP2ArchitectureLayerRulesTests : public QObject {
        Q_OBJECT
    private slots:
        void uiDoesNotIncludeRepositoriesOrSqliteInfrastructure();
        void uiDoesNotUseSqlQueryOrDatabaseEntryPoints();
        void workPartsUsesApplicationServiceRepositoryFacade();
        void repositoriesDoNotDependOnUiOrCache();
    };

    static QStringList p2LayerCollectFiles(const QString& root, const QStringList& suffixes)
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

    static QString p2LayerReadTextFile(const QString& path)
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
        return QString::fromUtf8(file.readAll());
    }

    void RegressionP2ArchitectureLayerRulesTests::uiDoesNotIncludeRepositoriesOrSqliteInfrastructure()
    {
        const QString root = cs::tests::projectRoot();
        const QStringList files = p2LayerCollectFiles(root + QStringLiteral("/src/ui"),
                                                      {QStringLiteral(".cpp"), QStringLiteral(".h"), QStringLiteral(".hpp")});
        const QRegularExpression forbiddenInclude(
            QStringLiteral(R"(^\s*#\s*include\s*[<\"](?:\.\./)*(?:core/repository/|infrastructure/db/sqlite/))"),
            QRegularExpression::MultilineOption);
        QStringList violations;
        for (const QString& path : files) {
            const QString text = p2LayerReadTextFile(path);
            if (forbiddenInclude.match(text).hasMatch()) violations << path;
        }
        QVERIFY2(violations.isEmpty(), qPrintable(violations.join('\n')));
    }

    void RegressionP2ArchitectureLayerRulesTests::uiDoesNotUseSqlQueryOrDatabaseEntryPoints()
    {
        const QString root = cs::tests::projectRoot();
        const QStringList files = p2LayerCollectFiles(root + QStringLiteral("/src/ui"),
                                                      {QStringLiteral(".cpp"), QStringLiteral(".h"), QStringLiteral(".hpp")});
        const QRegularExpression forbiddenSql(
            QStringLiteral(R"(\bQSql(?:Database|Query|TableModel|RelationalTableModel)\b|\bDatabase::db\s*\()"));
        QStringList violations;
        for (const QString& path : files) {
            const QString text = p2LayerReadTextFile(path);
            if (forbiddenSql.match(text).hasMatch()) violations << path;
        }
        QVERIFY2(violations.isEmpty(), qPrintable(violations.join('\n')));
    }

    void RegressionP2ArchitectureLayerRulesTests::workPartsUsesApplicationServiceRepositoryFacade()
    {
        cs::tests::requireContains("src/core/services/orders/WorkPartsPersistenceRepositoryFacade.h",
                                   "class WorkPartsPersistenceRepositoryFacade");
        cs::tests::requireContains("src/core/services/orders/WorkPartsPersistenceRepositoryFacade.cpp",
                                   "PartRepository::insert");
        cs::tests::requireContains("src/ui/tabs/orders/WorkPartsDialogModulePrelude.h",
                                   "core/services/orders/WorkPartsPersistenceRepositoryFacade.h");
    }

    void RegressionP2ArchitectureLayerRulesTests::repositoriesDoNotDependOnUiOrCache()
    {
        const QString root = cs::tests::projectRoot();
        const QStringList files = p2LayerCollectFiles(root + QStringLiteral("/src/core/repository"),
                                                      {QStringLiteral(".cpp"), QStringLiteral(".h"), QStringLiteral(".hpp")});
        const QRegularExpression forbidden(QStringLiteral(R"(^\s*#\s*include\s*[<\"](?:\.\./)*(?:ui/|core/cache/)|\bQMessageBox\b)"),
                                           QRegularExpression::MultilineOption);
        for (const QString& path : files) {
            const QString text = p2LayerReadTextFile(path);
            QVERIFY2(!forbidden.match(text).hasMatch(),
                     qPrintable(QStringLiteral("Repository depends on UI/cache/notification side effect boundary: %1").arg(path)));
        }
    }

    QTEST_MAIN(RegressionP2ArchitectureLayerRulesTests)
    #include "RegressionP2ArchitectureLayerRulesTests.moc"
