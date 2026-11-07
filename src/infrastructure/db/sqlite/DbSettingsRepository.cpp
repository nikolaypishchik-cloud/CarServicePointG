#include "infrastructure/db/sqlite/Db.h"
#include "core/services/settings/PersonnelEmploymentHistory.h"
#include "infrastructure/db/sqlite/DbFacadeSupport.h"
#include "infrastructure/db/sqlite/DbArchiveInternals.h"
#include "infrastructure/db/sqlite/LegacyCanonicalImportNormalization.h"
#include "infrastructure/db/sqlite/LegacyCanonicalAncillaryImportSupport.h"
#include "infrastructure/db/sqlite/DbSchemaAuditSupport.h"
#include "infrastructure/db/sqlite/DbSettingsCacheSupport.h"
#include "infrastructure/db/sqlite/DbThreadConnectionSupport.h"
#include "infrastructure/db/sqlite/RuntimeDbPreflightValidator.h"
#include "infrastructure/db/sqlite/OrderArchiveShardSqlSupport.h"
#include "core/services/LegacyMutationLogSupport.h"
#include "core/services/OrderIdentityService.h"
#include "core/services/OrderStoragePolicyService.h"
#include "core/services/legacyimport/RuntimeRecoveryCoordinator.h"
#include "core/services/OrderArchiveRotationService.h"
#include "core/services/OrderDetailIntegrityValidator.h"
#include "core/services/PartsQueryService.h"
#include "core/services/PurchasePaymentService.h"
#include "core/services/legacyimport/LegacyOrderChildMapper.h"
#include "infrastructure/security/EncryptedStorage.h"
#include "core/diagnostics/RuntimeLoggingControl.h"

#include <QCryptographicHash>
#include <QDate>
#include <QDebug>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QTextStream>
#include <QUuid>
#include <QVariant>
#include <QVector>

#include <algorithm>
#include <limits>

