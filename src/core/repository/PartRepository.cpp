#include "core/services/orders/OrderFinancialService.h"
#include <cmath>
#include "core/finance/ExactMoney.h"
#include "core/repository/PartRepository.h"
#include "core/services/OrderIdentityResolver.h"
#include "core/services/orders/PartMarkupPolicyService.h"
#include "core/services/orders/PartPricingEngine.h"
#include "core/util/FormatUtils.h"

#include <QDateTime>
#include <QSqlError>
#include <QSqlQuery>
#include <QDebug>
#include "core/repository/RepositoryWriteBoundary.h"
#include <QtGlobal>

namespace {
QString nonNullText(const QString& value)
{
    return value.isNull() ? QStringLiteral("") : value;
}

QString orderUidForNumber(QSqlDatabase database, int orderNumber)
{
    return OrderIdentityResolver::canonicalOrderUidForInternalNumber(database, orderNumber);
}

bool refreshOrderFinancialSums(QSqlDatabase database, const QString& orderUid, int orderNumber, QString* errorText = nullptr)
{
    if (orderUid.trimmed().isEmpty() || orderNumber <= 0) {
        if (errorText) *errorText = QStringLiteral("Missing order identity for financial refresh");
        return false;
    }
    return OrderFinancialService::refreshOrderFinancialSumsByUid(database, orderUid, errorText);
}

bool partExistsForOrderUid(QSqlDatabase database, int partId, const QString& orderUid)
{
    if (partId <= 0 || orderUid.trimmed().isEmpty()) return false;
    QSqlQuery q(database);
    q.prepare(QStringLiteral("SELECT 1 FROM work_order_parts WHERE id=? AND order_uid=? LIMIT 1"));
    q.addBindValue(partId);
    q.addBindValue(orderUid.trimmed());
    if (!q.exec()) return false;
    return q.next();
}

bool partExistsById(QSqlDatabase database, int partId, QString* actualOrderUid = nullptr, int* actualOrderNumber = nullptr)
{
    if (partId <= 0) return false;
    QSqlQuery q(database);
    q.prepare(QStringLiteral("SELECT COALESCE(order_uid,''), COALESCE(order_number,0) FROM work_order_parts WHERE id=? LIMIT 1"));
    q.addBindValue(partId);
    if (!q.exec() || !q.next()) return false;
    if (actualOrderUid) *actualOrderUid = q.value(0).toString();
    if (actualOrderNumber) *actualOrderNumber = q.value(1).toInt();
    return true;
}

bool validatePartPricingInvariant(QSqlDatabase database,
                                  const QString& op,
                                  int orderNumber,
                                  int partId,
                                  const WorkOrderPartRow& row,
                                  QString* errorText)
{
    for (const double value : {row.qty, row.purchasePrice, row.clientPrice, row.purchaseAmount, row.clientAmount, row.markupPercent}) {
        if (!std::isfinite(value) || value < 0.0) {
            if (errorText) *errorText = QStringLiteral("Некоректна кількість або фінансове значення запчастини.");
            return false;
        }
    }
    if (row.qty <= 0.0) {
        if (errorText) *errorText = QStringLiteral("Кількість запчастини повинна бути більшою за нуль.");
        return false;
    }
    const double safeQty = row.qty;
    const bool newQuote = op == QStringLiteral("insert") || row.pricingCalcVersion >= 3;
    if (newQuote) {
        // The existing DTO uses double. Accept only values that round-trip at
        // the declared input precision; never silently truncate extra digits.
        auto decimalInput = [](double value, unsigned scale, std::string* text) {
            QString decimal=QString::number(value,'f',static_cast<int>(scale));
            if(decimal.toDouble()!=value) return false;
            while(decimal.contains('.') && decimal.endsWith('0')) decimal.chop(1);
            if(decimal.endsWith('.')) decimal.chop(1);
            *text=decimal.toStdString();
            return std::holds_alternative<ExactMoney::Decimal>(ExactMoney::parse(*text,scale));
        };
        std::string quantity,unitPrice,purchaseTotal,clientTotal;
        if(!decimalInput(row.qty,6,&quantity) || !decimalInput(row.purchasePrice,6,&unitPrice)
            || !decimalInput(row.purchaseAmount,2,&purchaseTotal) || !decimalInput(row.clientAmount,2,&clientTotal)) {
            if(errorText) *errorText=QStringLiteral("Кількість і ціна закупівлі допускають до 6 десяткових знаків, суми — до 2. Перевірте точність і розмір значень.");
            return false;
        }
        const auto calculated=ExactMoney::lineAmount(quantity,unitPrice);
        const auto recorded=ExactMoney::payment(purchaseTotal);
        if(!std::holds_alternative<ExactMoney::Cents>(calculated)
            || !std::holds_alternative<ExactMoney::Cents>(recorded)
            || std::get<ExactMoney::Cents>(calculated)!=std::get<ExactMoney::Cents>(recorded)) {
            if(errorText) *errorText=QStringLiteral("Сума закупівлі не відповідає кількості × ціні з округленням рядка до копійки, або перевищено допустиму суму.");
            return false;
        }
    }
    const auto money=[&row](double value) {
        return row.markupSource=="inventory" ? qRound64(qMax(0.0,value)*100.0)/100.0 : FormatUtils::roundMoneyValue(qMax(0.0,value));
    };
    const double purchasePrice = newQuote ? row.purchasePrice : money(row.purchasePrice);
    const double clientPrice = newQuote ? row.clientPrice : money(row.clientPrice);
    const double clientAmount = money(row.clientAmount);
    const double purchaseAmount = money(row.purchaseAmount);
    const double markupPercent = qMax(0.0, row.markupPercent);

    if (purchasePrice <= 0.0 && clientPrice <= 0.0 && clientAmount <= 0.0) {
        return true;
    }

    PartPricingInput input;
    input.partName = row.name;
    input.store = row.store;
    input.qty = safeQty;
    input.purchasePrice = purchasePrice;
    input.manualMarkupOverride = row.manualMarkupOverride;
    input.manualMarkupPercent = row.manualMarkupOverride ? markupPercent : 0.0;
    input.sourceType = newQuote ? QStringLiteral("save") : QStringLiteral("legacy_save");
    const auto policy = PartMarkupPolicyService::loadPolicySnapshot(database);
    const PartPricingResult expected = PartPricingEngine::calculate(input, policy);

    const bool okClientPrice = qAbs(clientPrice - expected.clientPrice) < 0.01;
    const bool okClientAmount = qAbs(clientAmount - expected.clientAmount) < 0.01;
    const bool okPurchaseAmount = purchaseAmount <= 0.009 || qAbs(purchaseAmount - expected.purchaseAmount) < 0.01;
    const bool okPct = qAbs(markupPercent - expected.markupPercent) < 0.01;
    if (okClientPrice && okClientAmount && okPurchaseAmount && okPct) {
        return true;
    }

    const QString detail = QStringLiteral(
        "Pricing invariant failed op=%1 order=%2 partId=%3 stored(price=%4 amount=%5 purchase=%6 purchaseAmount=%7 pct=%8 override=%9) expected(price=%10 amount=%11 purchase=%12 purchaseAmount=%13 pct=%14 source=%15)")
        .arg(op)
        .arg(orderNumber)
        .arg(partId)
        .arg(clientPrice, 0, 'f', 2)
        .arg(clientAmount, 0, 'f', 2)
        .arg(purchasePrice, 0, 'f', 2)
        .arg(purchaseAmount, 0, 'f', 2)
        .arg(markupPercent, 0, 'f', 2)
        .arg(row.manualMarkupOverride ? 1 : 0)
        .arg(expected.clientPrice, 0, 'f', 2)
        .arg(expected.clientAmount, 0, 'f', 2)
        .arg(expected.purchasePrice, 0, 'f', 2)
        .arg(expected.purchaseAmount, 0, 'f', 2)
        .arg(expected.markupPercent, 0, 'f', 2)
        .arg(expected.markupSource);
    qCritical().noquote() << QStringLiteral("PART_PRICING_INVARIANT_VIOLATION op=%1 order=%2 partId=%3 fields=client_price,client_amount,purchase_amount,markup_percent details=\"%4\"")
        .arg(op)
        .arg(orderNumber)
        .arg(partId)
        .arg(detail);
    if (errorText) *errorText = QStringLiteral("Ціна запчастини не відповідає чинній націнці постачальника. Очікувана ціна клієнту: %1 грн. Операцію не збережено. Оновіть дані та повторіть спробу.")
        .arg(expected.clientPrice, 0, 'f', 2);
    return false;
}

QList<WorkOrderPartRow> listByOrderImpl(QSqlDatabase database, const QString& orderUid, int orderNumber)
{
    QList<WorkOrderPartRow> rows;
    if (orderUid.trimmed().isEmpty() && orderNumber <= 0) return rows;

    QSqlQuery q(database);
    q.prepare(QStringLiteral(
        "SELECT id, COALESCE(ordered_at,''), COALESCE(delivery_date,''), COALESCE(installed_at,''), COALESCE(store,''), COALESCE(part_number,''), COALESCE(name,''), "
        "COALESCE(note,''), COALESCE(manufacturer,''), COALESCE(qty,1), COALESCE(price,0), COALESCE(amount,0), "
        "COALESCE(purchase_price,0), COALESCE(purchase_amount,0), COALESCE(markup_percent,0), "
        "COALESCE(manual_markup_override,0), COALESCE(markup_source,'policy'), COALESCE(pricing_updated_at,''), COALESCE(pricing_calc_version,1), "
        "COALESCE(service_id,0), COALESCE(order_number,0) "
        "FROM work_order_parts WHERE TRIM(COALESCE(order_uid,''))<>'' AND order_uid=? "
        "ORDER BY CASE WHEN COALESCE(service_id,0)>0 THEN 0 ELSE 1 END, COALESCE(service_id,2147483647), id"));
    q.addBindValue(orderUid.trimmed());
    if (!q.exec()) return rows;

    while (q.next()) {
        WorkOrderPartRow row;
        row.id = q.value(0).toInt();
        row.orderNumber = q.value(20).toInt();
        row.orderedAt = q.value(1).toString();
        row.deliveryDate = q.value(2).toString();
        row.installedAt = q.value(3).toString();
        row.store = q.value(4).toString();
        row.partNumber = q.value(5).toString();
        row.name = q.value(6).toString();
        row.note = q.value(7).toString();
        row.manufacturer = q.value(8).toString();
        row.qty = q.value(9).toDouble();
        row.clientPrice = q.value(10).toDouble();
        row.clientAmount = q.value(11).toDouble();
        row.purchasePrice = q.value(12).toDouble();
        row.purchaseAmount = q.value(13).toDouble();
        row.markupPercent = q.value(14).toDouble();
        row.manualMarkupOverride = q.value(15).toInt() != 0;
        row.markupSource = q.value(16).toString().trimmed().isEmpty() ? QStringLiteral("policy") : q.value(16).toString().trimmed();
        row.pricingUpdatedAt = q.value(17).toString();
        row.pricingCalcVersion = q.value(18).toInt() > 0 ? q.value(18).toInt() : 1;
        row.serviceId = q.value(19).toInt();
        rows.append(row);
    }
    return rows;
}
}

