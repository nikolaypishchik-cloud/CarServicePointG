#include "core/services/orders/OrderDialogPrintQueryService.h"
#include "core/util/StoreNameNormalizer.h"

#include "infrastructure/db/sqlite/Db.h"
#include "core/services/ReportsDateUtils.h"

#include <QSqlQuery>
#include <QHash>
#include <QSqlRecord>
#include <QSet>
#include "core/services/WorkshopInventoryService.h"
#include "core/util/OilServiceClassifier.h"
#include "core/services/orders/PartMarkupPolicyService.h"

QList<OrderDialogPrintServiceRow> OrderDialogPrintQueryService::loadServiceRows(const QString& orderUid)
{
    QList<OrderDialogPrintServiceRow> rows;
    if (orderUid.trimmed().isEmpty()) return rows;

    QSqlQuery q(Database::db());
    q.prepare(
        "SELECT COALESCE(s.title,''), "
        "COALESCE(GROUP_CONCAT(COALESCE(NULLIF(m.name,''), NULLIF(sm.master_name_closed,'')), CHAR(10)), ''), "
        "COALESCE((SELECT COALESCE(NULLIF(oj.company_name,''), '') FROM outsource_jobs oj WHERE oj.service_id=s.id ORDER BY oj.id DESC LIMIT 1), ''), "
        "COALESCE(s.qty,1), COALESCE(s.price,0), s.id "
        "FROM work_order_services s "
        "LEFT JOIN service_masters sm ON sm.service_id=s.id "
        "LEFT JOIN masters m ON m.id=sm.master_id "
        "WHERE s.order_uid=? "
        "GROUP BY s.id, s.title, s.qty, s.price "
        "ORDER BY s.id");
    q.addBindValue(orderUid);
    if (!q.exec()) return rows;
    QHash<qint64, int> serviceIndexes;
    while (q.next()) {
        OrderDialogPrintServiceRow row;
        row.title = q.value(0).toString().trimmed();
        row.linkedMasters = q.value(1).toString().trimmed();
        row.outsourceCompany = q.value(2).toString().trimmed();
        row.qty = q.value(3).toDouble();
        row.price = q.value(4).toDouble();
        serviceIndexes.insert(q.value(5).toLongLong(), rows.size());
        rows.push_back(row);
    }
    // A work's salary follows its assigned masters and their participation shares.
    q.prepare("SELECT s.id, sm.master_id, "
              "COALESCE(NULLIF(m.name,''), sm.master_name_closed,''), COALESCE(sm.share_percent,0) "
              "FROM work_order_services s JOIN service_masters sm ON sm.service_id=s.id "
              "LEFT JOIN masters m ON m.id=sm.master_id "
              "WHERE s.order_uid=? AND COALESCE(NULLIF(sm.role,''),'master')='master'");
    q.addBindValue(orderUid);
    QMap<QPair<QString, QString>, double> percentages;
    if (q.exec()) {
        while (q.next()) {
            const auto index = serviceIndexes.constFind(q.value(0).toLongLong());
            if (index == serviceIndexes.cend()) continue;
            const auto key = qMakePair(q.value(1).toString(), q.value(2).toString());
            if (!percentages.contains(key))
                percentages.insert(key, Database::effectivePayrollPercent(key.first, key.second));
            auto& row = rows[*index];
            row.hasPayrollMaster = true;
            row.payrollAmount += qRound64(row.qty * row.price * q.value(3).toDouble()
                                         * percentages.value(key) / 100.0) / 100.0;
        }
    }
    return rows;
}

