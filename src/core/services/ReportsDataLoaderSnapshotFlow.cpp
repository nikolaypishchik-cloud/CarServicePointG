#include "core/services/orders/ClientReceiptReader.h"
#include "core/util/DateFormats.h"
// P2 architecture migration: implementation module converted from legacy .ipp implementation-fragment decomposition.
// Compiled as a separate P2 implementation module; include-order dependencies are isolated in the module prelude.

#include "core/services/ReportsDataLoaderModulePrelude.h"
#include "core/services/HistoryQueryService.h"
#include "core/services/ReportsDataLoaderLocalSupport.h"
#include "core/services/orders/PartMarkupPolicyService.h"
#include "core/services/orders/PartPricingEngine.h"
#include "core/services/WorkshopInventoryService.h"
#include "core/services/WorkshopSpareStockService.h"
#include "core/services/PartsQueryService.h"
#include "core/util/OilServiceClassifier.h"
#include <QSqlError>
#include "core/services/InvoiceRegistryService.h"
#include "core/services/PartsReportQuery.h"

namespace ReportsDataLoader {
using namespace LocalSupport;

QString currentDatabasePath()
{
    return Database::db().databaseName();
}


PeriodSnapshot buildPeriodSnapshot(const QString& dbPath, const QString& df, const QString& dt)
{
    PeriodSnapshot snapshot;
    const QString connName = makeReportsWorkerConnectionName(QStringLiteral("period"));
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
        db.setDatabaseName(dbPath);
        db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        if (!db.open()) {
            snapshot.stats = { QString::fromUtf8("Помилка відкриття БД для звіту") };
        } else {
            QSqlQuery q(db);
            q.prepare(
                "SELECT substr(COALESCE(NULLIF(wo.closed_at,''), wo.opened_at),1,10), COALESCE(wo.order_number,wo.number), wo.number, wo.car, "
                "COALESCE(NULLIF(wo.master_name_closed,''),m.name,''), "
                "COALESCE(wo.works_sum,0), COALESCE(wo.parts_sum,0) "
                "FROM router_work_orders wo "
                "LEFT JOIN masters m ON m.id = wo.master_id "
                "WHERE date(CASE WHEN substr(trim(COALESCE(NULLIF(wo.closed_at,''), wo.opened_at)),3,1)='.' AND substr(trim(COALESCE(NULLIF(wo.closed_at,''), wo.opened_at)),6,1)='.' THEN CASE WHEN substr(trim(COALESCE(NULLIF(wo.closed_at,''), wo.opened_at)),7,4) GLOB '[0-9][0-9][0-9][0-9]' THEN substr(trim(COALESCE(NULLIF(wo.closed_at,''), wo.opened_at)),7,4) ELSE '20'||substr(trim(COALESCE(NULLIF(wo.closed_at,''), wo.opened_at)),7,2) END||'-'||substr(trim(COALESCE(NULLIF(wo.closed_at,''), wo.opened_at)),4,2)||'-'||substr(trim(COALESCE(NULLIF(wo.closed_at,''), wo.opened_at)),1,2) ELSE COALESCE(NULLIF(wo.closed_at,''), wo.opened_at) END) >= date(?) "
                "AND date(CASE WHEN substr(trim(COALESCE(NULLIF(wo.closed_at,''), wo.opened_at)),3,1)='.' AND substr(trim(COALESCE(NULLIF(wo.closed_at,''), wo.opened_at)),6,1)='.' THEN CASE WHEN substr(trim(COALESCE(NULLIF(wo.closed_at,''), wo.opened_at)),7,4) GLOB '[0-9][0-9][0-9][0-9]' THEN substr(trim(COALESCE(NULLIF(wo.closed_at,''), wo.opened_at)),7,4) ELSE '20'||substr(trim(COALESCE(NULLIF(wo.closed_at,''), wo.opened_at)),7,2) END||'-'||substr(trim(COALESCE(NULLIF(wo.closed_at,''), wo.opened_at)),4,2)||'-'||substr(trim(COALESCE(NULLIF(wo.closed_at,''), wo.opened_at)),1,2) ELSE COALESCE(NULLIF(wo.closed_at,''), wo.opened_at) END) <= date(?) "
                "ORDER BY date(CASE WHEN substr(trim(COALESCE(NULLIF(wo.closed_at,''), wo.opened_at)),3,1)='.' AND substr(trim(COALESCE(NULLIF(wo.closed_at,''), wo.opened_at)),6,1)='.' THEN CASE WHEN substr(trim(COALESCE(NULLIF(wo.closed_at,''), wo.opened_at)),7,4) GLOB '[0-9][0-9][0-9][0-9]' THEN substr(trim(COALESCE(NULLIF(wo.closed_at,''), wo.opened_at)),7,4) ELSE '20'||substr(trim(COALESCE(NULLIF(wo.closed_at,''), wo.opened_at)),7,2) END||'-'||substr(trim(COALESCE(NULLIF(wo.closed_at,''), wo.opened_at)),4,2)||'-'||substr(trim(COALESCE(NULLIF(wo.closed_at,''), wo.opened_at)),1,2) ELSE COALESCE(NULLIF(wo.closed_at,''), wo.opened_at) END) DESC, wo.number DESC");
            q.addBindValue(df);
            q.addBindValue(dt);

            int count = 0;
            double works = 0.0;
            double parts = 0.0;

            const bool executed = q.exec();
            if (executed) {
                while (q.next()) {
                    const double ws = q.value(5).toDouble();
                    const double ps = q.value(6).toDouble();
                    PeriodSnapshotRow row;
                    row.date = ReportsDateUtils::dmy(q.value(0).toString());
                    row.orderNumber = q.value(1).toString();
                    row.internalOrderNumber = q.value(2).toInt();
                    row.car = q.value(3).toString();
                    row.master = q.value(4).toString().trimmed();
                    row.works = money0(ws);
                    row.parts = money0(ps);
                    row.total = money0(ws + ps);
                    snapshot.rows.append(row);
                    ++count;
                    works += ws;
                    parts += ps;
                }
            }

            if (!executed || q.lastError().isValid()) {
                snapshot.rows.clear();
                snapshot.stats = {QString::fromUtf8("Помилка читання звіту: ") + q.lastError().text()};
            } else snapshot.stats = {
                QString::fromUtf8("Нарядів:  %1").arg(count),
                QString::fromUtf8("Роботи:  %1  грн.").arg(money0(works)),
                QString::fromUtf8("Запчастини:  %1  грн.").arg(money0(parts)),
                QString::fromUtf8("Разом:  %1  грн.").arg(money0(works + parts))
            };
        }
    }
    SqliteConnectionUtils::safeCloseAndRemoveDatabase(connName);
    return snapshot;
}

