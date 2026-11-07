#include "core/util/DateFormats.h"
// P2 architecture migration: implementation module converted from legacy .ipp implementation-fragment decomposition.
// Compiled as a separate P2 implementation module; include-order dependencies are isolated in the module prelude.

#include "core/services/ReportsPdfSupportModulePrelude.h"
#include "core/services/ReportsPdfSupportLocalSupport.h"
#include "core/services/orders/PartPricingEngine.h"
#include "core/services/WorkshopInventoryService.h"
#include "core/services/PeriodReportGraph.h"
#include "core/services/PeriodReportRows.h"
#include "core/services/AccessControlService.h"
#include "core/services/ReportsDataLoaderLocalSupport.h"
#include "infrastructure/db/sqlite/DbFacadeSupport.h"
#include <QScopeGuard>
#include <QSqlError>

namespace ReportsPdfSupport {
using namespace LocalSupport;

ReportMarkupSnapshot loadReportMarkupSnapshot()
{
    ReportMarkupSnapshot snapshot;
    // Report calculations must use the same canonical policy as the order editor.
    // Historical UI defaults settings.fin.parts_markup / settings.fin.oil_markup are
    // intentionally not loaded here: an empty or unknown store must resolve to 0%.
    snapshot.policy = PartMarkupPolicyService::loadPolicySnapshot();
    return snapshot;
}

PartFinancials resolvePartFinancialsForReports(const ReportMarkupSnapshot& snapshot,
                                               const QString& partName,
                                               const QString& store,
                                               double qty,
                                               double storedClientAmount,
                                               double storedPurchaseAmount,
                                               double storedPurchasePrice,
                                               double storedMarkupPercent,
                                               bool manualMarkupOverride)
{
    PartFinancials out;
    out.qty = qty > 0.0 ? qty : 1.0;
    if (PartMarkupPolicyService::isCustomerSupplied(store)) return out;

    double purchasePrice = storedPurchasePrice > 0.0
        ? FormatUtils::roundMoneyValue(qMax(0.0, storedPurchasePrice))
        : 0.0;
    if (purchasePrice <= 0.0 && storedPurchaseAmount > 0.0 && out.qty > 0.0) {
        purchasePrice = FormatUtils::roundMoneyValue(qMax(0.0, storedPurchaseAmount) / out.qty);
    }

    PartPricingInput input;
    input.partName = partName;
    input.store = store;
    input.qty = out.qty;
    input.purchasePrice = purchasePrice;
    input.manualMarkupOverride = manualMarkupOverride;
    input.manualMarkupPercent = manualMarkupOverride ? qMax(0.0, storedMarkupPercent) : 0.0;
    input.sourceType = QStringLiteral("report");
    const PartPricingResult pricing = PartPricingEngine::calculate(input, snapshot.policy);

    out.purchasePrice = pricing.purchasePrice;
    out.purchaseAmount = pricing.purchaseAmount;
    out.clientPrice = pricing.clientPrice;
    out.clientAmount = pricing.clientAmount;
    out.markupPercent = pricing.markupPercent;

    const double explicitClientAmount = qRound64(qMax(0.0, storedClientAmount)*100.0)/100.0;
    if (explicitClientAmount > 0.009 || PartMarkupPolicyService::isInventoryStore(store)) {
        out.clientAmount = explicitClientAmount;
        out.clientPrice = qRound64(out.clientAmount / out.qty * 100.0)/100.0;
    }
    if (out.clientPrice <= 0.0 && out.clientAmount > 0.0 && out.qty > 0.0) {
        out.clientPrice = FormatUtils::roundMoneyValue(out.clientAmount / out.qty);
    }
    out.added = FormatUtils::roundMoneyValue(qMax(0.0, out.clientAmount - out.purchaseAmount));
    return out;
}

// Version 3 stores the confirmed row amounts; reports must not reprice them
// from today's policy or round a sub-kopeck unit price before multiplication.
PartFinancials resolvePartFinancialsForReports(const ReportMarkupSnapshot& snapshot,
                                               const QString& partName, const QString& store,
                                               double qty, double storedClientAmount,
                                               double storedPurchaseAmount, double storedPurchasePrice,
                                               double storedMarkupPercent, bool manualMarkupOverride,
                                               int pricingCalcVersion)
{
    if (pricingCalcVersion < 3)
        return resolvePartFinancialsForReports(snapshot, partName, store, qty, storedClientAmount,
            storedPurchaseAmount, storedPurchasePrice, storedMarkupPercent, manualMarkupOverride);
    PartFinancials out;
    out.qty = qty;
    out.purchasePrice = storedPurchasePrice;
    out.purchaseAmount = storedPurchaseAmount;
    out.clientAmount = storedClientAmount;
    out.clientPrice = qty > 0.0 ? storedClientAmount / qty : 0.0;
    out.markupPercent = storedMarkupPercent;
    out.added = FormatUtils::roundMoneyValue(storedClientAmount - storedPurchaseAmount);
    return out;
}

PartPeriodTotals collectClosedPartFinancialTotalsForReports(const QString& df, const QString& dt, QString* error)
{
    PartPeriodTotals totals;
    ReportMarkupSnapshot snapshot;snapshot.policy=PartMarkupPolicyService::loadPolicySnapshot(Database::db(),error);
    if(error && !error->isEmpty())return {};

    QSqlQuery q(Database::db());
    q.prepare(QStringLiteral(
        "SELECT COALESCE(p.name,''), COALESCE(p.store,''), COALESCE(p.qty,1), COALESCE(p.amount,0), "
        "COALESCE(p.purchase_amount, CASE WHEN COALESCE(p.purchase_price,0)>0 THEN COALESCE(p.purchase_price,0)*COALESCE(p.qty,1) ELSE COALESCE(p.amount,0) END), "
        "COALESCE(p.purchase_price,0), COALESCE(p.markup_percent,0), COALESCE(p.manual_markup_override,0), COALESCE(p.pricing_calc_version,1) "
        "FROM router_work_order_parts p "
        "JOIN router_work_orders wo ON ((NULLIF(TRIM(p.order_uid),'') IS NOT NULL AND wo.order_uid=p.order_uid) OR (NULLIF(TRIM(p.order_uid),'') IS NULL AND wo.number=p.order_number)) "
        "WHERE date(CASE WHEN substr(trim(NULLIF(TRIM(wo.closed_at),'')),3,1)='.' AND substr(trim(NULLIF(TRIM(wo.closed_at),'')),6,1)='.' THEN CASE WHEN substr(trim(NULLIF(TRIM(wo.closed_at),'')),7,4) GLOB '[0-9][0-9][0-9][0-9]' THEN substr(trim(NULLIF(TRIM(wo.closed_at),'')),7,4) ELSE '20'||substr(trim(NULLIF(TRIM(wo.closed_at),'')),7,2) END||'-'||substr(trim(NULLIF(TRIM(wo.closed_at),'')),4,2)||'-'||substr(trim(NULLIF(TRIM(wo.closed_at),'')),1,2) ELSE NULLIF(TRIM(wo.closed_at),'') END) >= date(?) "
        "AND date(CASE WHEN substr(trim(NULLIF(TRIM(wo.closed_at),'')),3,1)='.' AND substr(trim(NULLIF(TRIM(wo.closed_at),'')),6,1)='.' THEN CASE WHEN substr(trim(NULLIF(TRIM(wo.closed_at),'')),7,4) GLOB '[0-9][0-9][0-9][0-9]' THEN substr(trim(NULLIF(TRIM(wo.closed_at),'')),7,4) ELSE '20'||substr(trim(NULLIF(TRIM(wo.closed_at),'')),7,2) END||'-'||substr(trim(NULLIF(TRIM(wo.closed_at),'')),4,2)||'-'||substr(trim(NULLIF(TRIM(wo.closed_at),'')),1,2) ELSE NULLIF(TRIM(wo.closed_at),'') END) <= date(?) "
        "AND wo.status='closed'"));
    q.addBindValue(df);
    q.addBindValue(dt);
    if(!q.exec()){if(error)*error=q.lastError().text();return {};}
    {
        while (q.next()) {
            const PartFinancials row = resolvePartFinancialsForReports(snapshot,
                                                                       q.value(0).toString(),
                                                                       q.value(1).toString(),
                                                                       q.value(2).toDouble(),
                                                                       q.value(3).toDouble(),
                                                                       q.value(4).toDouble(),
                                                                       q.value(5).toDouble(),
                                                                       q.value(6).toDouble(),
                                                                       q.value(7).toInt() != 0, q.value(8).toInt());
            totals.qtyTotal += row.qty;
            totals.purchaseTotal += row.purchaseAmount;
            totals.clientTotal += row.clientAmount;
            totals.marginTotal += row.added;
        }
    }
    if(q.lastError().isValid()){if(error)*error=q.lastError().text();return {};}
    return totals;
}

QMap<QString,double> loadMarkupMapForReports(const QString& key)
{
    return loadMarkupMapForReportsImpl(key);
}

bool isMotorOilName(QString name)
{
    return isMotorOilNameImpl(std::move(name));
}

QList<PeriodPdfRow> collectPeriodPdfRows(const QString& df, const QString& dt,
                                         double& partsTotal, double& worksTotal, double& partsMargin,
                                         double& outsourceMargin, double& salaryTotal, double& expensesTotal, QString* error)
{
    QList<PeriodPdfRow> out;
    partsTotal = worksTotal = partsMargin = outsourceMargin = salaryTotal = expensesTotal = 0.0;
    if(error)error->clear();
    auto reject=[&](const QString& reason){partsTotal=worksTotal=partsMargin=outsourceMargin=salaryTotal=expensesTotal=0;if(error)*error=reason;return QList<PeriodPdfRow>{};};
    auto db=QSqlDatabase::database(currentConnectionName(),false);
    if(!db.isOpen())return reject(QStringLiteral("Period report database is not open"));
    if(!AccessControlService::currentUserHasPermission(db,"nav.reports") || AccessControlService::currentOrderScope(db).restricted)return reject(QString::fromUtf8("Недостатньо прав для фінансового звіту за період"));
    if(!db.transaction())return reject(db.lastError().text());
    auto rollback=qScopeGuard([&]{db.rollback();});QString failure;
    ReportMarkupSnapshot markupSnapshot;markupSnapshot.policy=PartMarkupPolicyService::loadPolicySnapshot(db,&failure);
    if(!failure.isEmpty())return reject(failure);
    PeriodReportGraph::Graph graph;
    if(!PeriodReportGraph::read(db,df,dt,&graph,&failure))return reject(failure);
    const auto assembled=PeriodReportRows::build(graph,markupSnapshot);
    out=assembled.rows;partsTotal=assembled.parts;worksTotal=assembled.works;partsMargin=assembled.margin;
    QSqlQuery oq(Database::db());
    oq.prepare(QStringLiteral("SELECT COALESCE(SUM(client_price - company_price),0), COALESCE(SUM(company_price),0) FROM outsource_jobs WHERE date((CASE WHEN substr(trim(sent_date),3,1)='.' AND substr(trim(sent_date),6,1)='.' THEN CASE WHEN substr(trim(sent_date),7,4) GLOB '[0-9][0-9][0-9][0-9]' THEN substr(trim(sent_date),7,4) ELSE '20'||substr(trim(sent_date),7,2) END||'-'||substr(trim(sent_date),4,2)||'-'||substr(trim(sent_date),1,2) ELSE sent_date END)) >= date(?) AND date((CASE WHEN substr(trim(sent_date),3,1)='.' AND substr(trim(sent_date),6,1)='.' THEN CASE WHEN substr(trim(sent_date),7,4) GLOB '[0-9][0-9][0-9][0-9]' THEN substr(trim(sent_date),7,4) ELSE '20'||substr(trim(sent_date),7,2) END||'-'||substr(trim(sent_date),4,2)||'-'||substr(trim(sent_date),1,2) ELSE sent_date END)) <= date(?)"));
    oq.addBindValue(df); oq.addBindValue(dt);
    if (!oq.exec() || !oq.next())return reject(oq.lastError().text());
    {
        outsourceMargin = oq.value(0).toDouble();
    }

    oq.next();if(oq.lastError().isValid())return reject(oq.lastError().text());oq.finish();
    const auto payrollDetails = collectPayrollDetails(df, dt,&failure,false);
    if(!failure.isEmpty())return reject(failure);
    salaryTotal = 0.0;
    for (const auto& master : payrollDetails) {
        salaryTotal += master.accrued;
    }
    expensesTotal = periodExpensesTotal(df, dt,&failure);
    if(!failure.isEmpty())return reject(failure);
    if(!db.commit())return reject(db.lastError().text());rollback.dismiss();
    return out;
}

QMap<QString, QList<PartPdfRow>> collectPartsPdfGroups(const QString& df, const QString& dt)
{
    QMap<QString, QList<PartPdfRow>> groups;
    QSet<QString> stockMaterials;
    for (const auto& material : WorkshopInventoryService::loadMaterials()) stockMaterials.insert(material.key);
    QSet<int> issuedSpares;
    QSqlQuery spareQuery(Database::db());
    if (spareQuery.exec(QStringLiteral("SELECT part_id FROM workshop_spare_movements WHERE part_id IS NOT NULL")))
        while (spareQuery.next()) issuedSpares.insert(spareQuery.value(0).toInt());
    const QDate from = DateFormats::parseDate(df);
    const QDate to = DateFormats::parseDate(dt);
    const ReportMarkupSnapshot markupSnapshot = loadReportMarkupSnapshot();
    QSqlQuery q(Database::db());
    q.prepare(QStringLiteral("SELECT COALESCE(p.store,''), p.ordered_at, substr(COALESCE(NULLIF(wo.closed_at,''), wo.opened_at),1,10), COALESCE(wo.order_number,wo.number,p.order_number), COALESCE(p.name,''), COALESCE(p.part_number,''), COALESCE(p.purchase_amount, CASE WHEN COALESCE(p.purchase_price,0)>0 THEN COALESCE(p.purchase_price,0)*COALESCE(p.qty,1) ELSE COALESCE(p.amount,0) END), COALESCE(p.amount,0), COALESCE(p.qty,1), COALESCE(p.purchase_price,0), COALESCE(p.markup_percent,0), COALESCE(p.manual_markup_override,0), COALESCE(a.invoice_number,''), p.id, COALESCE(p.pricing_calc_version,1) FROM router_work_order_parts p JOIN router_work_orders wo ON ((NULLIF(TRIM(p.order_uid),'') IS NOT NULL AND wo.order_uid=p.order_uid) OR (NULLIF(TRIM(p.order_uid),'') IS NULL AND wo.number=p.order_number)) LEFT JOIN work_order_parts wp ON wp.id = p.id AND COALESCE(wp.order_number,'') = COALESCE(p.order_number,'') AND COALESCE(wp.order_uid,'') = COALESCE(p.order_uid,'') LEFT JOIN work_order_part_attachments a ON a.part_id = wp.id ORDER BY COALESCE(NULLIF(TRIM(p.store),''), '(без магазину)') ASC, p.order_number ASC, p.id ASC"));
    if (q.exec()) while (q.next()) {
        const QDate eff = ReportsDateUtils::effectiveDate(q.value(1).toString(), q.value(2).toString());
        if (!eff.isValid() || eff < from || eff > to) continue;
        PartPdfRow r;
        const QString rawStore = q.value(0).toString().trimmed();
        const QString rawInvoiceNumber = q.value(12).toString().trimmed();
        const QString resolvedStore = resolveStoreForReports(rawStore.isEmpty() ? QString::fromUtf8("(без магазину)") : rawStore, rawInvoiceNumber);
        const QString policyStore = rawStore;
        r.store = resolvedStore;
        r.date = eff.toString(QStringLiteral("dd.MM.yy"));
        r.orderNumber = q.value(3).toString();
        r.name = canonicalizeOilGradesForReport(q.value(4).toString());
        const bool isMotorOil = isMotorOilNameImpl(r.name);
        r.oilGrade = isMotorOil ? detectOilGradeForReport(r.name) : QString();
        if (PartMarkupPolicyService::isInventoryStore(rawStore)
            && !issuedSpares.contains(q.value(13).toInt())
            && stockMaterials.contains(WorkshopInventoryService::normalizedMaterialKey(r.name)))
            r.store = QString::fromUtf8("Склад СТО");
        r.oem = q.value(5).toString();
        const double qty = q.value(8).toDouble();
        const PartFinancials financials = resolvePartFinancialsForReports(markupSnapshot,
                                                                          r.name,
                                                                          policyStore,
                                                                          qty,
                                                                          q.value(7).toDouble(),
                                                                          q.value(6).toDouble(),
                                                                          q.value(9).toDouble(),
                                                                          q.value(10).toDouble(),
                                                                          q.value(11).toInt() != 0, q.value(14).toInt());
        r.purchase = financials.purchaseAmount;
        r.purchaseUnitPrice = financials.purchasePrice > 0.0
            ? financials.purchasePrice
            : (qty > 0.0 ? FormatUtils::roundMoneyValue(financials.purchaseAmount / qty) : financials.purchaseAmount);
        r.invoiceNumber = normalizeInvoiceNumberForReports(rawInvoiceNumber);
        r.added = financials.added;
        r.qty = FormatUtils::smartQty(qty);
        r.qtyValue = qty;
        r.clientPrice = financials.clientAmount;
        groups[r.store].append(r);
    }
    for (auto it = groups.begin(); it != groups.end(); ++it) std::sort(it.value().begin(), it.value().end(), [](const PartPdfRow& a, const PartPdfRow& b){ const QDate da=QDate::fromString(a.date, QStringLiteral("dd.MM.yy")); const QDate db=QDate::fromString(b.date, QStringLiteral("dd.MM.yy")); if (da.isValid() && db.isValid() && da != db) return da < db; bool okA=false, okB=false; const int oa=a.orderNumber.toInt(&okA); const int ob=b.orderNumber.toInt(&okB); if (okA && okB && oa != ob) return oa < ob; return a.name.localeAwareCompare(b.name) < 0; });
    return groups;
}

QMap<QString, QList<OutsourcePdfRow>> collectOutsourceGroups(const QString& df, const QString& dt, int& count, double& repairTotal, double& clientTotal)
{
    QMap<QString, QList<OutsourcePdfRow>> groups; count=0; repairTotal=0.0; clientTotal=0.0;
    const QDate from = DateFormats::parseDate(df);
    const QDate to = DateFormats::parseDate(dt);
    QSqlQuery q(Database::db());
    q.prepare(QStringLiteral("SELECT COALESCE(NULLIF(oj.company_name,''), '(без компанії)'), oj.sent_date, COALESCE(oj.return_date,''), COALESCE(wo.order_number,wo.number,oj.order_number), oj.part_name, oj.company_price, oj.client_price FROM outsource_jobs oj LEFT JOIN router_work_orders wo ON wo.number = oj.order_number ORDER BY oj.company_name ASC, (CASE WHEN substr(trim(oj.sent_date),3,1)='.' AND substr(trim(oj.sent_date),6,1)='.' THEN CASE WHEN substr(trim(oj.sent_date),7,4) GLOB '[0-9][0-9][0-9][0-9]' THEN substr(trim(oj.sent_date),7,4) ELSE '20'||substr(trim(oj.sent_date),7,2) END||'-'||substr(trim(oj.sent_date),4,2)||'-'||substr(trim(oj.sent_date),1,2) ELSE oj.sent_date END) ASC, oj.id ASC"));
    if (q.exec()) while (q.next()) {
        const QDate eff = ReportsDateUtils::effectiveDate(q.value(1).toString());
        if (!eff.isValid() || eff < from || eff > to) continue;
        const QDate ret = ReportsDateUtils::effectiveDate(q.value(2).toString());
        OutsourcePdfRow r; r.company=q.value(0).toString(); r.date=eff.toString(QStringLiteral("dd.MM.yy")); r.returnDate=ret.isValid()?ret.toString(QStringLiteral("dd.MM.yy")):QString(); r.orderNumber=q.value(3).toString(); r.partName=q.value(4).toString(); r.repairCost=q.value(5).toDouble(); r.clientPrice=q.value(6).toDouble();
        groups[r.company].append(r); ++count; repairTotal += r.repairCost; clientTotal += r.clientPrice;
    }
    return groups;
}

QList<PayrollMasterPdf> collectPayrollDetails(const QString& df, const QString& dt, QString* error, bool includeWorkText)
{
    QList<PayrollMasterPdf> mastersOut;
    QString failure;auto db=QSqlDatabase::database(currentConnectionName(),false);
    const auto fail=[&](){if(error)*error=failure;return QList<PayrollMasterPdf>{};};
    if(error)error->clear();
    if(!db.isOpen()){failure=QStringLiteral("Payroll PDF database is not open");return fail();}
    const auto masters=ReportsDataLoader::LocalSupport::orderedPersonnelForReports(ReportsDataLoader::LocalSupport::loadMastersForPeriodForReports(db,df,dt,&failure));if(!failure.isEmpty())return fail();
    const auto checkedResolver=ReportsDataLoader::LocalSupport::makePayrollPercentResolverForReports(db,&failure);if(!failure.isEmpty())return fail();
    const PayrollPercentResolver payrollResolver{checkedResolver.byId,checkedResolver.activeByName};
    const auto adminSources=ReportsDataLoader::LocalSupport::loadAdminSalarySourcesForReports(db,&failure);if(!failure.isEmpty())return fail();
    const auto adminTotals=collectAdminSalaryTotals(df,dt,&failure);if(!failure.isEmpty())return fail();
    const auto issuedTotals=includeWorkText?collectIssuedPayrollTotals(df,dt,&failure):PayrollHistoryIssuedTotals{};if(!failure.isEmpty())return fail();
    int activeMasterCount=0;for(const auto& m:masters)if(m.role=="master")++activeMasterCount;
    const double sharedExpensesPerMaster=activeMasterCount>0?periodExpensesTotal(df,dt,&failure)/activeMasterCount:0;
    if(!failure.isEmpty())return fail();
    for (const auto& m : masters) {
        PayrollMasterPdf pm; pm.masterId = m.id; pm.masterName = m.role == QStringLiteral("admin") ? QString::fromUtf8("Адмін: %1").arg(m.name) : m.name; pm.percent = resolvePayrollPercent(m, payrollResolver);
        if (m.role == QStringLiteral("admin")) {
            const QString source = adminSources.value(m.id, QStringLiteral("margin"));
            PayrollOrderRow row; row.dateStart=ReportsDateUtils::dmy(df); row.dateEnd=ReportsDateUtils::dmy(dt); row.orderNumber=QStringLiteral("-"); row.car=adminSalarySourceLabel(source); row.works=QString::fromUtf8("Зарплата адміна за вибраним джерелом"); row.amount=adminSalaryBaseForSource(adminTotals, source); row.masterAmount=FormatUtils::roundMoneyValue(row.amount * pm.percent / 100.0);
            pm.orders.append(row); pm.ordersCount=1; pm.worksTotal=row.amount; pm.accrued=row.masterAmount; pm.issued = issuedPayrollForMaster(issuedTotals, m); pm.toIssue = qMax(0.0, pm.accrued - pm.issued);
            if (pm.accrued > 0.0 || pm.toIssue > 0.0) mastersOut.append(pm);
            continue;
        }
        QSqlQuery ordersQ(Database::db());
        ordersQ.prepare(QStringLiteral("SELECT wo.number, COALESCE(wo.order_number,wo.number), substr(wo.opened_at,1,10), substr(NULLIF(TRIM(wo.closed_at),''),1,10), wo.plate, wo.car, wo.engine_type, wo.engine_volume, COALESCE(SUM(COALESCE(s.amount,0) * COALESCE(sm.share_percent,0) / 100.0), 0), COALESCE(wo.discount,0), COALESCE(wo.works_sum,0), COALESCE(wo.discount_mode,'legacy') FROM router_work_orders wo JOIN router_work_order_services s ON s.order_number = wo.number JOIN router_service_masters sm ON sm.service_id = s.id LEFT JOIN payroll_order_adjustments adj ON adj.master_id = sm.master_id AND adj.order_number = wo.number WHERE wo.status='closed' AND sm.master_id=? AND COALESCE(adj.absent,0)=0 AND COALESCE(adj.deleted,0)=0 AND NOT EXISTS (SELECT 1 FROM outsource_jobs oj WHERE oj.service_id = s.id) AND date(CASE WHEN substr(trim(NULLIF(TRIM(wo.closed_at),'')),3,1)='.' AND substr(trim(NULLIF(TRIM(wo.closed_at),'')),6,1)='.' THEN CASE WHEN substr(trim(NULLIF(TRIM(wo.closed_at),'')),7,4) GLOB '[0-9][0-9][0-9][0-9]' THEN substr(trim(NULLIF(TRIM(wo.closed_at),'')),7,4) ELSE '20'||substr(trim(NULLIF(TRIM(wo.closed_at),'')),7,2) END||'-'||substr(trim(NULLIF(TRIM(wo.closed_at),'')),4,2)||'-'||substr(trim(NULLIF(TRIM(wo.closed_at),'')),1,2) ELSE NULLIF(TRIM(wo.closed_at),'') END) >= date(?) AND date(CASE WHEN substr(trim(NULLIF(TRIM(wo.closed_at),'')),3,1)='.' AND substr(trim(NULLIF(TRIM(wo.closed_at),'')),6,1)='.' THEN CASE WHEN substr(trim(NULLIF(TRIM(wo.closed_at),'')),7,4) GLOB '[0-9][0-9][0-9][0-9]' THEN substr(trim(NULLIF(TRIM(wo.closed_at),'')),7,4) ELSE '20'||substr(trim(NULLIF(TRIM(wo.closed_at),'')),7,2) END||'-'||substr(trim(NULLIF(TRIM(wo.closed_at),'')),4,2)||'-'||substr(trim(NULLIF(TRIM(wo.closed_at),'')),1,2) ELSE NULLIF(TRIM(wo.closed_at),'') END) <= date(?) GROUP BY wo.number, wo.order_number, wo.opened_at, wo.closed_at, wo.plate, wo.car, wo.engine_type, wo.engine_volume, wo.discount, wo.works_sum ORDER BY wo.number ASC"));
        ordersQ.addBindValue(m.id); ordersQ.addBindValue(df); ordersQ.addBindValue(dt);
        if(!ordersQ.exec()){failure=ordersQ.lastError().text();return fail();}
        while (ordersQ.next()) {
            PayrollOrderRow row;
            row.internalOrderNumber = ordersQ.value(0).toInt();
            const QString orderNumber = ordersQ.value(1).toString();
            row.dateStart = ReportsDateUtils::dmy(ordersQ.value(2).toString());
            row.dateEnd = ReportsDateUtils::dmy(ordersQ.value(3).toString());
            row.orderNumber = orderNumber;
            row.car = composePlateCarForUi(ordersQ.value(4).toString(), ordersQ.value(5).toString());
            row.engine = QStringLiteral("%1\n%2").arg(ordersQ.value(6).toString(), ordersQ.value(7).toString());
            row.amount = FormatUtils::roundMoneyValue(ordersQ.value(8).toDouble() * (1.0 - FormatUtils::discountPercent(ordersQ.value(9).toDouble(), ordersQ.value(11).toString(), ordersQ.value(10).toDouble()) / 100.0));
            row.masterAmount = FormatUtils::roundMoneyValue(row.amount * pm.percent / 100.0);
            if(includeWorkText)row.works = ReportsSyncSupport::payrollWorksTextForMaster(QString::number(row.internalOrderNumber), m.id, pm.percent,&failure);
            if(!failure.isEmpty())return fail();
            pm.orders.append(row);
            pm.ordersCount += 1;
            pm.worksTotal += row.amount;
        }
        if(ordersQ.lastError().isValid()){failure=ordersQ.lastError().text();return fail();}
        pm.accrued=FormatUtils::roundMoneyValue(pm.worksTotal * pm.percent / 100.0); pm.sharedExpenses=sharedExpensesPerMaster; pm.accrued=qMax(0.0, pm.accrued - sharedExpensesPerMaster);
        pm.issued = issuedPayrollForMaster(issuedTotals, m);
        pm.toIssue = qMax(0.0, pm.accrued - pm.issued);
        if (pm.accrued > 0.0 || pm.toIssue > 0.0) mastersOut.append(pm);
    }
    return mastersOut;
}

} // namespace ReportsPdfSupport