QList<MasterRow> Database::masters(bool onlyActive)
{
    QList<MasterRow> out;
    QSqlQuery q(db());
    QString sql = "SELECT id, name, role, admin_percent, active, COALESCE(active_from,''), COALESCE(active_to,'') FROM masters";
    if (onlyActive) {
        // New assignments use current employees; historical reports use mastersForPeriod.
        sql += " WHERE active=1 AND (NULLIF(active_from,'') IS NULL OR date(CASE WHEN substr(trim(active_from),3,1)='.' AND substr(trim(active_from),6,1)='.' THEN CASE WHEN substr(trim(active_from),7,4) GLOB '[0-9][0-9][0-9][0-9]' THEN substr(trim(active_from),7,4) ELSE '20'||substr(trim(active_from),7,2) END||'-'||substr(trim(active_from),4,2)||'-'||substr(trim(active_from),1,2) ELSE active_from END) <= date(?))";
    }
    sql += " ORDER BY name COLLATE NOCASE";

    if (onlyActive) {
        const QString businessDate = normalizedMasterDate(QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd")));
        if (!q.prepare(sql)) {
            qWarning() << "masters query preparation failed" << q.lastError().text();
            return out;
        }
        q.addBindValue(businessDate);
        if (!q.exec()) {
            qWarning() << "masters query failed" << q.lastError().text();
            return out;
        }
    } else if (!q.exec(sql)) {
        qWarning() << "masters query failed" << q.lastError().text();
        return out;
    }

    while (q.next()) {
        MasterRow m;
        m.id = q.value(0).toString();
        m.name = q.value(1).toString();
        m.role = q.value(2).toString();
        m.adminPercent = q.value(3).toDouble();
        m.active = q.value(4).toInt() != 0;
        m.activeFrom = q.value(5).toString();
        m.activeTo = q.value(6).toString();
        out.push_back(m);
    }
    return out;
}

QList<MasterRow> Database::mastersForPeriod(const QString& fromDateIso, const QString& toDateIso)
{
    QList<MasterRow> out;
    QSqlQuery q(db());
    q.prepare(
        "SELECT id, name, role, admin_percent, active, COALESCE(active_from,''), COALESCE(active_to,'') "
        "FROM masters "
        "WHERE (NULLIF(active_from,'') IS NULL OR date(CASE WHEN substr(trim(active_from),3,1)='.' AND substr(trim(active_from),6,1)='.' THEN CASE WHEN substr(trim(active_from),7,4) GLOB '[0-9][0-9][0-9][0-9]' THEN substr(trim(active_from),7,4) ELSE '20'||substr(trim(active_from),7,2) END||'-'||substr(trim(active_from),4,2)||'-'||substr(trim(active_from),1,2) ELSE active_from END) <= date(:to_date)) "
        "AND (active=1 OR NULLIF(active_to,'') IS NULL OR date(CASE WHEN substr(trim(active_to),3,1)='.' AND substr(trim(active_to),6,1)='.' THEN CASE WHEN substr(trim(active_to),7,4) GLOB '[0-9][0-9][0-9][0-9]' THEN substr(trim(active_to),7,4) ELSE '20'||substr(trim(active_to),7,2) END||'-'||substr(trim(active_to),4,2)||'-'||substr(trim(active_to),1,2) ELSE active_to END) >= date(:from_date)) "
        "ORDER BY name COLLATE NOCASE");
    q.bindValue(QStringLiteral(":to_date"), normalizedMasterDate(toDateIso));
    q.bindValue(QStringLiteral(":from_date"), normalizedMasterDate(fromDateIso));
    if (!q.exec()) {
        qWarning() << "mastersForPeriod query failed" << q.lastError().text();
        return out;
    }

    while (q.next()) {
        MasterRow m;
        m.id = q.value(0).toString();
        m.name = q.value(1).toString();
        m.role = q.value(2).toString();
        m.adminPercent = q.value(3).toDouble();
        m.active = q.value(4).toInt() != 0;
        m.activeFrom = q.value(5).toString();
        m.activeTo = q.value(6).toString();
        if(PersonnelEmploymentHistory::overlaps(db(),m.id,QDate::fromString(normalizedMasterDate(fromDateIso),Qt::ISODate),QDate::fromString(normalizedMasterDate(toDateIso),Qt::ISODate)))out.push_back(m);
    }
    return out;
}

bool Database::upsertMaster(const MasterRow& m)
{
    if (m.id.trimmed().isEmpty()) {
        qWarning() << "upsertMaster failed: empty id";
        return false;
    }

    if (!QSqlDatabase::contains(currentConnectionName()) || !db().isValid() || !db().isOpen()) {
        if (!Database::open()) {
            qWarning() << "upsertMaster failed: database is not open";
            return false;
        }
    }

    QSqlDatabase d = db();
    if (!d.isValid() || !d.isOpen()) {
        qWarning() << "upsertMaster failed: invalid database handle";
        return false;
    }

    const QString effectiveFrom = normalizedMasterDate(m.activeFrom);
    const QString effectiveTo = m.active ? QStringLiteral("") : normalizedMasterDate(m.activeTo);

    auto bindUpsert = [&](QSqlQuery& q) {
        q.addBindValue(m.id);
        q.addBindValue(m.name);
        q.addBindValue(m.role);
        q.addBindValue(m.adminPercent);
        q.addBindValue(m.active ? 1 : 0);
        q.addBindValue(effectiveFrom);
        q.addBindValue(effectiveTo);
    };

    QSqlQuery q(d);
    q.prepare(
        "INSERT INTO masters(id, name, role, admin_percent, active, active_from, active_to) "
        "VALUES(?,?,?,?,?,?,?) "
        "ON CONFLICT(id) DO UPDATE SET "
        "name=excluded.name, "
        "role=excluded.role, "
        "admin_percent=excluded.admin_percent, "
        "active=excluded.active, "
        "active_from=CASE WHEN COALESCE(NULLIF(masters.active_from,''),'')='' THEN excluded.active_from ELSE masters.active_from END, "
        "active_to=excluded.active_to"
    );
    bindUpsert(q);

    if (!q.exec()) {
        const QString firstError = q.lastError().text();
        qWarning() << "upsertMaster failed" << firstError << "id=" << m.id << "name=" << m.name;

        if (!ensureSchema()) {
            return false;
        }

        QSqlQuery retry(d);
        retry.prepare(
            "INSERT INTO masters(id, name, role, admin_percent, active, active_from, active_to) "
            "VALUES(?,?,?,?,?,?,?) "
            "ON CONFLICT(id) DO UPDATE SET "
            "name=excluded.name, "
            "role=excluded.role, "
            "admin_percent=excluded.admin_percent, "
            "active=excluded.active, "
            "active_from=CASE WHEN COALESCE(NULLIF(masters.active_from,''),'')='' THEN excluded.active_from ELSE masters.active_from END, "
            "active_to=excluded.active_to"
        );
        bindUpsert(retry);
        if (!retry.exec()) {
            qWarning() << "upsertMaster retry failed" << retry.lastError().text() << "id=" << m.id << "name=" << m.name;
            return false;
        }
    }
    return true;
}


bool Database::setMasterActive(const QString& id, bool active, const QString& effectiveDateIso)
{
    QString error;
    const bool ok=PersonnelEmploymentHistory::setActive(db(),id,active,
        effectiveDateIso.isEmpty()?QDate::currentDate().toString(Qt::ISODate):normalizedMasterDate(effectiveDateIso),&error);
    if(!ok)qWarning()<<"setMasterActive failed"<<error;
    return ok;
}

bool Database::deleteMasterIfUnusedOrArchive(const QString& id, const QString& effectiveDateIso)
{
    // Personnel changes are calendar-bound: keep history and archive instead of hard delete.
    return setMasterActive(id, false, effectiveDateIso);
}

static QString payrollPercentsKey() { return QStringLiteral("payroll_percents"); }

static QString payrollPercentMasterNameKey(QString value)
{
    return value.simplified().toCaseFolded();
}

QMap<QString, double> Database::payrollPercents()
{
    QMap<QString, double> out;
    const QVariant v = getSetting(payrollPercentsKey(), QStringLiteral("{}"));
    const QString s = v.toString().trimmed();
    if (s.isEmpty()) return out;

    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(s.toUtf8(), &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        return out;
    }
    const QJsonObject obj = doc.object();
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        out[it.key()] = it.value().toDouble(0.0);
    }
    return out;
}