QList<WorkOrderPartRow> PartRepository::listByOrder(QSqlDatabase database, int orderNumber)
{
    return listByOrderImpl(database, orderUidForNumber(database, orderNumber), orderNumber);
}

QList<WorkOrderPartRow> PartRepository::listByOrderUid(QSqlDatabase database, const QString& orderUid)
{
    return listByOrderImpl(database, orderUid.trimmed(), 0);
}

bool PartRepository::insert(QSqlDatabase database,
                            int orderNumber,
                            const WorkOrderPartRow& row,
                            int* newPartId,
                            QString* errorText)
{
    if (newPartId) *newPartId = 0;
    RepositoryWriteBoundary boundary(database, errorText);
    if (!boundary.started()) return false;
    const QString orderUid = orderUidForNumber(database, orderNumber);
    if (orderUid.isEmpty()) {
        if (errorText) *errorText = QStringLiteral("Order uid not found for order %1").arg(orderNumber);
        return false;
    }
    if (!validatePartPricingInvariant(database, QStringLiteral("insert"), orderNumber, 0, row, errorText)) {
        return false;
    }

    QSqlQuery q(database);
    q.prepare(QStringLiteral(
        "INSERT INTO work_order_parts(order_uid, order_number, ordered_at, delivery_date, installed_at, store, part_number, name, note, manufacturer, qty, price, amount, service_id, ordered_by_admin, paid, purchase_price, purchase_amount, markup_percent, manual_markup_override, markup_source, pricing_updated_at, pricing_calc_version) "
        "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)"));
    q.addBindValue(orderUid);
    q.addBindValue(orderNumber);
    q.addBindValue(nonNullText(row.orderedAt));
    q.addBindValue(nonNullText(row.deliveryDate));
    q.addBindValue(nonNullText(row.installedAt));
    q.addBindValue(nonNullText(row.store));
    q.addBindValue(nonNullText(row.partNumber));
    q.addBindValue(nonNullText(row.name));
    q.addBindValue(nonNullText(row.note));
    q.addBindValue(nonNullText(row.manufacturer));
    q.addBindValue(row.qty);
    q.addBindValue(row.clientPrice);
    q.addBindValue(row.clientAmount);
    q.addBindValue(row.serviceId);
    q.addBindValue(1);
    q.addBindValue(0);
    q.addBindValue(row.purchasePrice);
    q.addBindValue(row.purchaseAmount);
    q.addBindValue(row.markupPercent);
    q.addBindValue(row.manualMarkupOverride ? 1 : 0);
    q.addBindValue(row.markupSource.trimmed().isEmpty() ? QStringLiteral("policy") : row.markupSource.trimmed());
    q.addBindValue(row.pricingUpdatedAt.trimmed().isEmpty() ? QDateTime::currentDateTimeUtc().toString(Qt::ISODate) : row.pricingUpdatedAt.trimmed());
    q.addBindValue(qMax(3, row.pricingCalcVersion));
    if (!q.exec()) {
        if (errorText) *errorText = q.lastError().text();
        qCritical().noquote() << QStringLiteral("DB_MUTATION part_insert_failed order=") << orderNumber
                              << QStringLiteral(" orderUid=") << orderUid
                              << QStringLiteral(" error=") << q.lastError().text();
        return false;
    }
    const int affected = q.numRowsAffected();
    int insertedId = 0;
    const QVariant lastId = q.lastInsertId();
    insertedId = lastId.isValid() ? lastId.toInt() : 0;
    if (affected <= 0 || insertedId <= 0 || !partExistsForOrderUid(database, insertedId, orderUid)) {
        const QString detail = QStringLiteral("Part INSERT did not create a verified row: affected=%1 insertedId=%2 order=%3 orderUid=%4")
            .arg(affected).arg(insertedId).arg(orderNumber).arg(orderUid);
        if (errorText) *errorText = detail;
        qCritical().noquote() << QStringLiteral("DB_MUTATION part_insert_unverified") << detail;
        return false;
    }
    if (!refreshOrderFinancialSums(database, orderUid, orderNumber, errorText)) {
        return false;
    }
    if (!boundary.finish()) return false;
    if (newPartId) *newPartId = insertedId;
    return true;
}