OrdersSnapshot buildOrdersSnapshot(const QString& dbPath, const QString& df, const QString& dt, const QString& filter)
{
    OrdersSnapshot snapshot;
    const QString connName = makeReportsWorkerConnectionName(QStringLiteral("orders"));
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
        db.setDatabaseName(dbPath);
        db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        if (!db.open()) {
            snapshot.stats = {QString::fromUtf8("Помилка відкриття БД для звіту по нарядах")};
            snapshot.clientPaymentError=db.lastError().text();
        } else if (!db.transaction()) {
            snapshot.clientPaymentError=db.lastError().text();
            snapshot.stats={QStringLiteral("Не вдалося почати читання звіту: ")+snapshot.clientPaymentError};
        } else {
            const auto readSnapshot=[&] {
            QString searchPredicate;
            if (!filter.trimmed().isEmpty()) {
                HistoryQueryParams params{QDate(1900,1,1), QDate(9999,12,31), filter, true};
                QSqlQuery searchIndex(db);
                params.useSearchIndex = searchIndex.exec("SELECT 1 FROM sqlite_master WHERE name='order_search_v1'") && searchIndex.next();
                searchPredicate = QStringLiteral(" AND wo.number IN (SELECT __internal_number FROM (%1)) ")
                    .arg(HistoryQueryService::buildHistorySql(db,params));
            }
            QSqlQuery q(db);
            q.prepare(QStringLiteral(
                "WITH matched AS MATERIALIZED (SELECT wo.* FROM router_work_orders wo "
                "WHERE date(CASE WHEN substr(trim(COALESCE(NULLIF(wo.closed_at,''),wo.opened_at)),3,1)='.' AND substr(trim(COALESCE(NULLIF(wo.closed_at,''),wo.opened_at)),6,1)='.' THEN CASE WHEN substr(trim(COALESCE(NULLIF(wo.closed_at,''),wo.opened_at)),7,4) GLOB '[0-9][0-9][0-9][0-9]' THEN substr(trim(COALESCE(NULLIF(wo.closed_at,''),wo.opened_at)),7,4) ELSE '20'||substr(trim(COALESCE(NULLIF(wo.closed_at,''),wo.opened_at)),7,2) END||'-'||substr(trim(COALESCE(NULLIF(wo.closed_at,''),wo.opened_at)),4,2)||'-'||substr(trim(COALESCE(NULLIF(wo.closed_at,''),wo.opened_at)),1,2) ELSE COALESCE(NULLIF(wo.closed_at,''),wo.opened_at) END) BETWEEN date(?) AND date(?) %1), "
                "uid_totals AS (SELECT p.order_uid,SUM(COALESCE(p.purchase_amount,p.purchase_price*p.qty,0)) purchase, "
                "SUM(COALESCE(p.amount,p.price*p.qty,0)) sale FROM router_work_order_parts p "
                "WHERE p.order_uid IN (SELECT order_uid FROM matched WHERE TRIM(COALESCE(order_uid,''))<>'') GROUP BY p.order_uid), "
                "number_totals AS (SELECT p.order_number,SUM(COALESCE(p.purchase_amount,p.purchase_price*p.qty,0)) purchase, "
                "SUM(COALESCE(p.amount,p.price*p.qty,0)) sale FROM router_work_order_parts p "
                "WHERE p.order_number IN (SELECT number FROM matched) AND TRIM(COALESCE(p.order_uid,''))='' GROUP BY p.order_number) "
                "SELECT wo.number, COALESCE(wo.order_number,wo.number), "
                "substr(COALESCE(NULLIF(wo.closed_at,''),wo.opened_at),1,10), "
                "COALESCE(wo.plate,''), COALESCE(wo.car,''), COALESCE(wo.client,''), "
                "COALESCE(NULLIF(wo.master_name_closed,''),m.name,''), "
                "COALESCE(u.purchase,0)+COALESCE(n.purchase,0), "
                "COALESCE(u.sale,0)+COALESCE(n.sale,0), wo.status "
                "FROM matched wo "
                "LEFT JOIN masters m ON m.id=wo.master_id "
                "LEFT JOIN uid_totals u ON u.order_uid=wo.order_uid "
                "LEFT JOIN number_totals n ON n.order_number=wo.number "
                "ORDER BY date(CASE WHEN substr(trim(COALESCE(NULLIF(wo.closed_at,''),wo.opened_at)),3,1)='.' AND substr(trim(COALESCE(NULLIF(wo.closed_at,''),wo.opened_at)),6,1)='.' THEN CASE WHEN substr(trim(COALESCE(NULLIF(wo.closed_at,''),wo.opened_at)),7,4) GLOB '[0-9][0-9][0-9][0-9]' THEN substr(trim(COALESCE(NULLIF(wo.closed_at,''),wo.opened_at)),7,4) ELSE '20'||substr(trim(COALESCE(NULLIF(wo.closed_at,''),wo.opened_at)),7,2) END||'-'||substr(trim(COALESCE(NULLIF(wo.closed_at,''),wo.opened_at)),4,2)||'-'||substr(trim(COALESCE(NULLIF(wo.closed_at,''),wo.opened_at)),1,2) ELSE COALESCE(NULLIF(wo.closed_at,''),wo.opened_at) END) DESC, wo.number DESC").arg(searchPredicate));
            q.addBindValue(df);
            q.addBindValue(dt);
            double purchaseTotal = 0.0;
            double saleTotal = 0.0;
            if (!q.exec()) {
                snapshot.stats = {QString::fromUtf8("Не вдалося завантажити наряди: %1").arg(q.lastError().text())};
                snapshot.clientPaymentError=q.lastError().text();
            } else {
                while (q.next()) {
                    OrdersSnapshotRow row;
                    row.closed = q.value(9).toString() == QStringLiteral("closed");
                    row.internalOrderNumber = q.value(0).toInt();
                    row.orderNumber = q.value(1).toString();
                    row.date = ReportsDateUtils::dmy(q.value(2).toString());
                    row.car = QStringList{parsePlateForUi(q.value(3).toString()), q.value(4).toString()}.join(QStringLiteral(" | "));
                    row.client = q.value(5).toString();
                    row.master = q.value(6).toString();
                    row.purchaseTotal = FormatUtils::money(q.value(7).toDouble(), 2);
                    row.saleTotal = FormatUtils::money(q.value(8).toDouble(), 2);
                    purchaseTotal += q.value(7).toDouble();
                    saleTotal += q.value(8).toDouble();
                    snapshot.rows.append(row);
                }
                if(q.lastError().isValid()) {
                    snapshot.rows.clear();
                    snapshot.clientPaymentError=q.lastError().text();
                    snapshot.stats={QStringLiteral("Читання звіту перервано: ")+snapshot.clientPaymentError};
                    return;
                }
                q.finish();
                snapshot.stats = {
                    QString::fromUtf8("Нарядів: %1").arg(snapshot.rows.size()),
                    QString::fromUtf8("Закупка: %1 грн").arg(FormatUtils::money(purchaseTotal, 2)),
                    QString::fromUtf8("Продаж: %1 грн").arg(FormatUtils::money(saleTotal, 2))
                };
                // No selected orders means there are no receipts or invoices to enrich.
                if (snapshot.rows.isEmpty()) return;
                QString invoiceError;
                // Client receipts never derive from supplier payments or prices.
                QHash<int,QString> orderUids;
                QSet<QString> selectedUids;
                QSqlQuery identities(db);
                if(!identities.exec("SELECT number,order_uid FROM router_work_orders")) snapshot.clientPaymentError=identities.lastError().text();
                else {
                    while(identities.next()) {
                        const int number=identities.value(0).toInt();
                        if(orderUids.contains(number)) snapshot.clientPaymentError=QStringLiteral("Неоднозначний номер наряду.");
                        orderUids.insert(number,identities.value(1).toString());
                    }
                    if(identities.lastError().isValid()) snapshot.clientPaymentError=identities.lastError().text();
                }
                for(const auto& row:snapshot.rows) {
                    const auto uid=orderUids.value(row.internalOrderNumber);
                    if(uid.trimmed().isEmpty() || selectedUids.contains(uid)) snapshot.clientPaymentError=QStringLiteral("Відсутній або неоднозначний UID наряду.");
                    selectedUids.insert(uid);
                }
                if(snapshot.clientPaymentError.isEmpty()) {
                    const auto receipts=ClientReceiptReader::read(db,selectedUids);
                    if(!receipts.ok()) snapshot.clientPaymentError=receipts.error;
                    else if(!receipts.available) snapshot.clientPaymentError=QStringLiteral("Журнал клієнтських оплат відсутній. Історичні надходження невідомі.");
                    else for(auto& row:snapshot.rows) row.clientReceivedCents=receipts.centsByOrder.value(orderUids.value(row.internalOrderNumber));
                }
                const auto invoices=InvoiceRegistryService::load(db,&invoiceError);
                QString paymentError;
                QHash<int, qint64> unpaidByOrder;
                for (const auto& debt : PurchasePaymentService::debts(db, &paymentError))
                    if (debt.source == "part") unpaidByOrder[debt.orderNumber] += debt.remainingCents();
                for(auto& row:snapshot.rows) {
                    qint64 cost=0;bool allPaid=true;QStringList dates;
                    for(const auto& invoice:invoices)if(invoice.orders.contains(row.internalOrderNumber)) {
                        row.invoiceKeys.append(invoice.key);row.invoiceNumbers.append(invoice.number);
                        QSet<int> issued;
                        for(const auto& slice:invoice.distribution)if(slice.orderNumber==row.internalOrderNumber && slice.partId) {
                            cost+=slice.total;issued.insert(slice.partId);
                        }
                        QDate due;
                        for(const auto& part:invoice.parts) {
                            if(part.source=="part" && part.orderNumber==row.internalOrderNumber && !issued.contains(int(part.id)))cost+=part.totalCents;
                            if(part.dueDate.isValid() && (!due.isValid() || part.dueDate<due))due=part.dueDate;
                        }
                        if(!due.isValid())due=invoice.date.addDays(7);
                        if(due.isValid())dates.append(due.toString("dd.MM.yy"));
                        allPaid=allPaid && invoice.paid();
                    }
                    dates.removeDuplicates();row.paymentDue=dates.join(", ");
                    row.invoiceTotal=FormatUtils::money(cost/100.0,2);
                    if (row.invoiceKeys.isEmpty()) row.invoiceTotal = row.purchaseTotal;
                    row.paymentStatus=(!invoiceError.isEmpty() || !paymentError.isEmpty())?QString::fromUtf8("Помилка звірки")
                        :row.invoiceKeys.isEmpty()?(unpaidByOrder.value(row.internalOrderNumber)>0?QString::fromUtf8("Не сплачено повністю"):QString::fromUtf8("Сплачено"))
                        :(allPaid && unpaidByOrder.value(row.internalOrderNumber)==0)?QString::fromUtf8("Сплачено"):QString::fromUtf8("Не сплачено повністю");
                }
            }
            };
            readSnapshot();
            // Read-only transaction: all statements see the same committed state.
            if(!db.rollback()) {
                snapshot.rows.clear();
                snapshot.clientPaymentError=db.lastError().text();
                snapshot.stats={QStringLiteral("Не вдалося завершити читання звіту: ")+snapshot.clientPaymentError};
            }
        }
    }
    SqliteConnectionUtils::safeCloseAndRemoveDatabase(connName);
    return snapshot;
}

