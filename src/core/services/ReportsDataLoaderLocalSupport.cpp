#include "core/util/DateFormats.h"
#include "core/services/settings/PersonnelEmploymentHistory.h"
// P2 architecture migration: implementation module converted from legacy .ipp implementation-fragment decomposition.
// Compiled as a separate P2 implementation module; include-order dependencies are isolated in the module prelude.

#include "core/services/ReportsDataLoaderModulePrelude.h"
#include "core/services/ReportsDataLoaderLocalSupport.h"
#include "core/services/orders/PartMarkupPolicyService.h"

#include "core/util/StoreNameNormalizer.h"
namespace ReportsDataLoader::LocalSupport {

QString parsePlateForUi(const QString& value)
{
    const QString out = PlateUtils::formatPlateDisplay(value);
    return out.trimmed().isEmpty() ? value.trimmed() : out.trimmed();
}

QString money0(double value)
{
    return FormatUtils::money(value, 0);
}

QString normalizeStoreForPdf(QString store)
{
    return StoreNameNormalizer::canonicalDisplayNameOrDefault(store);
}

QString normalizeInvoiceNumberForReports(QString raw)
{
    raw = raw.trimmed();
    if (raw.isEmpty()) return {};

    raw.replace(QChar(0x00A0), QLatin1Char(' '));
    raw = raw.simplified();

    const QList<QRegularExpression> patterns = {
        QRegularExpression(QStringLiteral(R"((?:видаткова\s+накладна|накладна|документ)\s*(?:№|N|No|Номер)?\s*[:\-]?\s*([A-Za-zА-Яа-яІіЇїЄєҐґ0-9][A-Za-zА-Яа-яІіЇїЄєҐґ0-9/\_.\-]{1,47}))"), QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(QStringLiteral(R"((?:номер\s+накладн(?:ої|ой)?|номер\s+документа|номер|документ\s*№)\s*[:\-]?\s*([A-Za-zА-Яа-яІіЇїЄєҐґ0-9][A-Za-zА-Яа-яІіЇїЄєҐґ0-9/\_.\-]{1,47}))"), QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(QStringLiteral(R"((?:^|\s)№\s*([A-Za-zА-Яа-яІіЇїЄєҐґ0-9][A-Za-zА-Яа-яІіЇїЄєҐґ0-9/\_.\-]{1,47}))"), QRegularExpression::CaseInsensitiveOption)
    };

    for (const QRegularExpression& rx : patterns) {
        const QRegularExpressionMatch match = rx.match(raw);
        if (match.hasMatch()) {
            QString candidate = match.captured(1).trimmed();
            candidate.remove(QRegularExpression(QStringLiteral(R"(^[№#:\-\s]+)")));
            candidate.remove(QRegularExpression(QStringLiteral(R"([,;:.\s]+$)")));
            if (!candidate.isEmpty()) return candidate;
        }
    }

    raw.remove(QRegularExpression(QStringLiteral(R"(^[№#:\-\s]+)")));
    raw.remove(QRegularExpression(QStringLiteral(R"([,;:.\s]+$)")));
    return raw.trimmed();
}

QString inferStoreFromInvoiceNumberForReports(QString rawInvoice)
{
    const QString raw = rawInvoice.trimmed();
    if (raw.isEmpty()) return {};

    const QString folded = raw.toCaseFolded();
    if (folded.contains(QString::fromUtf8("автотехнікс")) ||
        folded.contains(QString::fromUtf8("автотехникс")) ||
        folded.contains(QStringLiteral("autotechnics")) ||
        folded.contains(QStringLiteral("autotehnix")) ||
        folded.contains(QStringLiteral("autotechnix"))) {
        return QString::fromUtf8("Автотехнікс");
    }
    if (folded.contains(QString::fromUtf8("автонова")) ||
        folded.contains(QStringLiteral("avtonova"))) {
        return QString::fromUtf8("Автонова-D");
    }
    if (folded.contains(QString::fromUtf8("еліт")) ||
        folded.contains(QString::fromUtf8("элит")) ||
        folded.contains(QStringLiteral("elit"))) {
        return QString::fromUtf8("Еліт");
    }

    QString normalizedInvoice = normalizeInvoiceNumberForReports(raw).trimmed().toUpper();
    normalizedInvoice.remove(QRegularExpression(QStringLiteral(R"([^A-ZА-ЯІЇЄҐ0-9\-]+)")));
    if (normalizedInvoice.startsWith(QStringLiteral("SI"))) {
        return QString::fromUtf8("Автотехнікс");
    }
    if (normalizedInvoice.startsWith(QStringLiteral("A-")) ||
        normalizedInvoice.startsWith(QStringLiteral("А-")) ||
        normalizedInvoice.startsWith(QStringLiteral("AN-"))) {
        return QString::fromUtf8("Автонова-D");
    }

    return {};
}

QString resolveStoreForReports(const QString& rawStore, const QString& rawInvoice)
{
    const QString normalizedStore = normalizeStoreForPdf(rawStore);
    if (normalizedStore != QString::fromUtf8("(без магазину)")) {
        return normalizedStore;
    }
    const QString inferredStore = inferStoreFromInvoiceNumberForReports(rawInvoice);
    if (!inferredStore.trimmed().isEmpty()) {
        return normalizeStoreForPdf(inferredStore);
    }
    return normalizedStore;
}

QString canonicalizeOilGradesForReport(QString text)
{
    static const QRegularExpression oilRx(
        QStringLiteral(R"((?<!\d)(\d{1,2})\s*([WwВв])\s*[-/]?\s*(\d{2})(?!\d))"),
        QRegularExpression::UseUnicodePropertiesOption);
    text.replace(oilRx, QStringLiteral("\\1W\\3"));
    return text;
}

QString detectOilGradeForReport(const QString& text)
{
    static const QRegularExpression re(
        QStringLiteral(R"((?<!\d)(\d{1,2})\s*[WwВв]\s*[-/]?\s*(\d{2})(?!\d))"),
        QRegularExpression::UseUnicodePropertiesOption);
    const auto m = re.match(text);
    if (!m.hasMatch()) return QString();
    return QStringLiteral("%1W%2").arg(m.captured(1)).arg(m.captured(2));
}

QString normalizeOilGradeForReport(QString text)
{
    text = canonicalizeOilGradesForReport(text.trimmed()).toUpper();
    text.replace(QChar(0x0412), QLatin1Char('W'));
    text.replace(QChar(0x0432), QLatin1Char('W'));
    text.remove(QRegularExpression(QStringLiteral("\\s+")));
    text.remove(QRegularExpression(QStringLiteral("[-/]")));
    return text;
}

bool oilGradeLessThan(const QString& a, const QString& b)
{
    static const QRegularExpression re(QStringLiteral(R"((\d{1,2})W(\d{2}))"));
    const QString na = normalizeOilGradeForReport(a);
    const QString nb = normalizeOilGradeForReport(b);
    const auto ma = re.match(na);
    const auto mb = re.match(nb);
    if (ma.hasMatch() && mb.hasMatch()) {
        const int aCold = ma.captured(1).toInt();
        const int bCold = mb.captured(1).toInt();
        if (aCold != bCold) return aCold < bCold;
        const int aHot = ma.captured(2).toInt();
        const int bHot = mb.captured(2).toInt();
        if (aHot != bHot) return aHot < bHot;
    }
    return na.localeAwareCompare(nb) < 0;
}

bool isMotorOilName(QString name)
{
    return OilServiceClassifier::isMotorOilProductName(canonicalizeOilGradesForReport(name));
}

QString canonStoreKey(const QString& s)
{
    return StoreNameNormalizer::canonKey(s);
}

QVariant reportsWorkerSetting(QSqlDatabase& db, const QString& key, const QVariant& def)
{
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT value FROM settings WHERE key=?"));
    q.addBindValue(key);
    if (q.exec() && q.next()) {
        const QVariant value = q.value(0);
        if (value.isValid() && !value.isNull()) return value;
    }
    return def;
}

QString normalizedMasterDateForReports(QString value)
{
    const QDate parsed = DateFormats::parseDate(value);
    if (parsed.isValid()) return parsed.toString(Qt::ISODate);
    value = value.trimmed();
    if (value.isEmpty()) return QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"));

    const QString raw10 = value.left(10);
    const QStringList formats = {
        QStringLiteral("yyyy-MM-dd"),
        QStringLiteral("dd.MM.yyyy"),
        QStringLiteral("dd.MM.yy"),
        QStringLiteral("yyyy/MM/dd"),
        QStringLiteral("dd/MM/yyyy")
    };
    for (const QString& fmt : formats) {
        const QDate d = QDate::fromString(raw10, fmt);
        if (d.isValid()) return d.toString(QStringLiteral("yyyy-MM-dd"));
    }
    const QDate iso = DateFormats::parseDate(raw10);
    if (iso.isValid()) return iso.toString(QStringLiteral("yyyy-MM-dd"));
    return QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"));
}

QList<MasterRow> loadMastersForPeriodForReports(QSqlDatabase& db, const QString& fromDateIso, const QString& toDateIso)
{
    QList<MasterRow> out;
    QSqlQuery q(db);
    q.prepare(
        QStringLiteral("SELECT id, name, role, admin_percent, active, COALESCE(active_from,''), COALESCE(active_to,'') "
                       "FROM masters "
                       "WHERE (NULLIF(active_from,'') IS NULL OR date(CASE WHEN substr(trim(active_from),3,1)='.' AND substr(trim(active_from),6,1)='.' THEN CASE WHEN substr(trim(active_from),7,4) GLOB '[0-9][0-9][0-9][0-9]' THEN substr(trim(active_from),7,4) ELSE '20'||substr(trim(active_from),7,2) END||'-'||substr(trim(active_from),4,2)||'-'||substr(trim(active_from),1,2) ELSE active_from END) <= date(?)) "
                       "AND (active=1 OR NULLIF(active_to,'') IS NULL OR date(CASE WHEN substr(trim(active_to),3,1)='.' AND substr(trim(active_to),6,1)='.' THEN CASE WHEN substr(trim(active_to),7,4) GLOB '[0-9][0-9][0-9][0-9]' THEN substr(trim(active_to),7,4) ELSE '20'||substr(trim(active_to),7,2) END||'-'||substr(trim(active_to),4,2)||'-'||substr(trim(active_to),1,2) ELSE active_to END) >= date(?)) "
                       "ORDER BY name COLLATE NOCASE"));
    q.addBindValue(normalizedMasterDateForReports(toDateIso));
    q.addBindValue(normalizedMasterDateForReports(fromDateIso));
    if (!q.exec()) return out;
    while (q.next()) {
        MasterRow m;
        m.id = q.value(0).toString();
        m.name = q.value(1).toString();
        m.role = q.value(2).toString();
        m.adminPercent = q.value(3).toDouble();
        m.active = q.value(4).toInt() != 0;
        m.activeFrom = q.value(5).toString();
        m.activeTo = q.value(6).toString();
        if(PersonnelEmploymentHistory::overlaps(db,m.id,QDate::fromString(normalizedMasterDateForReports(fromDateIso),Qt::ISODate),QDate::fromString(normalizedMasterDateForReports(toDateIso),Qt::ISODate)))out.push_back(m);
    }
    return out;
}

QList<MasterRow> loadAllMastersForReports(QSqlDatabase& db)
{
    QList<MasterRow> out;
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral("SELECT id, name, role, admin_percent, active, COALESCE(active_from,''), COALESCE(active_to,'') FROM masters ORDER BY name COLLATE NOCASE"))) return out;
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

QMap<QString, double> loadPayrollPercentsForReports(QSqlDatabase& db)
{
    QMap<QString, double> out;
    const QString s = reportsWorkerSetting(db, QStringLiteral("payroll_percents"), QStringLiteral("{}")).toString().trimmed();
    if (s.isEmpty()) return out;
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(s.toUtf8(), &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) return out;
    const QJsonObject obj = doc.object();
    for (auto it = obj.begin(); it != obj.end(); ++it) out[it.key()] = it.value().toDouble(0.0);
    return out;
}

QString makeReportsWorkerConnectionName(const QString& suffix)
{
    return QStringLiteral("reports_worker_%1_%2_%3")
        .arg(suffix,
             QString::number(QDateTime::currentMSecsSinceEpoch()),
             QString::number(reinterpret_cast<quintptr>(QThread::currentThread())));
}


QString payrollMasterNameKey(QString value)
{
    return value.simplified().toCaseFolded();
}

QList<MasterRow> orderedPersonnelForReports(const QList<MasterRow>& personnel)
{
    QList<MasterRow> masters;
    QList<MasterRow> admins;
    QList<MasterRow> otherRoles;

    masters.reserve(personnel.size());
    admins.reserve(personnel.size());
    otherRoles.reserve(personnel.size());

    for (const auto& person : personnel) {
        if (person.role == QStringLiteral("master")) {
            masters.append(person);
        } else if (person.role == QStringLiteral("admin")) {
            admins.append(person);
        } else {
            otherRoles.append(person);
        }
    }

    QList<MasterRow> ordered = masters;
    ordered.append(admins);
    ordered.append(otherRoles);
    return ordered;
}

QString normalizeAdminSalarySourceKeyForReports(QString source)
{
    source = source.trimmed();
    if (source == QStringLiteral("client_total")
        || source == QStringLiteral("purchase_total")
        || source == QStringLiteral("margin")) {
        return source;
    }
    if (source == QStringLiteral("all") || source == QStringLiteral("parts")) {
        return QStringLiteral("client_total");
    }
    if (source == QStringLiteral("services")) {
        return QStringLiteral("margin");
    }
    return QStringLiteral("margin");
}

double adminSalaryBaseForSource(const AdminSalaryTotals& totals, const QString& source)
{
    const QString normalizedSource = normalizeAdminSalarySourceKeyForReports(source);
    if (normalizedSource == QStringLiteral("client_total")) return totals.clientTotal;
    if (normalizedSource == QStringLiteral("purchase_total")) return totals.purchaseTotal;
    return totals.margin;
}

PayrollPercentResolver makePayrollPercentResolverForReports(QSqlDatabase& db)
{
    PayrollPercentResolver out;
    out.byId = loadPayrollPercentsForReports(db);
    const auto allMasters = loadAllMastersForReports(db);
    for (const auto& master : allMasters) {
        if (master.role != QStringLiteral("master") || !master.active) continue;
        if (!out.byId.contains(master.id)) continue;
        const QString key = payrollMasterNameKey(master.name);
        if (key.isEmpty()) continue;
        const double pct = out.byId.value(master.id, 0.0);
        if (!out.activeByName.contains(key) || (qFuzzyIsNull(out.activeByName.value(key)) && !qFuzzyIsNull(pct))) {
            out.activeByName.insert(key, pct);
        }
    }
    return out;
}

QMap<QString, QString> loadAdminSalarySourcesForReports(QSqlDatabase& db)
{
    QMap<QString, QString> out;
    const auto doc = QJsonDocument::fromJson(reportsWorkerSetting(db, QStringLiteral("settings.staff.admin_salary_sources"), QStringLiteral("{}")).toString().toUtf8());
    if (!doc.isObject()) return out;
    const auto obj = doc.object();
    for (auto it = obj.begin(); it != obj.end(); ++it) out.insert(it.key(), normalizeAdminSalarySourceKeyForReports(it.value().toString(QStringLiteral("margin"))));
    return out;
}

double periodExpensesTotalForReports(QSqlDatabase& db, const QString& df, const QString& dt)
{
    QSqlQuery ex(db);
    ex.prepare(
        QStringLiteral("SELECT COALESCE(SUM(COALESCE(elec_cost, ((COALESCE(elec_curr,0)-COALESCE(elec_prev,0))*COALESCE(elec_tariff,0))) + "
                       "COALESCE(cleaning,0) + COALESCE(heating,0) + COALESCE(rent,0) + COALESCE(amortization,0) + "
                       "COALESCE(sw_opendatabot,0) + COALESCE(sw_tecalliance,0) + COALESCE(other_amount,0)),0) "
                       "FROM expenses WHERE date((CASE WHEN substr(trim(period_to),3,1)='.' AND substr(trim(period_to),6,1)='.' THEN CASE WHEN substr(trim(period_to),7,4) GLOB '[0-9][0-9][0-9][0-9]' THEN substr(trim(period_to),7,4) ELSE '20'||substr(trim(period_to),7,2) END||'-'||substr(trim(period_to),4,2)||'-'||substr(trim(period_to),1,2) ELSE period_to END)) >= date(?) AND date((CASE WHEN substr(trim(period_from),3,1)='.' AND substr(trim(period_from),6,1)='.' THEN CASE WHEN substr(trim(period_from),7,4) GLOB '[0-9][0-9][0-9][0-9]' THEN substr(trim(period_from),7,4) ELSE '20'||substr(trim(period_from),7,2) END||'-'||substr(trim(period_from),4,2)||'-'||substr(trim(period_from),1,2) ELSE period_from END)) <= date(?)"));
    ex.addBindValue(df);
    ex.addBindValue(dt);
    if (ex.exec() && ex.next()) return ex.value(0).toDouble();
    return 0.0;
}

AdminSalaryTotals collectAdminSalaryTotalsForReports(QSqlDatabase& db, const QString& df, const QString& dt)
{
    AdminSalaryTotals t;

    QSqlQuery parts(db);
    parts.prepare(
        QStringLiteral("SELECT "
                       "COALESCE(SUM(COALESCE(p.purchase_amount, CASE WHEN COALESCE(p.purchase_price,0)>0 THEN COALESCE(p.purchase_price,0)*COALESCE(p.qty,1) ELSE COALESCE(p.amount,0) END)),0), "
                       "COALESCE(SUM(COALESCE(p.amount,0)),0), "
                       "COALESCE(SUM(COALESCE(p.amount,0) - COALESCE(p.purchase_amount, CASE WHEN COALESCE(p.purchase_price,0)>0 THEN COALESCE(p.purchase_price,0)*COALESCE(p.qty,1) ELSE COALESCE(p.amount,0) END)),0) "
                       "FROM router_work_order_parts p "
                       "JOIN router_work_orders wo ON wo.number = p.order_number "
                       "WHERE date(CASE WHEN substr(trim(NULLIF(TRIM(wo.closed_at),'')),3,1)='.' AND substr(trim(NULLIF(TRIM(wo.closed_at),'')),6,1)='.' THEN CASE WHEN substr(trim(NULLIF(TRIM(wo.closed_at),'')),7,4) GLOB '[0-9][0-9][0-9][0-9]' THEN substr(trim(NULLIF(TRIM(wo.closed_at),'')),7,4) ELSE '20'||substr(trim(NULLIF(TRIM(wo.closed_at),'')),7,2) END||'-'||substr(trim(NULLIF(TRIM(wo.closed_at),'')),4,2)||'-'||substr(trim(NULLIF(TRIM(wo.closed_at),'')),1,2) ELSE NULLIF(TRIM(wo.closed_at),'') END) >= date(?) "
                       "AND date(CASE WHEN substr(trim(NULLIF(TRIM(wo.closed_at),'')),3,1)='.' AND substr(trim(NULLIF(TRIM(wo.closed_at),'')),6,1)='.' THEN CASE WHEN substr(trim(NULLIF(TRIM(wo.closed_at),'')),7,4) GLOB '[0-9][0-9][0-9][0-9]' THEN substr(trim(NULLIF(TRIM(wo.closed_at),'')),7,4) ELSE '20'||substr(trim(NULLIF(TRIM(wo.closed_at),'')),7,2) END||'-'||substr(trim(NULLIF(TRIM(wo.closed_at),'')),4,2)||'-'||substr(trim(NULLIF(TRIM(wo.closed_at),'')),1,2) ELSE NULLIF(TRIM(wo.closed_at),'') END) <= date(?) "
                       "AND wo.status='closed'"));
    parts.addBindValue(df);
    parts.addBindValue(dt);
    if (parts.exec() && parts.next()) {
        t.purchaseTotal += parts.value(0).toDouble();
        t.clientTotal += parts.value(1).toDouble();
        t.margin += parts.value(2).toDouble();
    }

    QSqlQuery partsQtyQ(db);
    partsQtyQ.prepare(QStringLiteral("SELECT COALESCE(SUM(COALESCE(p.qty,1)),0) "
                                      "FROM router_work_order_parts p "
                                      "JOIN router_work_orders wo ON wo.number = p.order_number "
                                      "WHERE date(CASE WHEN substr(trim(NULLIF(TRIM(wo.closed_at),'')),3,1)='.' AND substr(trim(NULLIF(TRIM(wo.closed_at),'')),6,1)='.' THEN CASE WHEN substr(trim(NULLIF(TRIM(wo.closed_at),'')),7,4) GLOB '[0-9][0-9][0-9][0-9]' THEN substr(trim(NULLIF(TRIM(wo.closed_at),'')),7,4) ELSE '20'||substr(trim(NULLIF(TRIM(wo.closed_at),'')),7,2) END||'-'||substr(trim(NULLIF(TRIM(wo.closed_at),'')),4,2)||'-'||substr(trim(NULLIF(TRIM(wo.closed_at),'')),1,2) ELSE NULLIF(TRIM(wo.closed_at),'') END) >= date(?) "
                                      "AND date(CASE WHEN substr(trim(NULLIF(TRIM(wo.closed_at),'')),3,1)='.' AND substr(trim(NULLIF(TRIM(wo.closed_at),'')),6,1)='.' THEN CASE WHEN substr(trim(NULLIF(TRIM(wo.closed_at),'')),7,4) GLOB '[0-9][0-9][0-9][0-9]' THEN substr(trim(NULLIF(TRIM(wo.closed_at),'')),7,4) ELSE '20'||substr(trim(NULLIF(TRIM(wo.closed_at),'')),7,2) END||'-'||substr(trim(NULLIF(TRIM(wo.closed_at),'')),4,2)||'-'||substr(trim(NULLIF(TRIM(wo.closed_at),'')),1,2) ELSE NULLIF(TRIM(wo.closed_at),'') END) <= date(?) "
                                      "AND wo.status='closed'"));
    partsQtyQ.addBindValue(df);
    partsQtyQ.addBindValue(dt);
    if (partsQtyQ.exec() && partsQtyQ.next()) t.partsQty = partsQtyQ.value(0).toDouble();

    QSqlQuery ordersQ(db);
    ordersQ.prepare(QStringLiteral("SELECT COUNT(DISTINCT wo.number) FROM router_work_orders wo "
                                   "WHERE date(CASE WHEN substr(trim(NULLIF(TRIM(wo.closed_at),'')),3,1)='.' AND substr(trim(NULLIF(TRIM(wo.closed_at),'')),6,1)='.' THEN CASE WHEN substr(trim(NULLIF(TRIM(wo.closed_at),'')),7,4) GLOB '[0-9][0-9][0-9][0-9]' THEN substr(trim(NULLIF(TRIM(wo.closed_at),'')),7,4) ELSE '20'||substr(trim(NULLIF(TRIM(wo.closed_at),'')),7,2) END||'-'||substr(trim(NULLIF(TRIM(wo.closed_at),'')),4,2)||'-'||substr(trim(NULLIF(TRIM(wo.closed_at),'')),1,2) ELSE NULLIF(TRIM(wo.closed_at),'') END) >= date(?) "
                                   "AND date(CASE WHEN substr(trim(NULLIF(TRIM(wo.closed_at),'')),3,1)='.' AND substr(trim(NULLIF(TRIM(wo.closed_at),'')),6,1)='.' THEN CASE WHEN substr(trim(NULLIF(TRIM(wo.closed_at),'')),7,4) GLOB '[0-9][0-9][0-9][0-9]' THEN substr(trim(NULLIF(TRIM(wo.closed_at),'')),7,4) ELSE '20'||substr(trim(NULLIF(TRIM(wo.closed_at),'')),7,2) END||'-'||substr(trim(NULLIF(TRIM(wo.closed_at),'')),4,2)||'-'||substr(trim(NULLIF(TRIM(wo.closed_at),'')),1,2) ELSE NULLIF(TRIM(wo.closed_at),'') END) <= date(?) "
                                   "AND wo.status='closed'"));
    ordersQ.addBindValue(df);
    ordersQ.addBindValue(dt);
    if (ordersQ.exec() && ordersQ.next()) t.ordersCount = ordersQ.value(0).toInt();

    QSqlQuery outs(db);
    outs.prepare(QStringLiteral("SELECT COALESCE(SUM(company_price),0), COALESCE(SUM(client_price),0), COALESCE(SUM(client_price - company_price),0) "
                                "FROM outsource_jobs "
                                "WHERE date((CASE WHEN substr(trim(sent_date),3,1)='.' AND substr(trim(sent_date),6,1)='.' THEN CASE WHEN substr(trim(sent_date),7,4) GLOB '[0-9][0-9][0-9][0-9]' THEN substr(trim(sent_date),7,4) ELSE '20'||substr(trim(sent_date),7,2) END||'-'||substr(trim(sent_date),4,2)||'-'||substr(trim(sent_date),1,2) ELSE sent_date END)) >= date(?) AND date((CASE WHEN substr(trim(sent_date),3,1)='.' AND substr(trim(sent_date),6,1)='.' THEN CASE WHEN substr(trim(sent_date),7,4) GLOB '[0-9][0-9][0-9][0-9]' THEN substr(trim(sent_date),7,4) ELSE '20'||substr(trim(sent_date),7,2) END||'-'||substr(trim(sent_date),4,2)||'-'||substr(trim(sent_date),1,2) ELSE sent_date END)) <= date(?)"));
    outs.addBindValue(df);
    outs.addBindValue(dt);
    if (outs.exec() && outs.next()) {
        t.purchaseTotal += outs.value(0).toDouble();
        t.clientTotal += outs.value(1).toDouble();
        t.margin += outs.value(2).toDouble();
    }
    return t;
}

} // namespace ReportsDataLoader::LocalSupport