static bool savePayrollPercents(const QMap<QString, double>& m)
{
    QJsonObject obj;
    for (auto it = m.begin(); it != m.end(); ++it) {
        obj.insert(it.key(), it.value());
    }
    const QJsonDocument doc(obj);
    return Database::setSetting(payrollPercentsKey(), QString::fromUtf8(doc.toJson(QJsonDocument::Compact)));
}

double Database::effectivePayrollPercent(const QString& masterId, const QString& masterName)
{
    const QMap<QString, double> byId = payrollPercents();
    if (!masterId.isEmpty() && byId.contains(masterId)) return byId.value(masterId, 0.0);

    QString resolvedName = masterName.trimmed();
    const auto allMasters = Database::masters(false);
    if (resolvedName.isEmpty() && !masterId.isEmpty()) {
        for (const auto& row : allMasters) {
            if (row.id == masterId) {
                resolvedName = row.name;
                break;
            }
        }
    }

    const QString key = payrollPercentMasterNameKey(resolvedName);
    if (key.isEmpty()) return 0.0;

    bool hasAny = false;
    double anyPct = 0.0;
    bool hasActive = false;
    double activePct = 0.0;

    for (const auto& row : allMasters) {
        if (row.role != QStringLiteral("master")) continue;
        if (payrollPercentMasterNameKey(row.name) != key) continue;
        if (!byId.contains(row.id)) continue;

        const double pct = byId.value(row.id, 0.0);
        if (!hasAny || (qFuzzyIsNull(anyPct) && !qFuzzyIsNull(pct))) {
            anyPct = pct;
            hasAny = true;
        }
        if (row.active && (!hasActive || (qFuzzyIsNull(activePct) && !qFuzzyIsNull(pct)))) {
            activePct = pct;
            hasActive = true;
        }
    }

    if (hasActive) return activePct;
    if (hasAny) return anyPct;
    return 0.0;
}

