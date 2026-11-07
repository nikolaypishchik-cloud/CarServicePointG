#include "core/services/orders/PricingRecalculationService.h"
#include "core/diagnostics/RuntimeLoggingControl.h"

#include "core/services/MutationExecutor.h"
#include "core/services/orders/OrderFinancialService.h"
#include "core/services/orders/PartMarkupPolicyService.h"
#include "core/services/orders/PartPricingEngine.h"
#include "core/util/FormatUtils.h"
#include "infrastructure/db/sqlite/Db.h"

#include <QDateTime>
#include <QDebug>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QtGlobal>
#include <utility>
#include <cmath>

namespace PricingRecalculationService {
namespace {

QString safeMarkupSource(QString source, bool manualOverride)
{
    source = source.trimmed();
    if (manualOverride) return QStringLiteral("manual");
    return source.isEmpty() ? QStringLiteral("policy") : source;
}

} // namespace

MutationResult recalcPartMarkupsFromSettingsDetailed(QSqlDatabase db, RecalculationScope scope)
{
    const auto policySnapshot = PartMarkupPolicyService::loadPolicySnapshot(db);

    MutationExecutor::Request request;
    request.sourceModule = QStringLiteral("PricingRecalculationService");
    request.entityType = QStringLiteral("work_order_parts_batch");
    request.entityId = QStringLiteral("all");
    request.operation = QStringLiteral("recalc_markups");
    request.description = QString::fromUtf8("Перерахунок націнок запчастин");
    request.changedTables = {QStringLiteral("work_order_parts"), QStringLiteral("work_orders")};
    request.beginErrorFallback = QStringLiteral("Не вдалося відкрити транзакцію перерахунку націнок.");
    request.commitErrorFallback = QStringLiteral("Не вдалося завершити перерахунок націнок.");

    const auto execResult = MutationExecutor::execute(db, request, [&](QSqlDatabase& txDb, QString* txError) {
        QSet<QString> affectedOrderUids;
        QSet<int> affectedOrderNumbers;
        int totalRows = 0;
        int manualRows = 0;
        int policyRows = 0;
        int legacyRows = 0;

        QSqlQuery q(txDb);
        QString selectSql = QStringLiteral(
            "SELECT id, COALESCE(order_uid,''), COALESCE(order_number,0), COALESCE(name,''), COALESCE(store,''), "
            "qty, COALESCE(price,0), COALESCE(amount,0), COALESCE(purchase_price,0), COALESCE(purchase_amount,0), "
            "COALESCE(markup_percent,0), COALESCE(manufacturer,''), COALESCE(markup_source,'policy'), COALESCE(pricing_calc_version,0) "
            "FROM work_order_parts WHERE COALESCE(manual_markup_override,0)=0");
        if (scope == RecalculationScope::OpenOrdersOnly) {
            selectSql += QStringLiteral(
                " AND COALESCE(order_number,0) IN ("
                "SELECT number FROM work_orders WHERE "
                "TRIM(COALESCE(closed_at,''))='' "
                "AND LOWER(TRIM(COALESCE(status,''))) NOT IN ('closed','закритий','закрито','виконаний','виконано','done','completed'))");
        }
        q.prepare(selectSql);
        if (!q.exec()) {
            if (txError) *txError = q.lastError().text();
            return false;
        }

        QSqlQuery upd(txDb);
        upd.prepare(QStringLiteral(
            "UPDATE work_order_parts SET "
            "price=?, amount=?, purchase_price=?, purchase_amount=?, markup_percent=?, "
            "manual_markup_override=0, markup_source=?, pricing_updated_at=?, pricing_calc_version=2, "
            "manufacturer=CASE WHEN TRIM(COALESCE(manufacturer,''))='' AND ? <> '' THEN ? ELSE manufacturer END "
            "WHERE id=?"));

        const QString updatedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
        while (q.next()) {
            ++totalRows;
            const int id = q.value(0).toInt();
            const QString orderUid = q.value(1).toString().trimmed();
            const int orderNumber = q.value(2).toInt();
            const QString partName = q.value(3).toString();
            const QString store = q.value(4).toString();
            bool quantityValid = false;
            const double qty = q.value(5).toDouble(&quantityValid);
            if (q.value(5).isNull() || !quantityValid || !std::isfinite(qty) || qty <= 0.0) {
                if (txError) *txError = QStringLiteral("Некоректна кількість запчастини ID %1. Перерахунок скасовано без збереження змін.").arg(id);
                q.finish();
                return false;
            }
            const double storedClientPrice = FormatUtils::roundMoneyValue(qMax(0.0, q.value(6).toDouble()));
            const double storedClientAmount = FormatUtils::roundMoneyValue(qMax(0.0, q.value(7).toDouble()));
            double purchasePrice = FormatUtils::roundMoneyValue(qMax(0.0, q.value(8).toDouble()));
            const double purchaseAmount = FormatUtils::roundMoneyValue(qMax(0.0, q.value(9).toDouble()));
            const double storedPct = qMax(0.0, q.value(10).toDouble());
            const QString storedSource = safeMarkupSource(q.value(12).toString(), false);
            const int storedCalcVersion = q.value(13).toInt();

            if (purchasePrice <= 0.0 && purchaseAmount > 0.0 && qty > 0.0) {
                purchasePrice = FormatUtils::roundMoneyValue(purchaseAmount / qty);
            }
            if (purchasePrice <= 0.0 && storedClientPrice > 0.0) {
                purchasePrice = storedClientPrice;
                ++legacyRows;
            } else if (purchasePrice <= 0.0 && storedClientAmount > 0.0 && qty > 0.0) {
                purchasePrice = FormatUtils::roundMoneyValue(storedClientAmount / qty);
                ++legacyRows;
            }

            PartPricingInput pricingInput;
            pricingInput.partName = partName;
            pricingInput.store = store;
            pricingInput.qty = qty;
            pricingInput.purchasePrice = purchasePrice;
            pricingInput.sourceType = storedCalcVersion < 2 ? QStringLiteral("legacy_recalc") : QStringLiteral("recalc");
            pricingInput.manualMarkupOverride = false;
            pricingInput.manualMarkupPercent = 0.0;

            const PartPricingResult pricing = PartPricingEngine::calculate(pricingInput, policySnapshot);
            ++policyRows;
            if (!orderUid.isEmpty()) affectedOrderUids.insert(orderUid);
            if (orderNumber > 0) affectedOrderNumbers.insert(orderNumber);

            upd.addBindValue(pricing.clientPrice);
            upd.addBindValue(pricing.clientAmount);
            upd.addBindValue(pricing.purchasePrice);
            upd.addBindValue(pricing.purchaseAmount);
            upd.addBindValue(pricing.markupPercent);
            upd.addBindValue(pricing.markupSource.trimmed().isEmpty() ? storedSource : pricing.markupSource.trimmed());
            upd.addBindValue(updatedAt);
            const QString effectiveManufacturer = PartMarkupPolicyService::effectiveOilBrand(partName, store, policySnapshot);
            upd.addBindValue(effectiveManufacturer);
            upd.addBindValue(effectiveManufacturer);
            upd.addBindValue(id);
            if (!upd.exec()) {
                const QString sqlError = upd.lastError().text();
                upd.finish();
                if (txError) *txError = sqlError;
                return false;
            }
            upd.finish();
        }
        q.finish();

        for (const QString& uid : std::as_const(affectedOrderUids)) {
            QString sumError;
            if (!OrderFinancialService::refreshOrderFinancialSumsByUid(txDb, uid, &sumError)) {
                if (txError) *txError = QStringLiteral("Order financial refresh failed for uid=%1: %2").arg(uid, sumError);
                return false;
            }
        }
        for (int orderNumber : std::as_const(affectedOrderNumbers)) {
            QString sumError;
            if (!OrderFinancialService::refreshOrderFinancialSums(txDb, orderNumber, &sumError)) {
                if (txError) *txError = QStringLiteral("Order financial refresh failed for order=%1: %2").arg(orderNumber).arg(sumError);
                return false;
            }
        }

        if (RuntimeLoggingControl::deepDebugEnabled()) qInfo().noquote() << QStringLiteral("PART_PRICING op=recalc order=0 partId=0 trace=settings_batch totalRows=%1 manualRows=%2 policyRows=%3 legacyRows=%4 affectedOrderUids=%5 affectedOrderNumbers=%6")
            .arg(totalRows)
            .arg(manualRows)
            .arg(policyRows)
            .arg(legacyRows)
            .arg(affectedOrderUids.size())
            .arg(affectedOrderNumbers.size());
        return true;
    });
    return execResult;
}

bool recalcPartMarkupsFromSettings(QSqlDatabase db, RecalculationScope scope, QString* errorText)
{
    const auto result = recalcPartMarkupsFromSettingsDetailed(db, scope);
    if (!result.ok && errorText) *errorText = result.errorText;
    return result.ok;
}

bool recalcPartMarkupsFromSettings(RecalculationScope scope, QString* errorText)
{
    return recalcPartMarkupsFromSettings(Database::db(), scope, errorText);
}

MutationResult recalcAllPartMarkupsFromSettingsDetailed(QSqlDatabase db)
{
    return recalcPartMarkupsFromSettingsDetailed(db, RecalculationScope::AllOrders);
}

bool recalcAllPartMarkupsFromSettings(QSqlDatabase db, QString* errorText)
{
    return recalcPartMarkupsFromSettings(db, RecalculationScope::AllOrders, errorText);
}

MutationResult repairLegacyPricingMetadataAndFinancialSumsDetailed(QSqlDatabase db)
{
    MutationExecutor::Request request;
    request.sourceModule = QStringLiteral("PricingRecalculationService");
    request.entityType = QStringLiteral("work_order_parts_batch");
    request.entityId = QStringLiteral("legacy");
    request.operation = QStringLiteral("repair_legacy_pricing_metadata_and_sums");
    request.description = QString::fromUtf8("Нормалізація legacy-метаданих націнки та фінансових сум");
    request.changedTables = {QStringLiteral("work_order_parts"), QStringLiteral("work_orders")};
    request.beginErrorFallback = QStringLiteral("Не вдалося відкрити транзакцію repair pricing metadata.");
    request.commitErrorFallback = QStringLiteral("Не вдалося завершити repair pricing metadata.");

    return MutationExecutor::execute(db, request, [](QSqlDatabase& txDb, QString* txError) {
        QSqlQuery q(txDb);
        QStringList statements;
        statements << QStringLiteral("UPDATE work_order_parts SET manual_markup_override=CASE WHEN COALESCE(manual_markup_override,0)<>0 THEN 1 ELSE 0 END")
                   << QStringLiteral("UPDATE work_order_parts SET markup_source='manual' WHERE COALESCE(manual_markup_override,0)<>0")
                   << QStringLiteral("UPDATE work_order_parts SET markup_source='legacy' WHERE COALESCE(manual_markup_override,0)=0 AND COALESCE(pricing_updated_at,'')='' AND (TRIM(COALESCE(markup_source,''))='' OR LOWER(TRIM(COALESCE(markup_source,'')))='policy')")
                   << QStringLiteral("UPDATE work_order_parts SET markup_source='policy' WHERE TRIM(COALESCE(markup_source,''))=''")
                   << QStringLiteral("UPDATE work_order_parts SET pricing_calc_version=2 WHERE COALESCE(pricing_calc_version,0)<=0")
                   << QStringLiteral("UPDATE work_order_parts SET pricing_updated_at=datetime('now') WHERE TRIM(COALESCE(pricing_updated_at,''))=''");

        for (const QString& sql : std::as_const(statements)) {
            if (!q.exec(sql)) {
                if (txError) *txError = q.lastError().text();
                return false;
            }
            q.finish();
        }

        QString sumsError;
        if (!OrderFinancialService::refreshAllOrderFinancialSums(txDb, &sumsError)) {
            if (txError) *txError = sumsError;
            return false;
        }

        if (RuntimeLoggingControl::deepDebugEnabled()) qInfo().noquote() << QStringLiteral("PART_PRICING op=repair order=0 partId=0 trace=legacy_metadata metadata=normalized financialSums=refreshed");
        return true;
    });
}

bool repairLegacyPricingMetadataAndFinancialSums(QSqlDatabase db, QString* errorText)
{
    const auto result = repairLegacyPricingMetadataAndFinancialSumsDetailed(db);
    if (!result.ok && errorText) *errorText = result.errorText;
    return result.ok;
}

bool checkPartPricingIntegrity(QSqlDatabase db,
                               PricingIntegrityCheckResult* result,
                               QString* errorText)
{
    if (!db.isValid()) {
        if (errorText) *errorText = QStringLiteral("Invalid database connection");
        return false;
    }
    if (!db.isOpen() && !db.open()) {
        if (errorText) *errorText = QStringLiteral("Database connection is not open");
        return false;
    }

    PricingIntegrityCheckResult local;
    const auto policySnapshot = PartMarkupPolicyService::loadPolicySnapshot(db);

    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT id, COALESCE(order_number,0), COALESCE(name,''), COALESCE(store,''), COALESCE(qty,1), "
        "COALESCE(price,0), COALESCE(amount,0), COALESCE(purchase_price,0), COALESCE(purchase_amount,0), "
        "COALESCE(markup_percent,0), COALESCE(manual_markup_override,0) "
        "FROM work_order_parts"));
    if (!q.exec()) {
        if (errorText) *errorText = q.lastError().text();
        return false;
    }

    while (q.next()) {
        ++local.checkedRows;
        const int id = q.value(0).toInt();
        const int orderNumber = q.value(1).toInt();
        const QString partName = q.value(2).toString();
        const QString store = q.value(3).toString();
        const double qty = qMax(1.0, q.value(4).toDouble());
        const double storedClientPrice = FormatUtils::roundMoneyValue(qMax(0.0, q.value(5).toDouble()));
        const double storedClientAmount = FormatUtils::roundMoneyValue(qMax(0.0, q.value(6).toDouble()));
        double purchasePrice = FormatUtils::roundMoneyValue(qMax(0.0, q.value(7).toDouble()));
        const double purchaseAmount = FormatUtils::roundMoneyValue(qMax(0.0, q.value(8).toDouble()));
        const double storedPct = qMax(0.0, q.value(9).toDouble());
        const bool manualOverride = q.value(10).toInt() != 0;
        if (manualOverride) {
            ++local.manualRows;
        } else {
            ++local.policyRows;
        }

        if (purchasePrice <= 0.0 && purchaseAmount > 0.0) {
            purchasePrice = FormatUtils::roundMoneyValue(purchaseAmount / qty);
        }

        PartPricingInput pricingInput;
        pricingInput.partName = partName;
        pricingInput.store = store;
        pricingInput.qty = qty;
        pricingInput.purchasePrice = purchasePrice;
        pricingInput.manualMarkupOverride = manualOverride;
        pricingInput.manualMarkupPercent = manualOverride ? storedPct : 0.0;
        pricingInput.sourceType = QStringLiteral("integrity_dry_run");
        const PartPricingResult expected = PartPricingEngine::calculate(pricingInput, policySnapshot);

        const bool okClientPrice = qAbs(storedClientPrice - expected.clientPrice) < 0.01;
        const bool okClientAmount = qAbs(storedClientAmount - expected.clientAmount) < 0.01;
        const bool okPurchaseAmount = purchaseAmount <= 0.009 || qAbs(purchaseAmount - expected.purchaseAmount) < 0.01;
        const bool okPct = qAbs(storedPct - expected.markupPercent) < 0.01;
        if (okClientPrice && okClientAmount && okPurchaseAmount && okPct) {
            continue;
        }

        ++local.mismatchRows;
        if (local.samples.size() < 20) {
            local.samples << QStringLiteral("order=%1 partId=%2 name=\"%3\" store=\"%4\" stored(price=%5 amount=%6 pct=%7 purchase=%8) expected(price=%9 amount=%10 pct=%11 purchase=%12)")
                .arg(orderNumber)
                .arg(id)
                .arg(partName.left(80))
                .arg(store.left(40))
                .arg(storedClientPrice, 0, 'f', 2)
                .arg(storedClientAmount, 0, 'f', 2)
                .arg(storedPct, 0, 'f', 2)
                .arg(purchasePrice, 0, 'f', 2)
                .arg(expected.clientPrice, 0, 'f', 2)
                .arg(expected.clientAmount, 0, 'f', 2)
                .arg(expected.markupPercent, 0, 'f', 2)
                .arg(expected.purchasePrice, 0, 'f', 2);
        }
        qWarning().noquote() << QStringLiteral("PART_PRICING_INVARIANT_VIOLATION op=integrity_dry_run order=%1 partId=%2 fields=client_price,client_amount,purchase_amount,markup_percent")
            .arg(orderNumber)
            .arg(id);
    }

    if (RuntimeLoggingControl::deepDebugEnabled()) qInfo().noquote() << QStringLiteral("PART_PRICING op=integrity_dry_run order=0 partId=0 trace=settings_diagnostics checkedRows=%1 mismatchRows=%2 manualRows=%3 policyRows=%4")
        .arg(local.checkedRows)
        .arg(local.mismatchRows)
        .arg(local.manualRows)
        .arg(local.policyRows);

    if (result) *result = local;
    return true;
}

bool checkPartPricingIntegrity(PricingIntegrityCheckResult* result,
                               QString* errorText)
{
    return checkPartPricingIntegrity(Database::db(), result, errorText);
}

} // namespace PricingRecalculationService