bool PartRepository::update(QSqlDatabase database,
                            int orderNumber,
                            int partId,
                            const WorkOrderPartRow& row,
                            QString* errorText)
{
    RepositoryWriteBoundary boundary(database, errorText);
    if (!boundary.started()) return false;
    const QString orderUid = orderUidForNumber(database, orderNumber);
    if (orderUid.isEmpty()) {
        if (errorText) *errorText = QStringLiteral("Order uid not found for order %1").arg(orderNumber);
        return false;
    }
    if (!validatePartPricingInvariant(database, QStringLiteral("update"), orderNumber, partId, row, errorText)) {
        return false;
    }

    QSqlQuery q(database);
    q.prepare(QStringLiteral(
        "UPDATE work_order_parts SET order_uid=?, order_number=?, ordered_at=?, delivery_date=?, installed_at=?, store=?, part_number=?, name=?, note=?, manufacturer=?, qty=?, price=?, amount=?, purchase_price=?, purchase_amount=?, markup_percent=?, manual_markup_override=?, markup_source=?, pricing_updated_at=?, pricing_calc_version=?, service_id=? "
        "WHERE id=? AND order_uid=?"));
    q.addBindValue(orderUid);
    q.addBindValue(orderNumber);
    q.addBindValue(nonNullText(row.orderedAt));
    q.addBindValue(nonNullText(row.deliveryDate));
    q.addBindValue(nonNullText(row.installedAt));
    q.addBindValue(nonNullText(row.store));
    q.addBindValue(nonNullText(row.partNumber));
    q.addBindValue(nonNullText(row.name));
    q.addBindValue(nonNullText(row.note));
    q.addBindValue(nonNullText(row.manufacturer));
    q.addBindValue(row.qty);
    q.addBindValue(row.clientPrice);
    q.addBindValue(row.clientAmount);
    q.addBindValue(row.purchasePrice);
    q.addBindValue(row.purchaseAmount);
    q.addBindValue(row.markupPercent);
    q.addBindValue(row.manualMarkupOverride ? 1 : 0);
    q.addBindValue(row.markupSource.trimmed().isEmpty() ? QStringLiteral("policy") : row.markupSource.trimmed());
    q.addBindValue(row.pricingUpdatedAt.trimmed().isEmpty() ? QDateTime::currentDateTimeUtc().toString(Qt::ISODate) : row.pricingUpdatedAt.trimmed());
    q.addBindValue(row.pricingCalcVersion > 0 ? row.pricingCalcVersion : 1);
    q.addBindValue(row.serviceId);
    q.addBindValue(partId);
    q.addBindValue(orderUid);
    if (!q.exec()) {
        if (errorText) *errorText = q.lastError().text();
        qCritical().noquote() << QStringLiteral("DB_MUTATION part_update_failed order=") << orderNumber
                              << QStringLiteral(" orderUid=") << orderUid
                              << QStringLiteral(" partId=") << partId
                              << QStringLiteral(" error=") << q.lastError().text();
        return false;
    }
    const int affected = q.numRowsAffected();
    if (affected <= 0) {
        QString actualUid;
        int actualNumber = 0;
        const bool idExists = partExistsById(database, partId, &actualUid, &actualNumber);
        const bool belongsToOrder = idExists && actualUid.trimmed() == orderUid.trimmed();
        if (!belongsToOrder) {
            const QString detail = QStringLiteral("Part UPDATE matched no row: partId=%1 expectedOrder=%2 expectedOrderUid=%3 actualOrder=%4 actualOrderUid=%5")
                .arg(partId).arg(orderNumber).arg(orderUid).arg(actualNumber).arg(actualUid);
            if (errorText) *errorText = detail;
            qCritical().noquote() << QStringLiteral("DB_MUTATION part_update_order_uid_mismatch") << detail;
            return false;
        }
        qWarning().noquote() << QStringLiteral("DB_MUTATION part_update_noop_verified order=") << orderNumber
                             << QStringLiteral(" orderUid=") << orderUid
                             << QStringLiteral(" partId=") << partId;
    }
    if (!refreshOrderFinancialSums(database, orderUid, orderNumber, errorText)) {
        return false;
    }
    return boundary.finish();
}