QList<OrderDialogPrintPartRow> OrderDialogPrintQueryService::loadPartRows(const QString& orderUid, bool useCurrentInventoryPrices)
{
    QList<OrderDialogPrintPartRow> rows;
    if (orderUid.trimmed().isEmpty()) return rows;

    QSet<QString> materials;
    struct StockPrice { QDate date; qint64 id; double cost; QVariant sale; };
    QHash<QString, StockPrice> stockPrices;
    QSet<int> issuedSpares;
    QSqlQuery stock(Database::db());
    if (stock.exec("SELECT material_name,purchased_on,id,unit_cost,client_price FROM workshop_inventory_purchases WHERE stock_kind='material'")) {
        while (stock.next()) {
            const QString key = WorkshopInventoryService::normalizedMaterialKey(stock.value(0).toString());
            materials.insert(key);
            const QDate date = ReportsDateUtils::effectiveDate(stock.value(1).toString(), QString());
            if (!useCurrentInventoryPrices || !date.isValid() || date > QDate::currentDate()) continue;
            const qint64 id = stock.value(2).toLongLong();
            const auto previous = stockPrices.constFind(key);
            if (previous == stockPrices.cend() || date > previous->date || (date == previous->date && id > previous->id))
                stockPrices.insert(key, {date, id, stock.value(3).toDouble(), stock.value(4)});
        }
    }
    if (stock.exec("SELECT part_id FROM workshop_spare_movements WHERE operation='issue'"))
        while (stock.next()) issuedSpares.insert(stock.value(0).toInt());

    QSqlQuery q(Database::db());
    q.prepare("SELECT COALESCE(p.part_number,''), COALESCE(p.manufacturer,''), COALESCE(p.name,''), COALESCE(p.qty,1), COALESCE(p.price,0), COALESCE(p.amount,p.price*p.qty,0), "
              "COALESCE(p.store,''), COALESCE(p.purchase_price,0), COALESCE(p.purchase_amount,p.purchase_price*p.qty,0), "
              "COALESCE(a.invoice_date,''), COALESCE(p.ordered_at,''), COALESCE(a.invoice_number,''), p.id "
              "FROM work_order_parts p LEFT JOIN work_order_part_attachments a ON a.part_id=p.id "
              "WHERE p.order_uid=? ORDER BY p.id");
    q.addBindValue(orderUid);
    if (!q.exec()) return rows;
    while (q.next()) {
        OrderDialogPrintPartRow row;
        row.partNo = q.value(0).toString().trimmed();
        row.brand = q.value(1).toString().trimmed();
        row.name = q.value(2).toString().trimmed();
        row.qty = q.value(3).toDouble();
        row.price = q.value(4).toDouble();
        row.amount = q.value(5).toDouble();
        row.store = StoreNameNormalizer::canonicalDisplayName(q.value(6).toString());
        row.customerSupplied = PartMarkupPolicyService::isCustomerSupplied(row.store);
        row.stockMaterial = !issuedSpares.contains(q.value(12).toInt()) && PartMarkupPolicyService::isInventoryStore(row.store)
            && (materials.contains(WorkshopInventoryService::normalizedMaterialKey(row.name))
                || OilServiceClassifier::isMotorOilProductName(row.name));
        const QDate purchaseDate = ReportsDateUtils::effectiveDate(q.value(9).toString(), q.value(10).toString());
        row.purchaseDate = purchaseDate.isValid() ? purchaseDate.toString(QStringLiteral("dd.MM.yy")) : QString();
        row.invoiceNumber = q.value(11).toString().trimmed();
        row.purchaseAmount = q.value(8).toDouble();
        row.purchasePrice = row.qty > 0.0 ? row.purchaseAmount / row.qty : q.value(7).toDouble();
        // Administrative reports use the warehouse price list; customer documents keep the agreed order prices.
        const auto current = stockPrices.constFind(WorkshopInventoryService::normalizedMaterialKey(row.name));
        if (row.stockMaterial && current != stockPrices.cend()) {
            row.purchasePrice = current->cost;
            row.purchaseAmount = qRound64(row.qty * row.purchasePrice * 100.0) / 100.0;
            if (!current->sale.isNull()) row.price = current->sale.toDouble();
            row.amount = qRound64(row.qty * row.price * 100.0) / 100.0;
        }
        if (row.customerSupplied) {
            row.price = row.amount = row.purchasePrice = row.purchaseAmount = 0.0;
        }
        rows.push_back(row);
    }
    return rows;
}
