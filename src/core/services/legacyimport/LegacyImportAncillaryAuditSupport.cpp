#include "core/services/legacyimport/LegacyImportAncillaryAuditSupport.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

namespace {
void addFinding(LegacyImportAuditReport* report,
                const QString& severity,
                const QString& code,
                const QString& entityName,
                const QString& message,
                int affectedCount,
                bool blocksExecute)
{
    if (!report) return;
    LegacyImportFinding f;
    f.severity = severity;
    f.code = code;
    f.entityName = entityName;
    f.message = message;
    f.affectedCount = affectedCount;
    f.blocksExecute = blocksExecute;
    report->findings.push_back(f);
}

bool tableExists(QSqlDatabase db, const QString& tableName)
{
    return db.tables().contains(tableName, Qt::CaseInsensitive);
}

QSet<QString> tableColumns(QSqlDatabase db, const QString& tableName)
{
    QSet<QString> out;
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral("PRAGMA table_info(%1)").arg(tableName))) return out;
    while (q.next()) out.insert(q.value(1).toString().trimmed());
    return out;
}

int scalarCount(QSqlDatabase db, const QString& sql)
{
    QSqlQuery q(db);
    if (!q.exec(sql) || !q.next()) return -1;
    return q.value(0).toInt();
}

bool parseObjectFile(const QString& path, QJsonObject* outObj, QString* errorText)
{
    if (!outObj) return false;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (errorText) *errorText = f.errorString();
        return false;
    }
    const QByteArray raw = f.readAll();
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        if (errorText) *errorText = pe.error != QJsonParseError::NoError ? pe.errorString() : QStringLiteral("JSON root is not an object");
        return false;
    }
    *outObj = doc.object();
    return true;
}
}

void LegacyImportAncillaryAuditSupport::appendTableFindings(QSqlDatabase db, LegacyImportAuditReport* report)
{
    if (!report) return;

    if (tableExists(db, QStringLiteral("parts_catalog"))) {
        const int blankCatalogNames = scalarCount(db, QStringLiteral("SELECT COUNT(*) FROM parts_catalog WHERE TRIM(COALESCE(name,''))=''"));
        if (blankCatalogNames > 0) {
            addFinding(report, QStringLiteral("warning"), QStringLiteral("catalog_blank_name"), QStringLiteral("catalog"),
                       QStringLiteral("Є записи каталогу без назви"), blankCatalogNames, false);
        }
        const int exactCatalogDuplicates = scalarCount(db, QStringLiteral(
            "SELECT COUNT(*) FROM (SELECT COALESCE(part_number,''), COALESCE(name,''), COALESCE(brand,''), COUNT(*) c FROM parts_catalog GROUP BY COALESCE(part_number,''), COALESCE(name,''), COALESCE(brand,'') HAVING c>1)"));
        if (exactCatalogDuplicates > 0) {
            addFinding(report, QStringLiteral("warning"), QStringLiteral("catalog_duplicate_rows"), QStringLiteral("catalog"),
                       QStringLiteral("Є точні дублікати рядків каталогу"), exactCatalogDuplicates, false);
        }
    }

    if (tableExists(db, QStringLiteral("planning_calendar"))) {
        const int blankDays = scalarCount(db, QStringLiteral("SELECT COUNT(*) FROM planning_calendar WHERE TRIM(COALESCE(day,''))=''"));
        if (blankDays > 0) {
            addFinding(report, QStringLiteral("critical"), QStringLiteral("planning_blank_day"), QStringLiteral("planning"),
                       QStringLiteral("Є записи planning_calendar без day"), blankDays, true);
        }
        const int duplicateDays = scalarCount(db, QStringLiteral("SELECT COUNT(*) FROM (SELECT day, COUNT(*) c FROM planning_calendar GROUP BY day HAVING c>1)"));
        if (duplicateDays > 0) {
            addFinding(report, QStringLiteral("critical"), QStringLiteral("planning_duplicate_day"), QStringLiteral("planning"),
                       QStringLiteral("Є дублікати planning day"), duplicateDays, true);
        }
    }

    if (tableExists(db, QStringLiteral("outsource_repairs"))) {
        const int blankPartNames = scalarCount(db, QStringLiteral("SELECT COUNT(*) FROM outsource_repairs WHERE TRIM(COALESCE(part_name,''))=''"));
        if (blankPartNames > 0) {
            addFinding(report, QStringLiteral("warning"), QStringLiteral("outsource_blank_part_name"), QStringLiteral("outsource"),
                       QStringLiteral("Є outsource-записи без назви вузла/деталі"), blankPartNames, false);
        }
        const QSet<QString> cols = tableColumns(db, QStringLiteral("outsource_repairs"));
        if (cols.contains(QStringLiteral("service_id"))) {
            const int crossOrderServiceRefs = scalarCount(db, QStringLiteral(
                "SELECT COUNT(*) FROM outsource_repairs o "
                "JOIN work_order_services s ON s.id=o.service_id "
                "WHERE COALESCE(o.service_id,0)>0 AND s.order_number<>o.order_number"));
            if (crossOrderServiceRefs > 0) {
                addFinding(report, QStringLiteral("warning"), QStringLiteral("outsource_cross_order_service"), QStringLiteral("outsource"),
                           QStringLiteral("Є outsource-записи з service_id з іншого наряду"), crossOrderServiceRefs, false);
            }
        }
    }

    if (tableExists(db, QStringLiteral("expenses"))) {
        const int blankPeriods = scalarCount(db, QStringLiteral("SELECT COUNT(*) FROM expenses WHERE TRIM(COALESCE(period_from,''))='' OR TRIM(COALESCE(period_to,''))=''"));
        if (blankPeriods > 0) {
            addFinding(report, QStringLiteral("warning"), QStringLiteral("expenses_blank_period"), QStringLiteral("expenses"),
                       QStringLiteral("Є витрати без повного періоду"), blankPeriods, false);
        }
        const int duplicatePeriods = scalarCount(db, QStringLiteral("SELECT COUNT(*) FROM (SELECT COALESCE(period_from,''), COALESCE(period_to,''), COUNT(*) c FROM expenses GROUP BY COALESCE(period_from,''), COALESCE(period_to,'') HAVING c>1)"));
        if (duplicatePeriods > 0) {
            addFinding(report, QStringLiteral("warning"), QStringLiteral("expenses_duplicate_period"), QStringLiteral("expenses"),
                       QStringLiteral("Є дублікати періодів витрат"), duplicatePeriods, false);
        }
    }
}