bool PartRepository::remove(QSqlDatabase database,
                            int orderNumber,
                            int partId,
                            QString* errorText)
{
    RepositoryWriteBoundary boundary(database, errorText);
    if (!boundary.started()) return false;
    const QString orderUid = orderUidForNumber(database, orderNumber);
    QSqlQuery q(database);
    q.prepare(QStringLiteral("DELETE FROM work_order_parts WHERE id=? AND order_uid=?"));
    q.addBindValue(partId);
    q.addBindValue(orderUid);
    if (!q.exec()) {
        if (errorText) *errorText = q.lastError().text();
        qCritical().noquote() << QStringLiteral("DB_MUTATION part_delete_failed order=") << orderNumber
                              << QStringLiteral(" orderUid=") << orderUid
                              << QStringLiteral(" partId=") << partId
                              << QStringLiteral(" error=") << q.lastError().text();
        return false;
    }
    const int affected = q.numRowsAffected();
    if (affected <= 0) {
        QString actualUid;
        int actualNumber = 0;
        const bool idExists = partExistsById(database, partId, &actualUid, &actualNumber);
        const QString detail = QStringLiteral("Part DELETE matched no row: partId=%1 expectedOrder=%2 expectedOrderUid=%3 actualOrder=%4 actualOrderUid=%5 idExists=%6")
            .arg(partId).arg(orderNumber).arg(orderUid).arg(actualNumber).arg(actualUid).arg(idExists);
        if (errorText) *errorText = detail;
        qCritical().noquote() << QStringLiteral("DB_MUTATION part_delete_no_rows") << detail;
        return false;
    }
    if (!refreshOrderFinancialSums(database, orderUid, orderNumber, errorText)) {
        return false;
    }
    return boundary.finish();
}