PartsSnapshot buildPartsSnapshot(const QString& dbPath, const QString& df, const QString& dt,
    double defaultParts, double defaultOil, const QMap<QString,double>& partsMap, const QMap<QString,double>& oilMap)
{
    Q_UNUSED(defaultParts);
    Q_UNUSED(defaultOil);
    Q_UNUSED(partsMap);
    Q_UNUSED(oilMap);
    // The signature is preserved for UI/backward compatibility, but effective %
    // is resolved only through PartMarkupPolicyService inside the worker DB context.
    PartsSnapshot snapshot;
    const QString connName = makeReportsWorkerConnectionName(QStringLiteral("parts"));
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
        if (!PartsReportQuery::openSnapshot(db,dbPath,&snapshot.error)) {
        } else {
            const auto read=[&] {
            const PartMarkupPolicyService::PolicySnapshot policySnapshot = PartMarkupPolicyService::loadPolicySnapshot(db,&snapshot.error);
            if(!snapshot.error.isEmpty())return;
            struct PartReportRow {
                QDate date;
                int orderInternalNumber = 0;
                int orderSortVisible = 0;
                QString orderDisplay;
                int rowId = 0;
                QString store;
                QString partName;
                QString partNumber;
                QString invoiceNumber;
                QString invoicePath;
                QString invoiceDate;
                QString plate;
                QString car;
                QString vin;
                double purchase = 0.0;
                double qty = 0.0;
                double client = 0.0;
                double net = 0.0;
                bool isOil = false;
            };

            QList<PartReportRow> rows;
            QMap<QString, QString> stockUnits;
            for (const auto& material : WorkshopInventoryService::loadMaterials(db,&snapshot.error,false)) stockUnits.insert(material.key, material.unit);
            if(!snapshot.error.isEmpty())return;
            QSet<int> issuedSpares;
            QSqlQuery spareQuery(db);
            if(!spareQuery.exec("SELECT part_id FROM workshop_spare_movements WHERE operation='issue'")){snapshot.error=spareQuery.lastError().text();return;}
            while(spareQuery.next())issuedSpares.insert(spareQuery.value(0).toInt());
            if(spareQuery.lastError().isValid()){snapshot.error=spareQuery.lastError().text();return;}
            const QDate from = DateFormats::parseDate(df);
            const QDate to = DateFormats::parseDate(dt);
            QSqlQuery q(db);
            if(!PartsReportQuery::execute(db,&q,&snapshot.error))return;

            {
                while (q.next()) {
                    PartReportRow row;
                    row.rowId = q.value(0).toInt();
                    row.date = ReportsDateUtils::effectiveDate(q.value(1).toString(), q.value(2).toString());
                    if (!row.date.isValid() || row.date < from || row.date > to) continue;
                    row.orderInternalNumber = q.value(3).toInt();
                    row.orderSortVisible = q.value(4).toInt();
                    row.orderDisplay = q.value(4).toString();
                    const QString rawStore = q.value(5).toString().trimmed();
                    const QString rawInvoiceNumber = q.value(17).toString().trimmed();
                    row.invoiceNumber = rawInvoiceNumber;
                    row.invoicePath = q.value(18).toString().trimmed();
                    row.invoiceDate = q.value(19).toString().trimmed();
                    row.store = resolveStoreForReports(rawStore.isEmpty() ? QString::fromUtf8("(без магазину)") : rawStore, rawInvoiceNumber);
                    row.partName = q.value(6).toString();
                    row.partNumber = q.value(7).toString();
                    row.plate = parsePlateForUi(q.value(8).toString());
                    row.car = q.value(9).toString();
                    row.vin = q.value(10).toString();
                    row.purchase = q.value(11).toDouble();
                    row.qty = q.value(12).toDouble();
                    const double storedClientAmount = qMax(0.0, q.value(13).toDouble());
                    const double purchasePrice = q.value(14).toDouble() > 0.0 ? q.value(14).toDouble() : (row.qty > 0.0 ? row.purchase / row.qty : 0.0);
                    PartPricingInput pricingInput;
                    pricingInput.partName = row.partName;
                    pricingInput.store = rawStore;
                    pricingInput.qty = row.qty > 0.0 ? row.qty : 1.0;
                    pricingInput.purchasePrice = purchasePrice;
                    pricingInput.manualMarkupOverride = q.value(16).toInt() != 0;
                    pricingInput.manualMarkupPercent = pricingInput.manualMarkupOverride ? qMax(0.0, q.value(15).toDouble()) : 0.0;
                    pricingInput.sourceType = QStringLiteral("report");
                    const PartPricingResult pricing = PartPricingEngine::calculate(pricingInput, policySnapshot);
                    row.client = q.value(20).toInt() >= 3 || storedClientAmount > 0.009 || PartMarkupPolicyService::isInventoryStore(row.store)
                        ? storedClientAmount : pricing.clientAmount;
                    row.net = q.value(20).toInt() >= 3 ? FormatUtils::roundMoneyValue(row.client - row.purchase)
                        : qMax(0.0, row.client - row.purchase);
                    row.isOil = PartMarkupPolicyService::isInventoryStore(rawStore) && !issuedSpares.contains(row.rowId)
                        && stockUnits.contains(WorkshopInventoryService::normalizedMaterialKey(row.partName));
                    rows.append(row);
                }
            }

            if(q.lastError().isValid()){snapshot.error=q.lastError().text();return;}
            std::sort(rows.begin(), rows.end(), [](const PartReportRow& a, const PartReportRow& b) {
                if (a.isOil != b.isOil) return !a.isOil;
                if (a.store != b.store) {
                    if (a.date != b.date) return a.date > b.date;
                    if (a.orderSortVisible != b.orderSortVisible) return a.orderSortVisible > b.orderSortVisible;
                if (a.orderInternalNumber != b.orderInternalNumber) return a.orderInternalNumber > b.orderInternalNumber;
                    const int sc = QString::compare(a.store, b.store, Qt::CaseInsensitive);
                    if (sc != 0) return sc < 0;
                }
                if (a.date != b.date) return a.date > b.date;
                if (a.orderSortVisible != b.orderSortVisible) return a.orderSortVisible > b.orderSortVisible;
                if (a.orderInternalNumber != b.orderInternalNumber) return a.orderInternalNumber > b.orderInternalNumber;
                return a.rowId > b.rowId;
            });

            QString lastStore;
            int count = 0;
            double totalNet = 0.0;
            double nonOilPurchase = 0.0;
            double nonOilQty = 0.0;
            double oilTotalQty = 0.0;
            double oilTotalClient = 0.0;
            double oilTotalPurchase = 0.0;
            QMap<QString, double> oilQtyByGrade;
            QMap<QString, double> oilClientByGrade;

            for (const auto& row : rows) {
                const QString article = row.isOil ? QString::fromUtf8("Склад СТО") : row.store;
                if (article != lastStore) {
                    lastStore = article;
                    PartsSnapshotRow header;
                    header.isStoreHeader = true;
                    header.store = article;
                    snapshot.rows.append(header);
                }

                PartsSnapshotRow out;
                out.date = row.date.toString(QStringLiteral("dd.MM.yy"));
                out.orderNumber = row.orderDisplay;
                out.internalOrderNumber = row.orderInternalNumber;
                out.partName = row.partName;
                out.store = article;
                out.invoiceNumber = row.invoiceNumber;
                out.invoicePath = row.invoicePath;
                out.invoiceDate = row.invoiceDate;
                out.purchase = FormatUtils::formatNumberSpaces(row.purchase, 0);
                out.net = FormatUtils::formatNumberSpaces(row.net, 0);
                out.qty = FormatUtils::smartQty(row.qty);
                out.client = FormatUtils::formatNumberSpaces(row.client, 0);
                QStringList tip;
                if (!row.partNumber.trimmed().isEmpty()) tip << QString::fromUtf8("OEM: %1").arg(row.partNumber.trimmed());
                if (!row.plate.trimmed().isEmpty()) tip << QString::fromUtf8("Номер авто: %1").arg(parsePlateForUi(row.plate));
                if (!row.car.trimmed().isEmpty()) tip << QString::fromUtf8("Авто: %1").arg(row.car.trimmed());
                if (!row.vin.trimmed().isEmpty()) tip << QString::fromUtf8("VIN: %1").arg(row.vin.trimmed());
                out.tooltip = tip.join(QStringLiteral("\n"));
                snapshot.rows.append(out);

                if (row.isOil) {
                    const QString oilKey = row.partName + " (" + stockUnits.value(WorkshopInventoryService::normalizedMaterialKey(row.partName)) + ")";
                    oilTotalQty += row.qty;
                    oilTotalClient += row.client;
                    oilTotalPurchase += row.purchase;
                    oilQtyByGrade[oilKey] += row.qty;
                    oilClientByGrade[oilKey] += row.client;
                }

                ++count;
                totalNet += row.isOil ? row.client - row.purchase : row.net;
                if (!row.isOil) {
                    nonOilPurchase += row.purchase;
                    nonOilQty += row.qty;
                }
            }

            if (count == 0) {
                snapshot.rows.clear();
            }

            if (oilTotalQty > 0.0) {
                QStringList gradeKeys = oilQtyByGrade.keys();
                std::sort(gradeKeys.begin(), gradeKeys.end(), [](const QString& a, const QString& b) {
                    if (a == QString::fromUtf8("Без в'язкості")) return false;
                    if (b == QString::fromUtf8("Без в'язкості")) return true;
                    return oilGradeLessThan(a, b);
                });
                QStringList gradeRows;
                for (const QString& gradeKey : gradeKeys) {
                    gradeRows << QString::fromUtf8(
                        R"(<tr>
                             <td style="padding:0 14px 0 0; text-align:left; white-space:nowrap;">%1</td>
                             <td style="padding:0 14px 0 0; text-align:left; white-space:nowrap;">%2</td>
                             <td style="padding:0 14px 0 0; text-align:left; white-space:nowrap;"></td>
                             <td style="padding:0; text-align:left; white-space:nowrap;">%3 грн</td>
                           </tr>)")
                        .arg(gradeKey.toHtmlEscaped(),
                             FormatUtils::smartQty(oilQtyByGrade.value(gradeKey)).toHtmlEscaped(),
                             money0(oilClientByGrade.value(gradeKey)).toHtmlEscaped());
                }
                snapshot.oilHtml = QString::fromUtf8(
                    R"(<div style="color:#ffffff; font-size:13pt; font-weight:700; white-space:nowrap;">Склад СТО</div>
                       <table cellspacing="0" cellpadding="0" style="color:rgba(245,247,250,0.96); font-size:10.5pt; margin-top:2px; line-height:1.35;">
                         <tr>
                           <td style="padding:0 14px 0 0; text-align:left; white-space:nowrap;"><span style="font-weight:600;">Всього:</span></td>
                           <td style="padding:0 14px 0 0; text-align:left; white-space:nowrap;">%1 матеріалів</td>
                           <td style="padding:0 14px 0 0; text-align:left; white-space:nowrap;"><span style="font-weight:600;">На суму:</span></td>
                           <td style="padding:0; text-align:left; white-space:nowrap;">%2 грн</td>
                         </tr>
                         %3
                         <tr><td colspan="4">Собівартість: %4 грн · Продаж: %2 грн · Маржа: %5 грн</td></tr>
                       </table>)")
                    .arg(QString::number(gradeKeys.size()), money0(oilTotalClient), gradeRows.join(QString()),
                         money0(oilTotalPurchase), money0(oilTotalClient - oilTotalPurchase));
            }

            const auto stock = WorkshopInventoryService::buildSnapshot(db,QDate::fromString(df,Qt::ISODate),QDate::fromString(dt,Qt::ISODate),false);
            if(!stock.errorText.isEmpty()){snapshot.error=stock.errorText;return;}
            QMap<QString,double> sold, received;
            for (const auto& movement : stock.consumptions) sold[movement.materialKey] += movement.quantity;
            for (const auto& purchase : stock.purchases) received[purchase.materialKey] += purchase.quantity;
            QMap<QString,QString> materialGroups;
            for (const auto& balance : stock.balanceRows) {
                const QString group=OilServiceClassifier::isMotorOilProductName(balance.materialName)?QString::fromUtf8("Олива моторна")
                    :balance.materialName.contains(QString::fromUtf8("антифриз"),Qt::CaseInsensitive)?QString::fromUtf8("Антифриз"):QString::fromUtf8("Інші матеріали");
                materialGroups[group] += QString::fromUtf8("<tr><td style='padding-right:6px;'>%1 (%2)</td><td style='padding-right:6px;'><span style='font-size:10pt;'>Продаж:</span> %3 %2</td><td><span style='font-size:10pt;'>Залишок:</span> %4 %2</td></tr>")
                    .arg(balance.materialName.toHtmlEscaped(),balance.unit.toHtmlEscaped(),
                         FormatUtils::smartQty(sold.value(balance.materialKey)),FormatUtils::smartQty(balance.balanceQuantity));
            }
            double ordered=0,delivered=0,installed=0,issued=0,onHand=0;
            QString partsError;
            for (const auto& part : PartsQueryService::loadRows(db,{},QDate::fromString(df,Qt::ISODate),QDate::fromString(dt,Qt::ISODate),&partsError)) {
                if ((PartMarkupPolicyService::isInventoryStore(part.store) && stockUnits.contains(WorkshopInventoryService::normalizedMaterialKey(part.name))) || PartMarkupPolicyService::isCustomerSupplied(part.store)) continue;
                issued += part.qty;
                if (part.installed) installed += part.qty;
                else if (part.deliveryDate.isValid()) delivered += part.qty;
                else ordered += part.qty;
            }
            if(!partsError.isEmpty()){snapshot.error=partsError;return;}
            QString stockError;
            for (const auto& reserve : WorkshopSpareStockService::orders(db,&stockError,false)) onHand+=qMax(0.0,reserve.received-reserve.issued);
            if(!stockError.isEmpty()){snapshot.error=stockError;return;}
            snapshot.stats = {QString::fromUtf8("Запчастини:"),
                QString::fromUtf8("На складі зараз: %1 од.").arg(FormatUtils::smartQty(onHand)),
                QString::fromUtf8("Замовлено: %1 од. · Доставлено: %2 од.").arg(FormatUtils::smartQty(ordered),FormatUtils::smartQty(delivered)),
                QString::fromUtf8("Видано в наряди: %1 од. · Встановлено: %2 од.").arg(FormatUtils::smartQty(issued),FormatUtils::smartQty(installed))};
            const auto oil=QString::fromUtf8("Олива моторна");
            const auto antifreeze=QString::fromUtf8("Антифриз");
            const auto other=QString::fromUtf8("Інші матеріали");
            const QString noData=QString::fromUtf8("<tr><td>Немає даних</td></tr>");
            QString coolantRows;
            for(const auto& balance:stock.balanceRows) {
                if(!balance.materialName.contains(antifreeze,Qt::CaseInsensitive))continue;
                coolantRows+=QString::fromUtf8("<tr><td>%1</td></tr><tr><td><span style='font-size:10pt;'>Продаж:</span> %2 %4</td></tr><tr><td><span style='font-size:10pt;'>Залишок:</span> %3 %4</td></tr>")
                    .arg(balance.materialName.toHtmlEscaped(),FormatUtils::smartQty(sold.value(balance.materialKey)),
                         FormatUtils::smartQty(balance.balanceQuantity),balance.unit.toHtmlEscaped());
            }
            snapshot.oilHtml=QString::fromUtf8(
                "<table width='100%' cellspacing='0' cellpadding='0'>"
                "<tr><td width='35%' style='border-bottom:1px solid REPORT_RULE_COLOR;'>%1</td>"
                "<td width='REPORT_COLUMN_GAP'></td><td colspan='2' style='border-bottom:1px solid REPORT_RULE_COLOR;'>Запчастини:</td></tr>"
                "<tr><td valign='top' style='padding-top:REPORT_RULE_GAPpx;'><table cellspacing='0' cellpadding='0'>%2</table>%3</td>"
                "<td width='REPORT_COLUMN_GAP'></td>"
                "<td width='20%' valign='top' style='padding-top:REPORT_RULE_GAPpx;'><u>%4</u><table cellspacing='0' cellpadding='0'>%5</table></td>"
                "<td width='45%' valign='top' style='padding-top:REPORT_RULE_GAPpx;'><table cellspacing='0' cellpadding='0'>"
                "<tr><td style='font-size:10pt;padding-right:REPORT_VALUE_GAPpx;'>На складі:</td><td>%6 од.</td>"
                "<td width='REPORT_STOCK_GAP'></td><td style='font-size:10pt;padding-right:REPORT_VALUE_GAPpx;'>Видано в наряди:</td><td>%9 од.</td></tr>"
                "<tr><td style='font-size:10pt;padding-right:REPORT_VALUE_GAPpx;'>Замовлено:</td><td>%7 од.</td>"
                "<td></td><td style='font-size:10pt;padding-right:REPORT_VALUE_GAPpx;'>Встановлено:</td><td>%10 од.</td></tr>"
                "<tr><td style='font-size:10pt;padding-right:REPORT_VALUE_GAPpx;'>Доставлено:</td><td>%8 од.</td><td></td><td></td><td></td></tr></table></td></tr></table>")
                .arg(oil,materialGroups.value(oil,noData),
                     materialGroups.contains(other)?"<br/>"+other+"<table>"+materialGroups.value(other)+"</table>":QString(),
                     antifreeze,coolantRows.isEmpty()?noData:coolantRows,
                     FormatUtils::smartQty(onHand),FormatUtils::smartQty(ordered),FormatUtils::smartQty(delivered),
                     FormatUtils::smartQty(issued),FormatUtils::smartQty(installed));
            if(!stock.errorText.isEmpty())snapshot.oilHtml=stock.errorText.toHtmlEscaped();
            if(!partsError.isEmpty())snapshot.oilHtml+="<br/>"+partsError.toHtmlEscaped();
            if(!stockError.isEmpty())snapshot.oilHtml+="<br/>"+stockError.toHtmlEscaped();
            if (!partsError.isEmpty()) snapshot.stats={partsError};
            if (!stockError.isEmpty()) snapshot.stats={stockError};
            QStringList oilNames,oilRows,coolantTileRows;
            const auto quantity=[](double value){return "<b>"+FormatUtils::smartQty(value)+"</b>";};
            for(const auto& balance:stock.balanceRows){
                const QString amounts=QString::fromUtf8("<table cellspacing='0' cellpadding='0'><tr><td align='left'>Продаж:</td><td width='REPORT_TILE_VALUE_GAP'></td><td align='left'>%1 %3</td></tr><tr><td align='left'>Залишок:</td><td width='REPORT_TILE_VALUE_GAP'></td><td align='left'>%2 %3</td></tr></table>")
                    .arg(quantity(sold.value(balance.materialKey)),quantity(balance.balanceQuantity),balance.unit.toHtmlEscaped());
                if(OilServiceClassifier::isMotorOilProductName(balance.materialName)){
                    QString displayName=balance.materialName;
                    displayName.replace(QRegularExpression(QString::fromUtf8("\\bмасло\\b"),QRegularExpression::CaseInsensitiveOption|QRegularExpression::UseUnicodePropertiesOption),QString::fromUtf8("Олива"));
                    oilNames.append(displayName);
                    oilRows.append(amounts);
                }else if(balance.materialName.contains(antifreeze,Qt::CaseInsensitive)){
                    coolantTileRows.append(QString::fromUtf8("<table width='100%' cellspacing='0' cellpadding='0'><tr><td align='center'>Придбано:</td><td align='center'>Видано:</td><td align='center'>Залишок:</td></tr><tr><td align='center'>%1 %4</td><td align='center'>%2 %4</td><td align='center'>%3 %4</td></tr></table>")
                        .arg(quantity(received.value(balance.materialKey)),
                             quantity(sold.value(balance.materialKey)),
                             quantity(balance.balanceQuantity), balance.unit.toHtmlEscaped()));
                }
            }
            if(oilNames.size()>1)for(int i=0;i<oilRows.size();++i)oilRows[i]=oilNames[i].toHtmlEscaped()+"<br/>"+oilRows[i];
            const QString empty=QString::fromUtf8("Немає даних");
            snapshot.tileTitles={oilNames.size()==1?oilNames.first():oil,antifreeze,
                QString::fromUtf8("Запчастини · постачання"),QString::fromUtf8("Запчастини · використання")};
            snapshot.tileBodies={oilRows.isEmpty()?empty:oilRows.join("<br/>"),coolantTileRows.isEmpty()?empty:coolantTileRows.join("<br/>"),
                QString::fromUtf8("<table cellspacing='0' cellpadding='0'><tr><td align='left'>На складі:</td><td width='REPORT_TILE_VALUE_GAP'></td><td align='left'>%1 од.</td></tr><tr><td align='left'>Замовлено:</td><td width='REPORT_TILE_VALUE_GAP'></td><td align='left'>%2 од.</td></tr></table>").arg(quantity(onHand),quantity(ordered)),
                QString::fromUtf8("<table cellspacing='0' cellpadding='0'><tr><td align='left'>Видано в наряди:</td><td width='REPORT_TILE_VALUE_GAP'></td><td align='left'>%1 од.</td></tr><tr><td align='left'>Встановлено:</td><td width='REPORT_TILE_VALUE_GAP'></td><td align='left'>%2 од.</td></tr></table>").arg(quantity(issued),quantity(installed))};
            if(!stock.errorText.isEmpty())for(int i=0;i<2;++i)snapshot.tileBodies[i]=stock.errorText.toHtmlEscaped();
            if(!partsError.isEmpty()||!stockError.isEmpty())for(int i=2;i<4;++i)snapshot.tileBodies[i]=(partsError+" "+stockError).trimmed().toHtmlEscaped();
            };read();
            if(snapshot.error.isEmpty() && !db.commit())snapshot.error=db.lastError().text();
            if(!snapshot.error.isEmpty())db.rollback();
        }
    }
    if(!snapshot.error.isEmpty()){const auto error=snapshot.error;snapshot={};snapshot.error=error;snapshot.stats={QString::fromUtf8("Помилка читання запчастин: ")+error};}
    SqliteConnectionUtils::safeCloseAndRemoveDatabase(connName);
    return snapshot;
}

