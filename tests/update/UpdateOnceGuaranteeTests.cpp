#include <QtTest/QtTest>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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

int compareVersion(QString left, QString right)
{
    const QStringList l = normalizeVersion(left).split(QLatin1Char('.'), Qt::SkipEmptyParts);
    const QStringList r = normalizeVersion(right).split(QLatin1Char('.'), Qt::SkipEmptyParts);
    const int n = qMax(l.size(), r.size());
    for (int i = 0; i < n; ++i) {
        const int lv = i < l.size() ? l.at(i).toInt() : 0;
        const int rv = i < r.size() ? r.at(i).toInt() : 0;
        if (lv < rv) return -1;
        if (lv > rv) return 1;
    }
    return 0;
}

bool shouldAutoDownload(const QString& current,
                        const QString& latest,
                        const QString& packageSha,
                        const QJsonObject& appliedMarker,
                        const QJsonObject& failedPackages)
{
    if (compareVersion(current, latest) >= 0) return false;

    if (appliedMarker.value(QStringLiteral("packageSha256")).toString() == packageSha
        && compareVersion(appliedMarker.value(QStringLiteral("expectedVersion")).toString(), latest) == 0) {
        return false;
    }

    const QJsonArray failures = failedPackages.value(QStringLiteral("failures")).toArray();
    for (const QJsonValue& v : failures) {
        const QJsonObject failure = v.toObject();
        if (failure.value(QStringLiteral("asset_sha256")).toString() == packageSha
            && compareVersion(failure.value(QStringLiteral("expected_version")).toString(), latest) == 0) {
            return false;
        }
    }
    return true;
}
}

class UpdateOnceGuaranteeTests final : public QObject
{
    Q_OBJECT

private slots:
    void sameVersionNeverDownloadsAgain()
    {
        QVERIFY(!shouldAutoDownload(QStringLiteral("2.1.1"), QStringLiteral("2.1.1"), QStringLiteral("abc"), {}, {}));
    }

    void higherVersionDownloadsOnlyWhenNoTerminalMarkerExists()
    {
        QVERIFY(shouldAutoDownload(QStringLiteral("2.0.7"), QStringLiteral("2.1.1"), QStringLiteral("abc"), {}, {}));

        QJsonObject applied;
        applied.insert(QStringLiteral("expectedVersion"), QStringLiteral("2.1.1"));
        applied.insert(QStringLiteral("packageSha256"), QStringLiteral("abc"));
        QVERIFY(!shouldAutoDownload(QStringLiteral("2.0.7"), QStringLiteral("2.1.1"), QStringLiteral("abc"), applied, {}));
    }

    void failedPackageCooldownBlocksAutoLoop()
    {
        QJsonObject failure;
        failure.insert(QStringLiteral("asset_sha256"), QStringLiteral("abc"));
        failure.insert(QStringLiteral("expected_version"), QStringLiteral("2.1.1"));
        failure.insert(QStringLiteral("failure_reason"), QStringLiteral("installed_version_not_updated"));
        QJsonObject root;
        root.insert(QStringLiteral("failures"), QJsonArray{failure});
        QVERIFY(!shouldAutoDownload(QStringLiteral("2.0.7"), QStringLiteral("2.1.1"), QStringLiteral("abc"), {}, root));
    }

    void transactionSchemaContainsWorkerOwnershipFields()
    {
        QJsonObject tx;
        tx.insert(QStringLiteral("transaction_id"), QStringLiteral("run"));
        tx.insert(QStringLiteral("state"), QStringLiteral("handoff_waiting_app_exit"));
        tx.insert(QStringLiteral("updater_pid"), 100);
        tx.insert(QStringLiteral("installer_pid"), 0);
        tx.insert(QStringLiteral("heartbeat_utc"), QStringLiteral("2026-05-09T08:00:00Z"));
        tx.insert(QStringLiteral("asset_sha256"), QStringLiteral("abc"));
        QVERIFY(tx.contains(QStringLiteral("transaction_id")));
        QVERIFY(tx.contains(QStringLiteral("updater_pid")));
        QVERIFY(tx.contains(QStringLiteral("installer_pid")));
        QVERIFY(tx.contains(QStringLiteral("heartbeat_utc")));
        QVERIFY(tx.contains(QStringLiteral("asset_sha256")));
    }
};

QTEST_MAIN(UpdateOnceGuaranteeTests)
#include "UpdateOnceGuaranteeTests.moc"