bool PartRepository::syncRows(QSqlDatabase database,
                              int orderNumber,
                              const QList<WorkOrderPartRow>& rows,
                              QString* errorText)
{
    RepositoryWriteBoundary boundary(database, errorText);
    if (!boundary.started()) return false;
    const QString orderUid = orderUidForNumber(database, orderNumber);
    QList<int> existingIds;
    QSqlQuery idsQ(database);
    idsQ.prepare(QStringLiteral("SELECT id FROM work_order_parts WHERE order_uid=? ORDER BY id"));
    idsQ.addBindValue(orderUid);
    if (!idsQ.exec()) {
        if (errorText) *errorText = idsQ.lastError().text();
        return false;
    }
    while (idsQ.next()) existingIds << idsQ.value(0).toInt();
    if (idsQ.lastError().isValid()) {
        if (errorText) *errorText = idsQ.lastError().text();
        return false;
    }
    idsQ.finish();

    const int sharedCount = qMin(existingIds.size(), rows.size());
    for (int i = 0; i < sharedCount; ++i) {
        if (!update(database, orderNumber, existingIds.at(i), rows.at(i), errorText)) return false;
    }
    for (int i = sharedCount; i < rows.size(); ++i) {
        if (!insert(database, orderNumber, rows.at(i), nullptr, errorText)) return false;
    }
    for (int i = sharedCount; i < existingIds.size(); ++i) {
        if (!remove(database, orderNumber, existingIds.at(i), errorText)) return false;
    }
    return boundary.finish();
}