OutsourceSnapshot buildOutsourceSnapshot(const QString& dbPath, const QString& df, const QString& dt)
{
    OutsourceSnapshot snapshot;
    const QString connName = makeReportsWorkerConnectionName(QStringLiteral("outsource"));
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
        db.setDatabaseName(dbPath);
        db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        if (!db.open()) {
            snapshot.stats = { QString::fromUtf8("Помилка відкриття БД для аутсорсу") };
            snapshot.error = db.lastError().text();
        } else {
            const QDate from = DateFormats::parseDate(df);
            const QDate to = DateFormats::parseDate(dt);
            QSqlQuery q(db);
            q.prepare(
                "SELECT oj.service_id, oj.sent_date, COALESCE(oj.return_date,''), COALESCE(NULLIF(oj.company_name,''), '(без компанії)'), "
                "oj.order_number, COALESCE(wo.order_number,wo.number,oj.order_number), oj.part_name, oj.company_price, oj.client_price "
                "FROM outsource_jobs oj "
                "LEFT JOIN router_work_orders wo ON wo.number = oj.order_number "
                "ORDER BY (CASE WHEN substr(trim(oj.sent_date),3,1)='.' AND substr(trim(oj.sent_date),6,1)='.' THEN CASE WHEN substr(trim(oj.sent_date),7,4) GLOB '[0-9][0-9][0-9][0-9]' THEN substr(trim(oj.sent_date),7,4) ELSE '20'||substr(trim(oj.sent_date),7,2) END||'-'||substr(trim(oj.sent_date),4,2)||'-'||substr(trim(oj.sent_date),1,2) ELSE oj.sent_date END) DESC, oj.order_number DESC, oj.id DESC");

            int count = 0;
            double repair = 0.0;
            double client = 0.0;
            const bool executed = q.exec();
            if (executed) {
                while (q.next()) {
                    const QDate eff = ReportsDateUtils::effectiveDate(q.value(1).toString());
                    if (!eff.isValid() || eff < from || eff > to) continue;
                    const QDate ret = ReportsDateUtils::effectiveDate(q.value(2).toString());
                    const double repairCost = q.value(7).toDouble();
                    const double clientPrice = q.value(8).toDouble();
                    OutsourceSnapshotRow row;
                    row.serviceId = q.value(0).toInt();
                    row.internalOrderNumber = q.value(4).toInt();
                    row.sentDate = eff.toString(QStringLiteral("dd.MM.yy"));
                    row.returnDate = ret.isValid() ? ret.toString(QStringLiteral("dd.MM.yy")) : QString();
                    row.company = q.value(3).toString();
                    row.orderNumber = q.value(5).toString();
                    row.partName = q.value(6).toString();
                    row.repairCost = money0(repairCost);
                    row.clientPrice = money0(clientPrice);
                    snapshot.rows.append(row);
                    ++count;
                    repair += repairCost;
                    client += clientPrice;
                }
            }

            if (!executed || q.lastError().isValid()) {
                snapshot.rows.clear();
                snapshot.error = q.lastError().text();
                snapshot.stats = {QStringLiteral("Помилка читання звіту про аутсорс: ") + snapshot.error};
            } else snapshot.stats = {
                QString::fromUtf8("Відправлено на ремонт:  %1  од.").arg(count),
                QString::fromUtf8("На суму:  %1  грн.").arg(money0(repair)),
                QString::fromUtf8("Маржа:  %1  грн.").arg(money0(client - repair))
            };
        }
    }
    SqliteConnectionUtils::safeCloseAndRemoveDatabase(connName);
    return snapshot;
}

