#include "core/services/legacyimport/LegacyImportEntityAuditSupport.h"

#include <QSet>
#include <QSqlDatabase>
#include <QSqlQuery>

namespace {
int scalarCount(QSqlDatabase db, const QString& sql)
{
    QSqlQuery q(db);
    if (!q.exec(sql) || !q.next()) {
        return -1;
    }
    return q.value(0).toInt();
}

bool tableExists(QSqlDatabase db, const QString& tableName)
{
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT 1 FROM sqlite_master WHERE type='table' AND name=? LIMIT 1"));
    q.addBindValue(tableName);
    return q.exec() && q.next();
}

QSet<QString> tableColumns(QSqlDatabase db, const QString& tableName)
{
    QSet<QString> cols;
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral("PRAGMA table_info(%1)").arg(tableName))) {
        return cols;
    }
    while (q.next()) {
        cols.insert(q.value(1).toString().trimmed());
    }
    return cols;
}

void addFinding(LegacyImportAuditReport* report, const QString& severity, const QString& code,
                const QString& entityName, const QString& message, int affectedCount,
                bool blocksExecute)
{
    if (!report) return;
    LegacyImportFinding finding;
    finding.severity = severity;
    finding.code = code;
    finding.entityName = entityName;
    finding.message = message;
    finding.affectedCount = affectedCount;
    finding.blocksExecute = blocksExecute;
    report->findings.push_back(finding);

    const QString decorated = affectedCount >= 0
        ? QStringLiteral("[%1] %2 (affected=%3)").arg(code, message).arg(affectedCount)
        : QStringLiteral("[%1] %2").arg(code, message);
    if (severity == QStringLiteral("critical")) {
        report->errorMessages << decorated;
    } else if (severity == QStringLiteral("warning")) {
        report->warningMessages << decorated;
    } else {
        report->infoMessages << decorated;
    }
}
}