bool Database::setPayrollPercent(const QString& masterId, double percent)
{
    QMap<QString, double> m = payrollPercents();
    m[masterId] = percent;

    QString targetName;
    QString targetRole;
    const auto allMasters = Database::masters(false);
    for (const auto& row : allMasters) {
        if (row.id == masterId) {
            targetName = row.name;
            targetRole = row.role;
            break;
        }
    }

    const QString key = payrollPercentMasterNameKey(targetName);
    if (targetRole == QStringLiteral("master") && !key.isEmpty()) {
        for (const auto& row : allMasters) {
            if (row.role != QStringLiteral("master")) continue;
            if (payrollPercentMasterNameKey(row.name) != key) continue;
            m[row.id] = percent;
        }
    }

    return savePayrollPercents(m);
}

bool Database::removePayrollPercent(const QString& masterId)
{
    QMap<QString, double> m = payrollPercents();
    if (!m.contains(masterId)) return true;

    QString targetName;
    QString targetRole;
    const auto allMasters = Database::masters(false);
    for (const auto& row : allMasters) {
        if (row.id == masterId) {
            targetName = row.name;
            targetRole = row.role;
            break;
        }
    }

    m.remove(masterId);

    const QString key = payrollPercentMasterNameKey(targetName);
    if (targetRole == QStringLiteral("master") && !key.isEmpty()) {
        for (const auto& row : allMasters) {
            if (row.role != QStringLiteral("master")) continue;
            if (payrollPercentMasterNameKey(row.name) != key) continue;
            m.remove(row.id);
        }
    }

    return savePayrollPercents(m);
}


bool Database::unpaidPurchasesSummary(int& countOut, double& sumOut)
{
    countOut = 0;
    sumOut = 0.0;

    QSqlDatabase database = db();
    if (!database.isValid() || (!database.isOpen() && !database.open())) return false;

    QString error;
    const auto rows = PurchasePaymentService::debts(database, &error);
    if (!error.isEmpty()) { qWarning() << "unpaidPurchasesSummary failed" << error; return false; }
    qint64 totalCents=0;
    for (const auto& debt : rows) {
        ++countOut;
        totalCents += debt.remainingCents();
    }
    sumOut=totalCents/100.0;
    return true;
}

bool Database::setPartPaid(int partId, bool paid)
{
    const PartRowSnapshot beforeRow = PartsQueryService::loadRowById(db(), partId, nullptr);
    if (!beforeRow.id) return false;
    const auto debt = PurchasePaymentService::detail(db(), "part", partId, beforeRow.orderUid);
    if (paid && debt.remainingCents() == 0) return true;
    QString error;
    const bool ok = paid ? PurchasePaymentService::pay(db(), debt, debt.remainingCents(), &error)
                         : PurchasePaymentService::resetPart(db(), partId, &error);
    if (!ok) {
        qWarning() << "setPartPaid failed" << error;
        return false;
    }
    PartsQueryService::invalidateCache();
    const PartRowSnapshot afterRow = PartsQueryService::loadRowById(db(), partId, nullptr);
    if (RuntimeLoggingControl::deepDebugEnabled()) qInfo().noquote() << QStringLiteral("PART_PAID_CHANGED partId=") << partId
                      << QStringLiteral(" oldPaid=") << beforeRow.paid
                      << QStringLiteral(" newPaid=") << paid
                      << QStringLiteral(" oldRank=") << beforeRow.overdueRank
                      << QStringLiteral(" newRank=") << afterRow.overdueRank;
    return true;
}

bool Database::setPartInstalledAt(int partId, const QString& installedAtIso)
{
    const PartRowSnapshot beforeRow = PartsQueryService::loadRowById(db(), partId, nullptr);
    QSqlQuery q(db());
    q.prepare("UPDATE work_order_parts SET installed_at=? WHERE id=?");
    q.addBindValue(installedAtIso.trimmed());
    q.addBindValue(partId);
    if (!q.exec()) {
        qWarning() << "setPartInstalledAt failed" << q.lastError().text();
        return false;
    }
    PartsQueryService::invalidateCache();
    const PartRowSnapshot afterRow = PartsQueryService::loadRowById(db(), partId, nullptr);
    if (RuntimeLoggingControl::deepDebugEnabled()) qInfo().noquote() << QStringLiteral("PART_INSTALLED_CHANGED partId=") << partId
                      << QStringLiteral(" oldInstalled=") << beforeRow.installed
                      << QStringLiteral(" newInstalled=") << afterRow.installed
                      << QStringLiteral(" oldRank=") << beforeRow.overdueRank
                      << QStringLiteral(" newRank=") << afterRow.overdueRank;
    return true;
}