PayrollSnapshot buildPayrollSnapshot(const QString& dbPath, const QString& df, const QString& dt)
{
    PayrollSnapshot snapshot;
    const QString connName = makeReportsWorkerConnectionName(QStringLiteral("payroll"));
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
        db.setDatabaseName(dbPath);
        db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        if (!db.open() || !db.transaction()) {
            snapshot.error=db.lastError().text();
        } else {
            const auto read=[&] {
            const auto masters = orderedPersonnelForReports(loadMastersForPeriodForReports(db, df, dt,&snapshot.error));
            if(!snapshot.error.isEmpty())return;
            const PayrollPercentResolver payrollResolver = makePayrollPercentResolverForReports(db,&snapshot.error);
            if(!snapshot.error.isEmpty())return;
            const auto adminSources = loadAdminSalarySourcesForReports(db,&snapshot.error);
            if(!snapshot.error.isEmpty())return;
            const AdminSalaryTotals adminTotals = collectAdminSalaryTotalsForReports(db, df, dt,&snapshot.error);

            if(!snapshot.error.isEmpty())return;
            int activeMasterCount = 0;
            for (const auto& m : masters) if (m.role == QStringLiteral("master")) ++activeMasterCount;
            const double sharedExpensesPerMaster = activeMasterCount > 0 ? periodExpensesTotalForReports(db, df, dt,&snapshot.error) / activeMasterCount : 0.0;

            if(!snapshot.error.isEmpty())return;
            double total = 0.0;
            for (const auto& m : masters) {
                PayrollSnapshotRow row;
                row.masterId = m.id;
                row.masterName = m.role == QStringLiteral("admin") ? QString::fromUtf8("Адмін: %1").arg(m.name) : m.name;
                const double percent = [&] {
                    if (m.role == QStringLiteral("admin")) return m.adminPercent;
                    if (payrollResolver.byId.contains(m.id)) return payrollResolver.byId.value(m.id, 0.0);
                    const QString key = payrollMasterNameKey(m.name);
                    if (!key.isEmpty() && payrollResolver.activeByName.contains(key)) {
                        return payrollResolver.activeByName.value(key, 0.0);
                    }
                    return 0.0;
                }();

                int ordersCount = 0;
                double baseSum = 0.0;
                double accrued = 0.0;

                if (m.role == QStringLiteral("admin")) {
                    const QString source = adminSources.value(m.id, QStringLiteral("margin"));
                    ordersCount = 1;
                    baseSum = adminSalaryBaseForSource(adminTotals, source);
                    accrued = FormatUtils::roundMoneyValue(baseSum * percent / 100.0);
                    if (!(m.active || baseSum > 0.0)) continue;
                } else {
                    QSqlQuery q(db);
                    q.prepare(QStringLiteral(
                        "SELECT wo.number, COALESCE(SUM(COALESCE(s.amount,0) * COALESCE(sm.share_percent,0) / 100.0), 0) AS share_raw, "
                        "COALESCE(wo.discount,0), COALESCE(wo.works_sum,0), COALESCE(wo.discount_mode,'legacy') "
                        "FROM router_work_orders wo "
                        "JOIN router_work_order_services s ON s.order_number = wo.number "
                        "JOIN router_service_masters sm ON sm.service_id = s.id "
                        "LEFT JOIN payroll_order_adjustments adj ON adj.master_id = sm.master_id AND adj.order_number = wo.number "
                        "WHERE wo.status='closed' AND sm.master_id=? "
                        "AND COALESCE(adj.absent,0)=0 AND COALESCE(adj.deleted,0)=0 "
                        "AND NOT EXISTS (SELECT 1 FROM outsource_jobs oj WHERE oj.service_id = s.id) "
                        "AND date(CASE WHEN substr(trim(NULLIF(TRIM(wo.closed_at),'')),3,1)='.' AND substr(trim(NULLIF(TRIM(wo.closed_at),'')),6,1)='.' THEN CASE WHEN substr(trim(NULLIF(TRIM(wo.closed_at),'')),7,4) GLOB '[0-9][0-9][0-9][0-9]' THEN substr(trim(NULLIF(TRIM(wo.closed_at),'')),7,4) ELSE '20'||substr(trim(NULLIF(TRIM(wo.closed_at),'')),7,2) END||'-'||substr(trim(NULLIF(TRIM(wo.closed_at),'')),4,2)||'-'||substr(trim(NULLIF(TRIM(wo.closed_at),'')),1,2) ELSE NULLIF(TRIM(wo.closed_at),'') END) >= date(?) "
                        "AND date(CASE WHEN substr(trim(NULLIF(TRIM(wo.closed_at),'')),3,1)='.' AND substr(trim(NULLIF(TRIM(wo.closed_at),'')),6,1)='.' THEN CASE WHEN substr(trim(NULLIF(TRIM(wo.closed_at),'')),7,4) GLOB '[0-9][0-9][0-9][0-9]' THEN substr(trim(NULLIF(TRIM(wo.closed_at),'')),7,4) ELSE '20'||substr(trim(NULLIF(TRIM(wo.closed_at),'')),7,2) END||'-'||substr(trim(NULLIF(TRIM(wo.closed_at),'')),4,2)||'-'||substr(trim(NULLIF(TRIM(wo.closed_at),'')),1,2) ELSE NULLIF(TRIM(wo.closed_at),'') END) <= date(?) "
                        "GROUP BY wo.number "
                        "ORDER BY wo.number ASC"));
                    q.addBindValue(m.id);
                    q.addBindValue(df);
                    q.addBindValue(dt);
                    if (!q.exec()){snapshot.error=q.lastError().text();return;}
                    {
                        while (q.next()) {
                            ordersCount += 1;
                            const double shareRaw = q.value(1).toDouble();
                            const double discountRaw = q.value(2).toDouble();
                            const double worksTotal = q.value(3).toDouble();
                            const double discountPct = FormatUtils::discountPercent(discountRaw, q.value(4).toString(), worksTotal);
                            baseSum += qMax(0.0, FormatUtils::roundMoneyValue(shareRaw * (1.0 - discountPct / 100.0)));
                        }
                    }
                    if(q.lastError().isValid()){snapshot.error=q.lastError().text();return;}
                    accrued = FormatUtils::roundMoneyValue(baseSum * percent / 100.0);
                    accrued = qMax(0.0, accrued - sharedExpensesPerMaster);
                    if (!(m.active || ordersCount > 0 || baseSum > 0.0)) continue;
                }

                row.ordersCount = QString::number(ordersCount);
                row.baseSum = money0(baseSum);
                row.percent = QString::number(percent, 'f', 2);
                row.accrued = money0(accrued);
                row.accruedValue = accrued;
                snapshot.rows.append(row);
                total += accrued;
            }

            snapshot.stats = {
                QString::fromUtf8("Співробітників:  %1").arg(snapshot.rows.size()),
                QString::fromUtf8("Нараховано:  %1  грн.").arg(money0(total))
            };
            };read();
            if(snapshot.error.isEmpty() && !db.commit())snapshot.error=db.lastError().text();
            if(!snapshot.error.isEmpty())db.rollback();
        }
    }
    if(!snapshot.error.isEmpty()){snapshot.rows.clear();snapshot.stats={QString::fromUtf8("Помилка читання зарплати: ")+snapshot.error};}
    SqliteConnectionUtils::safeCloseAndRemoveDatabase(connName);
    return snapshot;
}

