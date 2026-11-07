#include <QtTest/QtTest>

#include <QRegularExpression>
#include <QString>

namespace {
QString normalizeVersion(QString version)
{
    version = version.trimmed();
    if (version.startsWith(QLatin1Char('v'), Qt::CaseInsensitive)) version.remove(0, 1);
    version.replace(QRegularExpression(QStringLiteral("[^0-9.]")), QString());
    while (version.endsWith(QLatin1Char('.'))) version.chop(1);
    return version;
}

bool versionTokenMatches(const QString& name, const QString& version)
{
    const QString low = name.toLower();
    const QString normalized = normalizeVersion(version);
    if (normalized.isEmpty()) return false;

    QString underscored = normalized;
    underscored.replace(QLatin1Char('.'), QLatin1Char('_'));
    QString dashed = normalized;
    dashed.replace(QLatin1Char('.'), QLatin1Char('-'));

    const QStringList variants{normalized, underscored, dashed};
    for (const QString& token : variants) {
        const QRegularExpression rx(
            QStringLiteral("(^|[^0-9a-z])v?%1(?![._-]?[0-9])(?=$|[^0-9a-z])")
                .arg(QRegularExpression::escape(token.toLower())),
            QRegularExpression::CaseInsensitiveOption);
        if (rx.match(low).hasMatch()) return true;
    }
    return false;
}
}

class UpdateReleaseContractTests final : public QObject
{
    Q_OBJECT

private slots:
    void exactVersionTokenAcceptsCanonicalInstaller()
    {
        QVERIFY(versionTokenMatches(QStringLiteral("CarServicePointG_Setup_x86_x64_v2.1.1.exe"), QStringLiteral("2.1.1")));
        QVERIFY(versionTokenMatches(QStringLiteral("CarServicePointG_Setup_x86_x64_2_1_1.exe"), QStringLiteral("2.1.1")));
        QVERIFY(versionTokenMatches(QStringLiteral("CarServicePointG-Setup-2-1-1.exe"), QStringLiteral("2.1.1")));
    }

    void exactVersionTokenRejectsAdjacentVersions()
    {
        QVERIFY(!versionTokenMatches(QStringLiteral("CarServicePointG_Setup_x86_x64_v2.10.exe"), QStringLiteral("2.1.1")));
        QVERIFY(!versionTokenMatches(QStringLiteral("CarServicePointG_Setup_x86_x64_v2.1.2.exe"), QStringLiteral("2.1.1")));
        QVERIFY(!versionTokenMatches(QStringLiteral("CarServicePointG_Setup_x86_x64_v12.1.exe"), QStringLiteral("2.1.1")));
    }

    void blockedExecutableNamesRemainBlockedByContract()
    {
        const QStringList blocked{
            QStringLiteral("vc_redist_x64_v2.1.1.exe"),
            QStringLiteral("unins000_v2.1.1.exe"),
            QStringLiteral("maintenancetool_v2.1.1.exe"),
            QStringLiteral("helper_debug_v2.1.1.exe")
        };
        for (const QString& name : blocked) {
            QVERIFY2(name.contains(QStringLiteral("vc_redist"))
                         || name.contains(QStringLiteral("unins"))
                         || name.contains(QStringLiteral("maintenancetool"))
                         || name.contains(QStringLiteral("helper"))
                         || name.contains(QStringLiteral("debug")),
                     qPrintable(name));
        }
    }
};

QTEST_MAIN(UpdateReleaseContractTests)
#include "UpdateReleaseContractTests.moc"