void LegacyImportEntityAuditSupport::appendEntityFindings(QSqlDatabase db, LegacyImportAuditReport* report)
{
    if (!report) return;

    const bool hasServices = tableExists(db, QStringLiteral("work_order_services"));
    const bool hasParts = tableExists(db, QStringLiteral("work_order_parts"));
    const bool hasServiceMasters = tableExists(db, QStringLiteral("service_masters"));
    const bool hasMasters = tableExists(db, QStringLiteral("masters"));

    if (hasServices) {
        const QSet<QString> serviceCols = tableColumns(db, QStringLiteral("work_order_services"));
        const int blankTitles = scalarCount(db, QStringLiteral("SELECT COUNT(*) FROM work_order_services WHERE TRIM(COALESCE(title,''))=''"));
        if (blankTitles > 0) {
            addFinding(report, QStringLiteral("warning"), QStringLiteral("blank_service_title"), QStringLiteral("services"),
                       QStringLiteral("Є роботи без назви; після міграції їх треба буде вручну перевірити"), blankTitles, false);
        }

        const int invalidQty = scalarCount(db, QStringLiteral("SELECT COUNT(*) FROM work_order_services WHERE COALESCE(qty,0)<=0"));
        if (invalidQty > 0) {
            addFinding(report, QStringLiteral("warning"), QStringLiteral("nonpositive_service_qty"), QStringLiteral("services"),
                       QStringLiteral("Є роботи з qty<=0; під час verify суми будуть перевірені окремо"), invalidQty, false);
        }

        const int negativePrice = scalarCount(db, QStringLiteral("SELECT COUNT(*) FROM work_order_services WHERE COALESCE(price,0)<0"));
        if (negativePrice > 0) {
            addFinding(report, QStringLiteral("warning"), QStringLiteral("negative_service_price"), QStringLiteral("services"),
                       QStringLiteral("Є роботи з від’ємною ціною"), negativePrice, false);
        }

        if (serviceCols.contains(QStringLiteral("parent_id"))) {
            const int crossOrderParents = scalarCount(db, QStringLiteral(
                "SELECT COUNT(*) FROM work_order_services c "
                "JOIN work_order_services p ON p.id=c.parent_id "
                "WHERE COALESCE(c.parent_id,0)<>0 AND COALESCE(p.order_number,0)<>COALESCE(c.order_number,0)"));
            if (crossOrderParents > 0) {
                addFinding(report, QStringLiteral("warning"), QStringLiteral("cross_order_service_parent"), QStringLiteral("services"),
                           QStringLiteral("Є роботи, де parent_id посилається на роботу з іншого наряду; зв’язок буде нормалізовано до NULL"), crossOrderParents, false);
            }
        }
    }

    if (hasParts) {
        const QSet<QString> partCols = tableColumns(db, QStringLiteral("work_order_parts"));

        const int blankPartNames = scalarCount(db, QStringLiteral("SELECT COUNT(*) FROM work_order_parts WHERE TRIM(COALESCE(name,''))=''"));
        if (blankPartNames > 0) {
            addFinding(report, QStringLiteral("warning"), QStringLiteral("blank_part_name"), QStringLiteral("parts"),
                       QStringLiteral("Є запчастини без назви; їх потрібно перевірити після імпорту"), blankPartNames, false);
        }

        const int blankPartNumbers = scalarCount(db, QStringLiteral("SELECT COUNT(*) FROM work_order_parts WHERE TRIM(COALESCE(part_number,''))=''"));
        if (blankPartNumbers > 0) {
            addFinding(report, QStringLiteral("info"), QStringLiteral("blank_part_number"), QStringLiteral("parts"),
                       QStringLiteral("Є legacy-запчастини без артикула; це допустимо і не повинно блокувати імпорт"), blankPartNumbers, false);
        }

        const int invalidPartQty = scalarCount(db, QStringLiteral("SELECT COUNT(*) FROM work_order_parts WHERE COALESCE(qty,0)<=0"));
        if (invalidPartQty > 0) {
            addFinding(report, QStringLiteral("warning"), QStringLiteral("nonpositive_part_qty"), QStringLiteral("parts"),
                       QStringLiteral("Є запчастини з qty<=0; суми будуть нормалізовані під час імпорту"), invalidPartQty, false);
        }

        const int negativePartPrice = scalarCount(db, QStringLiteral("SELECT COUNT(*) FROM work_order_parts WHERE COALESCE(price,0)<0"));
        if (negativePartPrice > 0) {
            addFinding(report, QStringLiteral("warning"), QStringLiteral("negative_part_price"), QStringLiteral("parts"),
                       QStringLiteral("Є запчастини з від’ємною ціною"), negativePartPrice, false);
        }

        if (partCols.contains(QStringLiteral("service_id")) && hasServices) {
            const int invalidPartService = scalarCount(db, QStringLiteral(
                "SELECT COUNT(*) FROM work_order_parts p "
                "LEFT JOIN work_order_services s ON s.id=p.service_id "
                "WHERE COALESCE(p.service_id,0)<>0 AND (s.id IS NULL OR COALESCE(s.order_number,0)<>COALESCE(p.order_number,0))"));
            if (invalidPartService > 0) {
                addFinding(report, QStringLiteral("warning"), QStringLiteral("invalid_part_service"), QStringLiteral("parts"),
                           QStringLiteral("Є запчастини з service_id, що не відповідає роботі цього ж наряду; зв’язок буде нормалізовано до 0"), invalidPartService, false);
            }
        }
    }

    if (hasMasters) {
        const QSet<QString> masterCols = tableColumns(db, QStringLiteral("masters"));
        const int blankMasterIds = scalarCount(db, QStringLiteral("SELECT COUNT(*) FROM masters WHERE TRIM(COALESCE(id,''))=''"));
        if (blankMasterIds > 0) {
            addFinding(report, QStringLiteral("critical"), QStringLiteral("blank_master_id"), QStringLiteral("personnel"),
                       QStringLiteral("У legacy БД є записи персоналу без id; execute mode повинен бути заблокований"), blankMasterIds, true);
        }

        if (masterCols.contains(QStringLiteral("role"))) {
            const int blankRoles = scalarCount(db, QStringLiteral("SELECT COUNT(*) FROM masters WHERE TRIM(COALESCE(role,''))=''"));
            if (blankRoles > 0) {
                addFinding(report, QStringLiteral("info"), QStringLiteral("blank_master_role"), QStringLiteral("personnel"),
                           QStringLiteral("Порожні ролі персоналу будуть нормалізовані до master"), blankRoles, false);
            }
        }

        if (masterCols.contains(QStringLiteral("active"))) {
            const int inactiveCount = scalarCount(db, QStringLiteral("SELECT COUNT(*) FROM masters WHERE COALESCE(active,1)=0"));
            if (inactiveCount > 0) {
                addFinding(report, QStringLiteral("info"), QStringLiteral("inactive_personnel"), QStringLiteral("personnel"),
                           QStringLiteral("У legacy БД є неактивні записи персоналу; вони будуть збережені для історії"), inactiveCount, false);
            }
        }
    }

    if (hasServiceMasters) {
        const QSet<QString> smCols = tableColumns(db, QStringLiteral("service_masters"));
        const int duplicateLinks = scalarCount(db, QStringLiteral("SELECT COUNT(*) FROM (SELECT service_id, master_id FROM service_masters GROUP BY service_id, master_id HAVING COUNT(*)>1)"));
        if (duplicateLinks > 0) {
            addFinding(report, QStringLiteral("warning"), QStringLiteral("duplicate_service_master_link"), QStringLiteral("service_masters"),
                       QStringLiteral("У legacy БД є дублікати прив’язок service_id/master_id; імпорт залишить єдину зв’язку на пару"), duplicateLinks, false);
        }

        const int blankLinkMasterIds = scalarCount(db, QStringLiteral("SELECT COUNT(*) FROM service_masters WHERE TRIM(COALESCE(master_id,''))=''"));
        if (blankLinkMasterIds > 0) {
            addFinding(report, QStringLiteral("critical"), QStringLiteral("blank_service_master_id"), QStringLiteral("service_masters"),
                       QStringLiteral("Є прив’язки робіт до порожнього master_id; execute mode повинен бути заблокований"), blankLinkMasterIds, true);
        }

        if (smCols.contains(QStringLiteral("share_percent"))) {
            const int nonPositiveShare = scalarCount(db, QStringLiteral("SELECT COUNT(*) FROM service_masters WHERE COALESCE(share_percent,0)<=0"));
            if (nonPositiveShare > 0) {
                addFinding(report, QStringLiteral("warning"), QStringLiteral("nonpositive_share_percent"), QStringLiteral("service_masters"),
                           QStringLiteral("Є service_masters з share_percent<=0; значення буде нормалізовано до 100"), nonPositiveShare, false);
            }
        }

        if (smCols.contains(QStringLiteral("role"))) {
            const int blankRoles = scalarCount(db, QStringLiteral("SELECT COUNT(*) FROM service_masters WHERE TRIM(COALESCE(role,''))=''"));
            if (blankRoles > 0) {
                addFinding(report, QStringLiteral("info"), QStringLiteral("blank_service_master_role"), QStringLiteral("service_masters"),
                           QStringLiteral("Порожні ролі у service_masters будуть нормалізовані до master"), blankRoles, false);
            }
        }
    }
}
