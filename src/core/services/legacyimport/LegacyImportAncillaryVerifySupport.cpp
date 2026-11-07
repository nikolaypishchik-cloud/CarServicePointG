#include "infrastructure/db/sqlite/SqliteSchemaInspection.h"
#include "core/services/legacyimport/LegacyImportAncillaryVerifySupport.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

namespace {
int scalarCount(QSqlDatabase db, const QString& sql)
{
    QSqlQuery q(db);
    if (!q.exec(sql) || !q.next()) return -1;
    return q.value(0).toInt();
}

void addEntity(LegacyImportVerifyReport* report, const LegacyImportVerifyEntityResult& entity)
{
    if (!report) return;
    report->entities.push_back(entity);
}

QString readJsonText(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return QString();
    return QString::fromUtf8(f.readAll());
}
}

void LegacyImportAncillaryVerifySupport::appendEntityResults(QSqlDatabase db, const LegacyImportBundle& bundle, LegacyImportVerifyReport* report)
{
    if (!report) return;

    LegacyImportVerifyEntityResult catalog;
    catalog.entityName = QStringLiteral("catalog");
    const bool catalogAbsent = SqliteSchemaInspection::tableAbsent(db, "verify_src", QStringLiteral("parts_catalog"));
    catalog.sourceCount = catalogAbsent ? 0 : scalarCount(db, QStringLiteral("SELECT COUNT(*) FROM verify_src.parts_catalog"));
    catalog.targetCount = scalarCount(db, QStringLiteral("SELECT COUNT(*) FROM parts_catalog"));
    catalog.missingCount = catalogAbsent ? 0 : scalarCount(db, QStringLiteral("SELECT COUNT(*) FROM verify_src.parts_catalog s LEFT JOIN parts_catalog t ON COALESCE(t.part_number,'')=COALESCE(s.part_number,'') AND COALESCE(t.name,'')=COALESCE(s.name,'') WHERE t.id IS NULL"));
    catalog.ok = catalog.sourceCount >= 0 && catalog.targetCount >= catalog.sourceCount && catalog.missingCount == 0;
    if (!catalog.ok) catalog.messages << QStringLiteral("source=%1 target=%2 missing=%3").arg(catalog.sourceCount).arg(catalog.targetCount).arg(catalog.missingCount);
    addEntity(report, catalog);

    LegacyImportVerifyEntityResult planning;
    planning.entityName = QStringLiteral("planning");
    const bool planningAbsent = SqliteSchemaInspection::tableAbsent(db, "verify_src", QStringLiteral("planning_calendar"));
    planning.sourceCount = planningAbsent ? 0 : scalarCount(db, QStringLiteral("SELECT COUNT(*) FROM verify_src.planning_calendar"));
    planning.targetCount = scalarCount(db, QStringLiteral("SELECT COUNT(*) FROM planning_calendar"));
    planning.missingCount = planningAbsent ? 0 : scalarCount(db, QStringLiteral("SELECT COUNT(*) FROM verify_src.planning_calendar s LEFT JOIN planning_calendar t ON t.day=s.day WHERE t.day IS NULL"));
    planning.ok = planning.sourceCount >= 0 && planning.targetCount >= planning.sourceCount && planning.missingCount == 0;
    if (!planning.ok) planning.messages << QStringLiteral("source=%1 target=%2 missing=%3").arg(planning.sourceCount).arg(planning.targetCount).arg(planning.missingCount);
    addEntity(report, planning);

    LegacyImportVerifyEntityResult outsource;
    outsource.entityName = QStringLiteral("outsource");
    const bool outsourceAbsent = SqliteSchemaInspection::tableAbsent(db, "verify_src", QStringLiteral("outsource_repairs"));
    outsource.sourceCount = outsourceAbsent ? 0 : scalarCount(db, QStringLiteral("SELECT COUNT(*) FROM verify_src.outsource_repairs"));
    outsource.targetCount = scalarCount(db, QStringLiteral("SELECT COUNT(*) FROM router_outsource_repairs"));
    outsource.missingCount = outsourceAbsent ? 0 : scalarCount(db, QStringLiteral("SELECT COUNT(*) FROM verify_src.outsource_repairs s LEFT JOIN router_outsource_repairs t ON t.order_number=s.order_number AND COALESCE(t.part_name,'')=COALESCE(s.part_name,'') AND COALESCE(t.sent_at,'')=COALESCE(s.sent_at,'') WHERE t.id IS NULL"));
    outsource.invalidLinkCount = scalarCount(db, QStringLiteral("SELECT COUNT(*) FROM router_outsource_repairs r LEFT JOIN router_work_orders o ON o.number=r.order_number WHERE o.number IS NULL"));
    outsource.ok = outsource.sourceCount >= 0 && outsource.targetCount >= outsource.sourceCount && outsource.missingCount == 0 && outsource.invalidLinkCount == 0;
    if (!outsource.ok) outsource.messages << QStringLiteral("source=%1 target=%2 missing=%3 invalid=%4").arg(outsource.sourceCount).arg(outsource.targetCount).arg(outsource.missingCount).arg(outsource.invalidLinkCount);
    addEntity(report, outsource);

    const int bridgedOutsourceJobs = scalarCount(db, QStringLiteral("SELECT COUNT(*) FROM outsource_jobs"));
    report->infoMessages << QStringLiteral("Outsource UI bridge rows in outsource_jobs: %1").arg(qMax(0, bridgedOutsourceJobs));

    LegacyImportVerifyEntityResult expenses;
    expenses.entityName = QStringLiteral("expenses");
    const bool expensesAbsent = SqliteSchemaInspection::tableAbsent(db, "verify_src", QStringLiteral("expenses"));
    expenses.sourceCount = expensesAbsent ? 0 : scalarCount(db, QStringLiteral("SELECT COUNT(*) FROM verify_src.expenses"));
    expenses.targetCount = scalarCount(db, QStringLiteral("SELECT COUNT(*) FROM expenses"));
    expenses.missingCount = expensesAbsent ? 0 : scalarCount(db, QStringLiteral("SELECT COUNT(*) FROM verify_src.expenses s LEFT JOIN expenses t ON COALESCE(t.period_from,'')=COALESCE(s.period_from,'') AND COALESCE(t.period_to,'')=COALESCE(s.period_to,'') WHERE t.id IS NULL"));
    expenses.ok = expenses.sourceCount >= 0 && expenses.targetCount >= expenses.sourceCount && expenses.missingCount == 0;
    if (!expenses.ok) expenses.messages << QStringLiteral("source=%1 target=%2 missing=%3").arg(expenses.sourceCount).arg(expenses.targetCount).arg(expenses.missingCount);
    addEntity(report, expenses);

    const int settingsApplied = scalarCount(db, QStringLiteral("SELECT COUNT(*) FROM settings WHERE key LIKE 'legacy.import.settings.applied/%'"));
    const int uiStateApplied = scalarCount(db, QStringLiteral("SELECT COUNT(*) FROM settings WHERE key LIKE 'legacy.import.ui_state.applied/%'"));
    if (settingsApplied <= 0 && !bundle.settingsJsonPath.trimmed().isEmpty()) {
        report->warningMessages << QStringLiteral("settings.json було знайдено, але не зафіксовано жодного перенесеного сумісного ключа");
    } else if (settingsApplied > 0) {
        report->infoMessages << QStringLiteral("settings.json: перенесено %1 сумісних ключів").arg(settingsApplied);
    }
    if (uiStateApplied <= 0 && !bundle.uiStateJsonPath.trimmed().isEmpty()) {
        report->warningMessages << QStringLiteral("ui_state.json було знайдено, але сумісні UI-state ключі не перенесено");
    } else if (uiStateApplied > 0) {
        report->infoMessages << QStringLiteral("ui_state.json: перенесено %1 сумісних UI-state ключів").arg(uiStateApplied);
    }

    const QString importedSettingsRaw = scalarCount(db, QStringLiteral("SELECT COUNT(*) FROM settings WHERE key='legacy.import.settings.raw_json'")) > 0
        ? QStringLiteral("yes") : QStringLiteral("no");
    const QString importedUiStateRaw = scalarCount(db, QStringLiteral("SELECT COUNT(*) FROM settings WHERE key='legacy.import.ui_state.raw_json'")) > 0
        ? QStringLiteral("yes") : QStringLiteral("no");
    report->infoMessages << QStringLiteral("Raw sidecar preservation: settings=%1 ui_state=%2").arg(importedSettingsRaw, importedUiStateRaw);
}