void LegacyImportAncillaryAuditSupport::appendBundleFindings(const LegacyImportBundle& bundle, LegacyImportAuditReport* report)
{
    if (!report) return;

    if (!bundle.settingsJsonPath.trimmed().isEmpty()) {
        QJsonObject settingsObj;
        QString errorText;
        if (parseObjectFile(bundle.settingsJsonPath, &settingsObj, &errorText)) {
            const QStringList requiredCompatibleKeys = {
                QStringLiteral("theme"),
                QStringLiteral("sounds_enabled"),
                QStringLiteral("volume")
            };
            int foundCompatible = 0;
            for (const QString& key : requiredCompatibleKeys) {
                if (settingsObj.contains(key)) ++foundCompatible;
            }
            report->infoMessages << QStringLiteral("settings.json: знайдено %1 сумісних верхньорівневих ключів для переносу").arg(foundCompatible);
            if (settingsObj.contains(QStringLiteral("percents")) && !settingsObj.value(QStringLiteral("percents")).isObject()) {
                addFinding(report, QStringLiteral("warning"), QStringLiteral("settings_percents_not_object"), QStringLiteral("settings_json"),
                           QStringLiteral("settings.json містить percents не у форматі object"), 1, false);
            }
        }
    }

    if (!bundle.uiStateJsonPath.trimmed().isEmpty()) {
        QJsonObject uiStateObj;
        QString errorText;
        if (parseObjectFile(bundle.uiStateJsonPath, &uiStateObj, &errorText)) {
            const QJsonObject tables = uiStateObj.value(QStringLiteral("tables")).toObject();
            if (tables.isEmpty()) {
                addFinding(report, QStringLiteral("warning"), QStringLiteral("ui_state_empty_tables"), QStringLiteral("ui_state_json"),
                           QStringLiteral("ui_state.json не містить таблиць для потенційного часткового переносу"), 0, false);
            } else {
                report->infoMessages << QStringLiteral("ui_state.json: знайдено %1 таблиць для часткового переносу").arg(tables.size());
            }
        }
    }
}