QVariant Database::getSetting(const QString& key, const QVariant& def)
{
    bool cached = false;
    const QVariant cachedValue = DbSettingsCacheSupport::tryGet(key, &cached);
    if (cached) {
        return (cachedValue.isValid() && !cachedValue.isNull()) ? cachedValue : def;
    }

    QSqlQuery q(Database::db());
    q.prepare(QStringLiteral("SELECT value FROM settings WHERE \"key\"=:key LIMIT 1"));
    q.bindValue(QStringLiteral(":key"), key);
    if (!q.exec()) {
        qWarning() << "getSetting failed:" << q.lastError().text();
        const QVariant fallback = qSettingsFallbackValue(key);
        const QVariant resolved = (fallback.isValid() && !fallback.isNull()) ? fallback : def;
        DbSettingsCacheSupport::store(key, resolved);
        return resolved;
    }
    if (q.next()) {
        const QVariant value = q.value(0);
        if (value.isValid() && !value.isNull()) {
            DbSettingsCacheSupport::store(key, value);
            return value;
        }
    }

    const QVariant fallback = qSettingsFallbackValue(key);
    if (fallback.isValid() && !fallback.isNull()) {
        QSqlQuery up(Database::db());
        up.prepare(QStringLiteral("INSERT INTO settings(\"key\", value) VALUES(:key, :value) ON CONFLICT(\"key\") DO UPDATE SET value=excluded.value"));
        up.bindValue(QStringLiteral(":key"), key);
        up.bindValue(QStringLiteral(":value"), fallback);
        if (!up.exec()) {
            qWarning() << "getSetting backfill failed:" << key << up.lastError().text();
        }
        DbSettingsCacheSupport::store(key, fallback);
        return fallback;
    }
    DbSettingsCacheSupport::store(key, def);
    return def;
}

bool Database::setSetting(const QString& key, const QVariant& value)
{
    // settings.value is a NOT NULL TEXT column. QString() is null in Qt,
    // therefore normalize the textual representation before binding it.
    QString safeText = value.toString();
    if (safeText.isNull()) safeText = QStringLiteral("");
    mirrorSettingToQSettings(key, safeText);

    QSqlQuery q(Database::db());
    q.prepare("INSERT INTO settings(key, value) VALUES(?, ?) "
              "ON CONFLICT(key) DO UPDATE SET value=excluded.value");
    q.addBindValue(key);
    q.addBindValue(safeText);
    if (!q.exec()) {
        qWarning() << "setSetting failed:" << key << q.lastError().text();
        DbSettingsCacheSupport::remove(key);
        return false;
    }
    DbSettingsCacheSupport::store(key, safeText);
    return true;
}
static QString getSettingForDb(QSqlDatabase database, const QString& key, const QString& def = QString())
{
    if (!database.isValid()) return def;
    if (!database.isOpen() && !database.open()) return def;
    QSqlQuery q(database);
    q.prepare(QStringLiteral("SELECT value FROM settings WHERE \"key\"=? LIMIT 1"));
    q.addBindValue(key);
    if (!q.exec() || !q.next()) return def;
    return q.value(0).toString();
}

static bool setSettingForDb(QSqlDatabase database, const QString& key, const QVariant& value)

{
    if (!database.isValid()) return false;
    if (!database.isOpen() && !database.open()) return false;
    QSqlQuery q(database);
    q.prepare(QStringLiteral("INSERT INTO settings(\"key\", value) VALUES(?, ?) ON CONFLICT(\"key\") DO UPDATE SET value=excluded.value"));
    q.addBindValue(key);
    q.addBindValue(value);
    return q.exec();
}
