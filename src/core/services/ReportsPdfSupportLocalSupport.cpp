// P2 architecture migration: implementation module converted from legacy .ipp implementation-fragment decomposition.
// Compiled as a separate P2 implementation module; include-order dependencies are isolated in the module prelude.

#include "core/services/ReportsPdfSupportModulePrelude.h"
#include "core/services/ReportsPdfSupportLocalSupport.h"
#include "core/services/orders/PartMarkupPolicyService.h"

#include "core/util/StoreNameNormalizer.h"
#include "core/services/ReportsDataLoaderLocalSupport.h"
#include <QSqlError>
namespace ReportsPdfSupport::LocalSupport {

struct MasterRowLite {
    QString id;
    QString name;
    QString role;
    bool active = false;
    double adminPercent = 0.0;
};

QString payrollMasterNameKey(QString value)
{
    return value.simplified().toCaseFolded();
}

QString formatPlateForUi(const QString& value)
{
    const QString out = PlateUtils::formatPlateDisplay(value);
    return out.trimmed().isEmpty() ? value.trimmed() : out.trimmed();
}

QString composePlateCarForUi(const QString& plate, const QString& car)
{
    const QString plateText = formatPlateForUi(plate);
    const QString carText = car.trimmed();
    if (plateText.isEmpty()) return carText;
    if (carText.isEmpty()) return plateText;
    return plateText + QString::fromUtf8(" | ") + carText;
}

QList<MasterRow> orderedPersonnelForReports(const QList<MasterRow>& personnel)
{
    QList<MasterRow> masters;
    QList<MasterRow> admins;
    QList<MasterRow> otherRoles;
    for (const auto& person : personnel) {
        if (person.role == QStringLiteral("master")) masters.append(person);
        else if (person.role == QStringLiteral("admin")) admins.append(person);
        else otherRoles.append(person);
    }
    QList<MasterRow> ordered = masters;
    ordered.append(admins);
    ordered.append(otherRoles);
    return ordered;
}

PayrollPercentResolver makePayrollPercentResolver()
{
    PayrollPercentResolver out;
    out.byId = Database::payrollPercents();
    const auto allMasters = Database::masters(false);
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

double resolvePayrollPercent(const MasterRow& master, const PayrollPercentResolver& resolver)
{
    if (master.role == QStringLiteral("admin")) return master.adminPercent;
    if (resolver.byId.contains(master.id)) return resolver.byId.value(master.id, 0.0);
    const QString key = payrollMasterNameKey(master.name);
    if (!key.isEmpty() && resolver.activeByName.contains(key)) return resolver.activeByName.value(key, 0.0);
    return 0.0;
}

QString normalizeAdminSalarySourceKey(QString source)
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

QString adminSalarySourceLabel(const QString& key)
{
    const QString normalizedKey = normalizeAdminSalarySourceKey(key);
    if (normalizedKey == QStringLiteral("client_total")) return QString::fromUtf8("Повна ціна");
    if (normalizedKey == QStringLiteral("purchase_total")) return QString::fromUtf8("Ціна закупки");
    return QString::fromUtf8("Маржа");
}

QMap<QString, QString> loadAdminSalarySources()
{
    QMap<QString, QString> out;
    const auto doc = QJsonDocument::fromJson(Database::getSetting(QStringLiteral("settings.staff.admin_salary_sources"), QStringLiteral("{}")).toString().toUtf8());
    if (!doc.isObject()) return out;
    const auto obj = doc.object();
    for (auto it = obj.begin(); it != obj.end(); ++it) out.insert(it.key(), normalizeAdminSalarySourceKey(it.value().toString(QStringLiteral("margin"))));
    return out;
}

AdminSalaryTotals collectAdminSalaryTotals(const QString& df, const QString& dt, QString* error)
{
    AdminSalaryTotals t;
    const ReportsPdfSupport::PartPeriodTotals partTotals = ReportsPdfSupport::collectClosedPartFinancialTotalsForReports(df, dt,error);
    if(error && !error->isEmpty())return {};
    t.purchaseTotal += partTotals.purchaseTotal;
    t.clientTotal += partTotals.clientTotal;
    t.margin += partTotals.marginTotal;
    t.partsQty = partTotals.qtyTotal;

    QSqlQuery ordersQ(Database::db());
    ordersQ.prepare(QStringLiteral("SELECT COUNT(DISTINCT wo.number) FROM router_work_orders wo WHERE date(CASE WHEN substr(trim(NULLIF(TRIM(wo.closed_at),'')),3,1)='.' AND substr(trim(NULLIF(TRIM(wo.closed_at),'')),6,1)='.' THEN CASE WHEN substr(trim(NULLIF(TRIM(wo.closed_at),'')),7,4) GLOB '[0-9][0-9][0-9][0-9]' THEN substr(trim(NULLIF(TRIM(wo.closed_at),'')),7,4) ELSE '20'||substr(trim(NULLIF(TRIM(wo.closed_at),'')),7,2) END||'-'||substr(trim(NULLIF(TRIM(wo.closed_at),'')),4,2)||'-'||substr(trim(NULLIF(TRIM(wo.closed_at),'')),1,2) ELSE NULLIF(TRIM(wo.closed_at),'') END) >= date(?) AND date(CASE WHEN substr(trim(NULLIF(TRIM(wo.closed_at),'')),3,1)='.' AND substr(trim(NULLIF(TRIM(wo.closed_at),'')),6,1)='.' THEN CASE WHEN substr(trim(NULLIF(TRIM(wo.closed_at),'')),7,4) GLOB '[0-9][0-9][0-9][0-9]' THEN substr(trim(NULLIF(TRIM(wo.closed_at),'')),7,4) ELSE '20'||substr(trim(NULLIF(TRIM(wo.closed_at),'')),7,2) END||'-'||substr(trim(NULLIF(TRIM(wo.closed_at),'')),4,2)||'-'||substr(trim(NULLIF(TRIM(wo.closed_at),'')),1,2) ELSE NULLIF(TRIM(wo.closed_at),'') END) <= date(?) AND wo.status='closed'"));
    ordersQ.addBindValue(df); ordersQ.addBindValue(dt);
    if(!ordersQ.exec() || !ordersQ.next()){if(error)*error=ordersQ.lastError().text();return {};}
    t.ordersCount=ordersQ.value(0).toInt();ordersQ.next();if(ordersQ.lastError().isValid()){if(error)*error=ordersQ.lastError().text();return {};}

    QSqlQuery outs(Database::db());
    outs.prepare(QStringLiteral("SELECT COALESCE(SUM(company_price),0), COALESCE(SUM(client_price),0), COALESCE(SUM(client_price - company_price),0) FROM outsource_jobs WHERE date((CASE WHEN substr(trim(sent_date),3,1)='.' AND substr(trim(sent_date),6,1)='.' THEN CASE WHEN substr(trim(sent_date),7,4) GLOB '[0-9][0-9][0-9][0-9]' THEN substr(trim(sent_date),7,4) ELSE '20'||substr(trim(sent_date),7,2) END||'-'||substr(trim(sent_date),4,2)||'-'||substr(trim(sent_date),1,2) ELSE sent_date END)) >= date(?) AND date((CASE WHEN substr(trim(sent_date),3,1)='.' AND substr(trim(sent_date),6,1)='.' THEN CASE WHEN substr(trim(sent_date),7,4) GLOB '[0-9][0-9][0-9][0-9]' THEN substr(trim(sent_date),7,4) ELSE '20'||substr(trim(sent_date),7,2) END||'-'||substr(trim(sent_date),4,2)||'-'||substr(trim(sent_date),1,2) ELSE sent_date END)) <= date(?)"));
    outs.addBindValue(df); outs.addBindValue(dt);
    if(!outs.exec() || !outs.next()){if(error)*error=outs.lastError().text();return {};}
    {
        t.purchaseTotal += outs.value(0).toDouble();
        t.clientTotal += outs.value(1).toDouble();
        t.margin += outs.value(2).toDouble();
    }
    outs.next();if(outs.lastError().isValid()){if(error)*error=outs.lastError().text();return {};}
    return t;
}

double adminSalaryBaseForSource(const AdminSalaryTotals& totals, const QString& source)
{
    const QString normalizedSource = normalizeAdminSalarySourceKey(source);
    if (normalizedSource == QStringLiteral("client_total")) return totals.clientTotal;
    if (normalizedSource == QStringLiteral("purchase_total")) return totals.purchaseTotal;
    return totals.margin;
}

double periodExpensesTotal(const QString& df, const QString& dt, QString* error)
{
    auto db=Database::db();return ReportsDataLoader::LocalSupport::periodExpensesTotalForReports(db,df,dt,error);
}

int overlapScore(const QString& a, const QString& b, const QStringList& roots)
{
    int score = 0;
    for (const QString& r : roots) { if (a.contains(r) && b.contains(r)) ++score; }
    return score;
}

QString normalizeStoreForPdf(QString store)
{
    const QString normalized = StoreNameNormalizer::canonicalDisplayName(store);
    if (normalized.compare(QString::fromUtf8("Автопро"), Qt::CaseInsensitive) == 0) return QString::fromUtf8("Автопро");
    return normalized.isEmpty() ? QString::fromUtf8("(без магазину)") : normalized;
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
    QString t = text;
    static const QVector<QPair<QRegularExpression, QString>> reps = {
        { QRegularExpression(QStringLiteral(R"(\b0\s*[-—–]?\s*20\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("0W-20") },
        { QRegularExpression(QStringLiteral(R"(\b5\s*[-—–]?\s*20\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("5W-20") },
        { QRegularExpression(QStringLiteral(R"(\b5\s*[-—–]?\s*30\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("5W-30") },
        { QRegularExpression(QStringLiteral(R"(\b5\s*[-—–]?\s*40\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("5W-40") },
        { QRegularExpression(QStringLiteral(R"(\b10\s*[-—–]?\s*40\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("10W-40") }
    };
    for (const auto& rep : reps) t.replace(rep.first, rep.second);
    return t;
}

QString normalizeOilGradeForReport(QString grade)
{
    grade = canonicalizeOilGradesForReport(grade.trimmed()).toUpper();
    return grade;
}

QString detectOilGradeForReport(const QString& text)
{
    static const QRegularExpression re(QStringLiteral(R"(\b(0W-20|5W-20|5W-30|5W-40|10W-40)\b)"), QRegularExpression::CaseInsensitiveOption);
    const auto m = re.match(text);
    return m.hasMatch() ? normalizeOilGradeForReport(m.captured(1)) : QString();
}

bool isMotorOilNameImpl(QString name)
{
    return OilServiceClassifier::isMotorOilProductName(canonicalizeOilGradesForReport(name));
}

QString canonStoreKey(const QString& s)
{
    return StoreNameNormalizer::canonKey(s);
}

QMap<QString,double> loadMarkupMapForReportsImpl(const QString& key)
{
    QMap<QString,double> out;
    const QString raw = Database::getSetting(key, QStringLiteral("{}")).toString();
    const auto doc = QJsonDocument::fromJson(raw.toUtf8());
    if (doc.isObject()) {
        const auto obj = doc.object();
        for (auto it = obj.begin(); it != obj.end(); ++it) {
            const QString k = it.key().trimmed();
            bool ok = false;
            const double v = it.value().toVariant().toDouble(&ok);
            if (!k.isEmpty() && ok) out.insert(k, v);
        }
        return out;
    }
    if (doc.isArray()) {
        const auto arr = doc.array();
        for (const auto& entry : arr) {
            if (!entry.isObject()) continue;
            const QJsonObject obj = entry.toObject();
            const QString k = obj.value(QStringLiteral("store")).toString().trimmed();
            bool ok = false;
            const double v = obj.value(QStringLiteral("pct")).toVariant().toDouble(&ok);
            if (!k.isEmpty() && ok && !out.contains(k)) out.insert(k, v);
        }
    }
    return out;
}

QString motorOilStoreName()
{
    return QString::fromUtf8("Масло · Склад СТО");
}


int payrollHistoryColumnIndex(const QSqlRecord& rec, const QStringList& names)
{
    for (const QString& name : names) {
        const int idx = rec.indexOf(name);
        if (idx >= 0) return idx;
    }
    return -1;
}

PayrollHistoryIssuedTotals collectIssuedPayrollTotals(const QString& df, const QString& dt, QString* error)
{
    PayrollHistoryIssuedTotals totals;

    auto db=Database::db();QSqlQuery present(db);
    if(!present.exec("SELECT 1 FROM sqlite_master WHERE type='table' AND name='payroll_history'")){if(error)*error=present.lastError().text();return {};}
    if(!present.next()){if(present.lastError().isValid() && error)*error=present.lastError().text();return {};}present.finish();
    QSqlQuery schemaQ(db);
    if (!schemaQ.exec(QStringLiteral("SELECT * FROM payroll_history LIMIT 0"))) {
        if(error)*error=schemaQ.lastError().text();return {};
    }

    const QSqlRecord rec = schemaQ.record();
    const int periodFromIdx = payrollHistoryColumnIndex(rec, {QStringLiteral("period_from")});
    const int periodToIdx = payrollHistoryColumnIndex(rec, {QStringLiteral("period_to")});
    const int amountIdx = payrollHistoryColumnIndex(rec, {QStringLiteral("master_amount"), QStringLiteral("amount"), QStringLiteral("salary_amount")});
    const int masterIdIdx = payrollHistoryColumnIndex(rec, {QStringLiteral("master_id"), QStringLiteral("employee_id"), QStringLiteral("person_id"), QStringLiteral("staff_id")});
    const int masterNameIdx = payrollHistoryColumnIndex(rec, {QStringLiteral("master_name"), QStringLiteral("employee_name"), QStringLiteral("person_name"), QStringLiteral("name")});

    if (periodFromIdx < 0 || periodToIdx < 0 || amountIdx < 0) {
        if(error)*error=QStringLiteral("Invalid payroll history schema");return {};
    }

    const QDate targetFrom = ReportsDateUtils::parseFlexibleDate(df);
    const QDate targetTo = ReportsDateUtils::parseFlexibleDate(dt);
    if (!targetFrom.isValid() || !targetTo.isValid()) {
        return totals;
    }

    QSqlQuery dataQ(Database::db());
    if (!dataQ.exec(QStringLiteral("SELECT * FROM payroll_history"))) {
        if(error)*error=dataQ.lastError().text();return {};
    }

    while (dataQ.next()) {
        const QDate rowFrom = ReportsDateUtils::parseFlexibleDate(dataQ.value(periodFromIdx).toString());
        const QDate rowTo = ReportsDateUtils::parseFlexibleDate(dataQ.value(periodToIdx).toString());
        if (!rowFrom.isValid() || !rowTo.isValid()) continue;
        if (rowFrom != targetFrom || rowTo != targetTo) continue;

        bool ok = false;
        const double amount = dataQ.value(amountIdx).toDouble(&ok);
        if (!ok || qFuzzyIsNull(amount)) continue;

        totals.total += amount;

        if (masterIdIdx >= 0) {
            const QString masterId = dataQ.value(masterIdIdx).toString().trimmed();
            if (!masterId.isEmpty()) totals.byMasterId[masterId] += amount;
        }
        if (masterNameIdx >= 0) {
            const QString masterName = payrollMasterNameKey(dataQ.value(masterNameIdx).toString());
            if (!masterName.isEmpty()) totals.byMasterName[masterName] += amount;
        }
    }

    if(dataQ.lastError().isValid()){if(error)*error=dataQ.lastError().text();return {};}
    return totals;
}

double issuedPayrollForMaster(const PayrollHistoryIssuedTotals& totals, const MasterRow& master)
{
    const QString id = master.id.trimmed();
    if (!id.isEmpty() && totals.byMasterId.contains(id)) {
        return totals.byMasterId.value(id, 0.0);
    }
    const QString key = payrollMasterNameKey(master.name);
    if (!key.isEmpty() && totals.byMasterName.contains(key)) {
        return totals.byMasterName.value(key, 0.0);
    }
    return 0.0;
}

int partServiceMatchScore(const QString& serviceTitle, const QString& partName)
{
    const QString s = serviceTitle.toLower();
    const QString p = canonicalizeOilGradesForReport(partName).toLower();
    const QStringList roots = {
        QString::fromUtf8("масл"), QString::fromUtf8("олив"), QStringLiteral("oil"), QString::fromUtf8("фільтр"), QString::fromUtf8("фильтр"),
        QString::fromUtf8("свеч"), QString::fromUtf8("свіч"), QString::fromUtf8("ремень"), QString::fromUtf8("ремін"), QString::fromUtf8("грм"),
        QString::fromUtf8("колод"), QString::fromUtf8("диск"), QString::fromUtf8("аморт"), QString::fromUtf8("стійк"), QString::fromUtf8("стойк")
    };
    return overlapScore(s, p, roots);
}

} // namespace ReportsPdfSupport::LocalSupport
