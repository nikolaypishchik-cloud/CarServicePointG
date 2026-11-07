#include "infrastructure/db/sqlite/RuntimeDbPreflightValidator.h"
#include "infrastructure/db/sqlite/ScopedConnectionManager.h"

#include <QDateTime>
#include <QFileInfo>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QUuid>

namespace {

QSet<QString> objectsInDb(QSqlDatabase db, const QString& type)
{
    QSet<QString> out;
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT name FROM sqlite_master WHERE type=?"));
    q.addBindValue(type);
    if (!q.exec()) return out;
    while (q.next()) out.insert(q.value(0).toString().trimmed());
    return out;
}

QString joinMissing(const QStringList& values)
{
    return values.join(QStringLiteral(", "));
}

bool pragmaOk(QSqlDatabase db, QString* errorText)
{
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral("PRAGMA integrity_check(1)")) || !q.next()) {
        if (errorText) *errorText = q.lastError().text().trimmed().isEmpty()
            ? QStringLiteral("PRAGMA integrity_check failed")
            : q.lastError().text().trimmed();
        return false;
    }
    const QString result = q.value(0).toString().trimmed();
    if (result.compare(QStringLiteral("ok"), Qt::CaseInsensitive) != 0) {
        if (errorText) *errorText = QStringLiteral("integrity_check=%1").arg(result);
        return false;
    }
    return true;
}

} // namespace

namespace RuntimeDbPreflightValidator {

QString roleName(DbRole role)
{
    switch (role) {
    case DbRole::Workspace: return QStringLiteral("workspace");
    case DbRole::Registry: return QStringLiteral("registry");
    case DbRole::Archive: return QStringLiteral("archive");
    }
    return QStringLiteral("unknown");
}

bool validateFile(const QString& dbPath, DbRole role, QString* errorText)
{
    const QFileInfo fi(dbPath);
    if (!fi.exists()) {
        if (errorText) *errorText = QStringLiteral("DB file does not exist: %1").arg(dbPath);
        return false;
    }
    if (fi.size() <= 0) {
        if (errorText) *errorText = QStringLiteral("DB file is empty: %1").arg(dbPath);
        return false;
    }

    const QString connName = QStringLiteral("preflight_%1_%2")
        .arg(roleName(role))
        .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    bool ok = false;
    QString localError;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
        db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        db.setDatabaseName(dbPath);
        if (!db.open()) {
            localError = db.lastError().text().trimmed();
        } else if (!pragmaOk(db, &localError)) {
        } else {
            const QSet<QString> tables = objectsInDb(db, QStringLiteral("table"));
            const QSet<QString> views = objectsInDb(db, QStringLiteral("view"));
            QStringList missing;
            auto requireTable = [&](const QString& name) { if (!tables.contains(name)) missing << QStringLiteral("table:%1").arg(name); };
            auto requireView = [&](const QString& name) { if (!views.contains(name)) missing << QStringLiteral("view:%1").arg(name); };

            switch (role) {
            case DbRole::Workspace:
                requireTable(QStringLiteral("settings"));
                requireTable(QStringLiteral("work_orders"));
                requireTable(QStringLiteral("work_order_services"));
                requireTable(QStringLiteral("work_order_parts"));
                requireTable(QStringLiteral("order_registry"));
                requireTable(QStringLiteral("router_cache_work_orders"));
                requireTable(QStringLiteral("router_cache_work_order_services"));
                requireTable(QStringLiteral("router_cache_work_order_parts"));
                requireView(QStringLiteral("router_open_order_locator"));
                requireView(QStringLiteral("router_order_locator"));
                requireView(QStringLiteral("router_work_orders"));
                requireView(QStringLiteral("router_work_order_services"));
                requireView(QStringLiteral("router_work_order_parts"));
                break;
            case DbRole::Registry:
                requireTable(QStringLiteral("order_registry"));
                requireTable(QStringLiteral("part_registry"));
                requireTable(QStringLiteral("registry_meta"));
                break;
            case DbRole::Archive:
                requireTable(QStringLiteral("work_orders"));
                requireTable(QStringLiteral("work_order_services"));
                requireTable(QStringLiteral("work_order_parts"));
                break;
            }

            if (missing.isEmpty()) {
                ok = true;
            } else {
                localError = QStringLiteral("DB preflight failed for %1: %2")
                    .arg(roleName(role), joinMissing(missing));
            }
            db.close();
        }
    }
    SqliteConnectionUtils::safeCloseAndRemoveDatabase(connName);
    if (!ok && errorText) *errorText = localError.trimmed().isEmpty() ? QStringLiteral("DB preflight failed") : localError;
    return ok;
}

} // namespace RuntimeDbPreflightValidator