static ExpensesSnapshot readExpensePeriod(QSqlDatabase& db, const QString& df, const QString& dt)
{
    ExpensesSnapshot snapshot;
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT elec_prev, elec_curr, elec_tariff, cleaning, cleaning_note, sw_opendatabot, software_note, heating, heating_note, amortization, amortization_note, rent, rent_note, other_amount, other_note FROM expenses WHERE %1=? AND %2=? ORDER BY id DESC LIMIT 1").arg(DateFormats::isoSql("period_from"), DateFormats::isoSql("period_to")));
    q.addBindValue(df);
    q.addBindValue(dt);
    const double savedTariff = reportsWorkerSetting(db, QStringLiteral("reports.expenses.electricity_tariff"), 0.0, &snapshot.error).toDouble();
    if (!snapshot.error.isEmpty()) return snapshot;
    if (!q.exec()) {
        snapshot.error = q.lastError().text();
    } else if (q.next()) {
        snapshot.foundRow = true;
        snapshot.elecPrev = q.value(0).toDouble();
        snapshot.elecCurr = q.value(1).toDouble();
        const double rowTariff = q.value(2).toDouble();
        snapshot.elecTariff = rowTariff > 0.0 ? rowTariff : savedTariff;
        snapshot.cleaning = q.value(3).toDouble();
        snapshot.cleaningNote = q.value(4).toString();
        snapshot.software = q.value(5).toDouble();
        snapshot.softwareNote = q.value(6).toString();
        snapshot.heating = q.value(7).toDouble();
        snapshot.heatingNote = q.value(8).toString();
        snapshot.amortization = q.value(9).toDouble();
        snapshot.amortizationNote = q.value(10).toString();
        snapshot.rent = q.value(11).toDouble();
        snapshot.rentNote = q.value(12).toString();
        snapshot.other = q.value(13).toDouble();
        snapshot.otherNote = q.value(14).toString();
    } else if (q.lastError().isValid()) {
        snapshot.error = q.lastError().text();
    } else {
        snapshot.elecTariff = savedTariff;
        QSqlQuery prev(db);
        prev.prepare(QStringLiteral("SELECT COALESCE(elec_curr,0) FROM expenses WHERE date((CASE WHEN substr(trim(period_to),3,1)='.' AND substr(trim(period_to),6,1)='.' THEN CASE WHEN substr(trim(period_to),7,4) GLOB '[0-9][0-9][0-9][0-9]' THEN substr(trim(period_to),7,4) ELSE '20'||substr(trim(period_to),7,2) END||'-'||substr(trim(period_to),4,2)||'-'||substr(trim(period_to),1,2) ELSE period_to END)) < date(?) ORDER BY date((CASE WHEN substr(trim(period_to),3,1)='.' AND substr(trim(period_to),6,1)='.' THEN CASE WHEN substr(trim(period_to),7,4) GLOB '[0-9][0-9][0-9][0-9]' THEN substr(trim(period_to),7,4) ELSE '20'||substr(trim(period_to),7,2) END||'-'||substr(trim(period_to),4,2)||'-'||substr(trim(period_to),1,2) ELSE period_to END)) DESC, id DESC LIMIT 1"));
        prev.addBindValue(df);
        if (prev.exec() && prev.next()) snapshot.elecPrev = prev.value(0).toDouble();
        if (prev.lastError().isValid()) snapshot.error = prev.lastError().text();
    }
    return snapshot;
}

