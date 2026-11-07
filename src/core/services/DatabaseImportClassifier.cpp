#include "core/services/DatabaseImportClassifier.h"
#include "infrastructure/db/sqlite/ScopedConnectionManager.h"

#include <QFileInfo>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QThread>

namespace {

QSet<QString> loadTableNames(QSqlDatabase& db)
{
    QSet<QString> names;
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral("SELECT name FROM sqlite_master WHERE type='table'"))) {
        return names;
    }
    while (q.next()) {
        names.insert(q.value(0).toString().trimmed());
    }
    return names;
}

QStringList intersectingTables(const QSet<QString>& tables, const QStringList& probe)
{
    QStringList out;
    for (const QString& name : probe) {
        if (tables.contains(name)) out << name;
    }
    return out;
}

int queryScalarCount(QSqlDatabase& db, const QString& tableName)
{
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral("SELECT COUNT(*) FROM %1").arg(tableName)) || !q.next()) {
        return 0;
    }
    return q.value(0).toInt();
}

} // namespace

bool DatabaseImportClassifier::classify(const QString& sourcePath,
                                        DatabaseImportClassification* classification,
                                        QString* errorText)
{
    DatabaseImportClassification local;
    local.sourcePath = QFileInfo(sourcePath).absoluteFilePath();
    local.fileExists = QFileInfo(local.sourcePath).exists();
    if (!local.fileExists) {
        if (classification) *classification = local;
        if (errorText) *errorText = QStringLiteral("Вибраний файл БД не знайдено");
        return false;
    }

    const QString connName = QStringLiteral("db_import_classifier_")
        + QString::number(static_cast<qulonglong>(reinterpret_cast<quintptr>(QThread::currentThread())));
    QString openError;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
        db.setDatabaseName(local.sourcePath);
        if (!db.open()) {
            openError = db.lastError().text();
        } else {
            local.sqliteReadable = true;

            const QSet<QString> tables = loadTableNames(db);
            const QStringList nativeSignature = {
                QStringLiteral("settings"),
                QStringLiteral("order_registry"),
                QStringLiteral("workspace_order_state"),
                QStringLiteral("router_cache_work_orders")
            };
            const QStringList legacySignature = {
                QStringLiteral("work_orders"),
                QStringLiteral("work_order_services"),
                QStringLiteral("work_order_parts"),
                QStringLiteral("service_masters"),
                QStringLiteral("masters"),
                QStringLiteral("parts_catalog"),
                QStringLiteral("planning_calendar"),
                QStringLiteral("expenses")
            };

            local.currentSignatureTables = intersectingTables(tables, nativeSignature);
            local.legacySignatureTables = intersectingTables(tables, legacySignature);
            if (tables.contains(QStringLiteral("work_orders"))) {
                local.workOrdersCount = queryScalarCount(db, QStringLiteral("work_orders"));
            }

            const bool hasNative = !local.currentSignatureTables.isEmpty();
            const bool hasLegacy = local.legacySignatureTables.size() >= 4
                && local.legacySignatureTables.contains(QStringLiteral("work_orders"))
                && local.legacySignatureTables.contains(QStringLiteral("work_order_services"))
                && local.legacySignatureTables.contains(QStringLiteral("work_order_parts"))
                && local.legacySignatureTables.contains(QStringLiteral("masters"));

            if (hasNative) {
                local.detectedKind = QStringLiteral("native_backup");
                local.explanation = QStringLiteral("Файл містить таблиці поточної схеми CarService: %1")
                    .arg(local.currentSignatureTables.join(QStringLiteral(", ")));
            } else if (hasLegacy) {
                local.detectedKind = QStringLiteral("legacy_import");
                local.explanation = QStringLiteral("Файл містить таблиці legacy service_center.db: %1")
                    .arg(local.legacySignatureTables.join(QStringLiteral(", ")));
            } else {
                local.detectedKind = QStringLiteral("unknown");
                local.explanation = QStringLiteral("Не вдалося однозначно визначити тип БД за структурою таблиць");
            }
            db.close();
        }
    }
    SqliteConnectionUtils::safeCloseAndRemoveDatabase(connName);

    if (!openError.isEmpty()) {
        if (classification) *classification = local;
        if (errorText) *errorText = QStringLiteral("Не вдалося відкрити SQLite-файл: %1").arg(openError);
        return false;
    }

    if (classification) *classification = local;
    if (local.detectedKind == QStringLiteral("unknown")) {
        if (errorText) *errorText = local.explanation;
        return false;
    }
    return true;
}