ExpensesSnapshot buildExpensesSnapshot(const QString& dbPath, const QString& df, const QString& dt)
{
    ExpensesSnapshot snapshot;
    const QString connName = makeReportsWorkerConnectionName(QStringLiteral("expenses"));
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
        db.setDatabaseName(dbPath);
        db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        if (!db.open() || !db.transaction()) {
            snapshot.error = db.lastError().text();
        } else {
            snapshot = readExpensePeriod(db, df, dt);
            if (snapshot.error.isEmpty() && !db.commit()) snapshot.error = db.lastError().text();
            if (!snapshot.error.isEmpty()) db.rollback();
        }
        db.close();
    }
    SqliteConnectionUtils::safeCloseAndRemoveDatabase(connName);
    return snapshot;
}

TotalExpensesSnapshot buildTotalExpensesSnapshot(const QString& dbPath, const QString& df, const QString& dt)
{
    TotalExpensesSnapshot snapshot;
    const QString connName = makeReportsWorkerConnectionName(QStringLiteral("total_expenses"));
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
        db.setDatabaseName(dbPath);
        db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        const QDate from = DateFormats::parseDate(df);
        const QDate to = DateFormats::parseDate(dt);
        if (!from.isValid() || !to.isValid() || from > to) {
            snapshot.error = QString::fromUtf8("Початкова дата має бути не пізніше кінцевої.");
        } else if (!db.open() || !db.transaction()) {
            snapshot.error = db.lastError().text();
        } else {
            QSqlQuery q(db);
            q.prepare(
                "SELECT period_from,period_to,"
                "COALESCE(elec_cost,((COALESCE(elec_curr,0)-COALESCE(elec_prev,0))*COALESCE(elec_tariff,0))),"
                "COALESCE(cleaning,0),COALESCE(sw_opendatabot,0)+COALESCE(sw_tecalliance,0),"
                "COALESCE(heating,0),COALESCE(amortization,0),COALESCE(rent,0),COALESCE(other_amount,0),"
                "cleaning_note,software_note,heating_note,amortization_note,rent_note,other_note "
                "FROM expenses WHERE date((CASE WHEN substr(trim(period_to),3,1)='.' AND substr(trim(period_to),6,1)='.' THEN CASE WHEN substr(trim(period_to),7,4) GLOB '[0-9][0-9][0-9][0-9]' THEN substr(trim(period_to),7,4) ELSE '20'||substr(trim(period_to),7,2) END||'-'||substr(trim(period_to),4,2)||'-'||substr(trim(period_to),1,2) ELSE period_to END))>=date(?) AND date((CASE WHEN substr(trim(period_from),3,1)='.' AND substr(trim(period_from),6,1)='.' THEN CASE WHEN substr(trim(period_from),7,4) GLOB '[0-9][0-9][0-9][0-9]' THEN substr(trim(period_from),7,4) ELSE '20'||substr(trim(period_from),7,2) END||'-'||substr(trim(period_from),4,2)||'-'||substr(trim(period_from),1,2) ELSE period_from END))<=date(?) "
                "ORDER BY date((CASE WHEN substr(trim(period_from),3,1)='.' AND substr(trim(period_from),6,1)='.' THEN CASE WHEN substr(trim(period_from),7,4) GLOB '[0-9][0-9][0-9][0-9]' THEN substr(trim(period_from),7,4) ELSE '20'||substr(trim(period_from),7,2) END||'-'||substr(trim(period_from),4,2)||'-'||substr(trim(period_from),1,2) ELSE period_from END)) DESC,date((CASE WHEN substr(trim(period_to),3,1)='.' AND substr(trim(period_to),6,1)='.' THEN CASE WHEN substr(trim(period_to),7,4) GLOB '[0-9][0-9][0-9][0-9]' THEN substr(trim(period_to),7,4) ELSE '20'||substr(trim(period_to),7,2) END||'-'||substr(trim(period_to),4,2)||'-'||substr(trim(period_to),1,2) ELSE period_to END)) DESC,id DESC");
            q.addBindValue(df);
            q.addBindValue(dt);
            const QStringList labels = {
                QString::fromUtf8("Електроенергія"), QString::fromUtf8("Прибирання"),
                QString::fromUtf8("Програми"), QString::fromUtf8("Опалення"),
                QString::fromUtf8("Амортизація"), QString::fromUtf8("Оренда"), QString::fromUtf8("Інше")
            };
            if (!q.exec()) snapshot.error = q.lastError().text();
            else while (q.next()) {
                for (int i = 0; i < labels.size(); ++i) {
                    const qint64 cents = qRound64(q.value(i + 2).toDouble() * 100);
                    if (cents == 0) continue;
                    TotalExpensesSnapshotRow row;
                    row.sortDate = q.value(1).toString();
                    row.period = QString::fromUtf8("%1 — %2").arg(
                        ReportsDateUtils::dmy(q.value(0).toString()), ReportsDateUtils::dmy(q.value(1).toString()));
                    row.category = labels[i];
                    row.description = i == 0 ? QString() : q.value(i + 8).toString();
                    row.source = QString::fromUtf8("Витрати за період");
                    row.total = FormatUtils::money(cents / 100.0, 2);
                    snapshot.periodicCents += cents;
                    snapshot.rows.append(row);
                }
            }
            if (q.lastError().isValid()) snapshot.error = q.lastError().text();
            // Old databases remain readable before their first schema upgrade.
            QSqlQuery presence(db);
            bool hasAdditional = false;
            if (snapshot.error.isEmpty()) {
                if (presence.exec("SELECT EXISTS(SELECT 1 FROM sqlite_schema WHERE type='table' AND name='additional_expenses' COLLATE NOCASE)") && presence.next())
                    hasAdditional = presence.value(0).toBool();
                else
                    snapshot.error = presence.lastError().text();
                presence.finish();
            }
            if (hasAdditional) {
                q.prepare("SELECT id,spent_on,category,description,amount_cents FROM additional_expenses "
                          "WHERE (CASE WHEN substr(trim(spent_on),3,1)='.' AND substr(trim(spent_on),6,1)='.' THEN CASE WHEN substr(trim(spent_on),7,4) GLOB '[0-9][0-9][0-9][0-9]' THEN substr(trim(spent_on),7,4) ELSE '20'||substr(trim(spent_on),7,2) END||'-'||substr(trim(spent_on),4,2)||'-'||substr(trim(spent_on),1,2) ELSE spent_on END)>=? AND (CASE WHEN substr(trim(spent_on),3,1)='.' AND substr(trim(spent_on),6,1)='.' THEN CASE WHEN substr(trim(spent_on),7,4) GLOB '[0-9][0-9][0-9][0-9]' THEN substr(trim(spent_on),7,4) ELSE '20'||substr(trim(spent_on),7,2) END||'-'||substr(trim(spent_on),4,2)||'-'||substr(trim(spent_on),1,2) ELSE spent_on END)<=? ORDER BY (CASE WHEN substr(trim(spent_on),3,1)='.' AND substr(trim(spent_on),6,1)='.' THEN CASE WHEN substr(trim(spent_on),7,4) GLOB '[0-9][0-9][0-9][0-9]' THEN substr(trim(spent_on),7,4) ELSE '20'||substr(trim(spent_on),7,2) END||'-'||substr(trim(spent_on),4,2)||'-'||substr(trim(spent_on),1,2) ELSE spent_on END) DESC,id DESC");
                q.addBindValue(df);
                q.addBindValue(dt);
                if (!q.exec()) snapshot.error = q.lastError().text();
                else while (q.next()) {
                    TotalExpensesSnapshotRow row;
                    row.expenseId = q.value(0).toLongLong();
                    row.sortDate = q.value(1).toString();
                    row.period = ReportsDateUtils::dmy(row.sortDate);
                    row.category = q.value(2).toString();
                    row.description = q.value(3).toString();
                    row.source = QString::fromUtf8("Додаткова витрата");
                    const qint64 cents = q.value(4).toLongLong();
                    row.total = FormatUtils::money(cents / 100.0, 2);
                    snapshot.additionalCents += cents;
                    if (row.category.simplified().compare(QString::fromUtf8("Нова пошта"), Qt::CaseInsensitive) == 0)
                        snapshot.novaPoshtaCents += cents;
                    snapshot.rows.append(row);
                }
                if (q.lastError().isValid()) snapshot.error = q.lastError().text();
            }
            std::stable_sort(snapshot.rows.begin(), snapshot.rows.end(),
                [](const TotalExpensesSnapshotRow& a, const TotalExpensesSnapshotRow& b) {
                    return a.sortDate > b.sortDate;
                });
            q.finish();
            if (snapshot.error.isEmpty() && !db.commit()) snapshot.error = db.lastError().text();
            if (!snapshot.error.isEmpty()) db.rollback();
        }
        db.close();
    }
    SqliteConnectionUtils::safeCloseAndRemoveDatabase(connName);
    if (!snapshot.error.isEmpty()) {
        snapshot.rows.clear();
        snapshot.periodicCents = snapshot.additionalCents = snapshot.novaPoshtaCents = 0;
        snapshot.stats = {QString::fromUtf8("Не вдалося завантажити витрати"), snapshot.error};
        return snapshot;
    }
    snapshot.stats = {
        QString::fromUtf8("Періодичні: %1 грн").arg(FormatUtils::money(snapshot.periodicCents / 100.0, 2)),
        QString::fromUtf8("Додаткові: %1 грн").arg(FormatUtils::money(snapshot.additionalCents / 100.0, 2)),
        QString::fromUtf8("Разом: %1 грн").arg(FormatUtils::money(
            (snapshot.periodicCents + snapshot.additionalCents) / 100.0, 2))
    };
    return snapshot;
}

} // namespace ReportsDataLoader


namespace ReportsDataLoader {

ExpensesSnapshot buildExpensesSnapshot(const QString& df, const QString& dt)
{
    return buildExpensesSnapshot(currentDatabasePath(), df, dt);
}

} // namespace ReportsDataLoader
