#include "core/services/MainWindowMetricsService.h"

#include "infrastructure/db/sqlite/Db.h"
#include "core/services/AccessControlService.h"
#include "core/services/PurchasePaymentService.h"
#include "core/services/SupplierDebtReconciliationService.h"
#include "core/services/SupplierInvoiceLedger.h"

namespace MainWindowMetricsService {

bool loadAnimationsEnabled()
{
    return Database::getSetting(QStringLiteral("settings.ui.animations"), true).toBool();
}

int loadAnimationDurationMs()
{
    return Database::getSetting(QStringLiteral("settings.ui.anim_duration_ms"), 180).toInt();
}

bool loadUnpaidPurchasesSummary(int& countOut, double& sumOut)
{
    return loadUnpaidPurchasesSummary(countOut, sumOut, AccessControlService::currentOrderScope(Database::db()));
}

bool loadUnpaidPurchasesSummary(int& countOut, double& sumOut, const AccessControlService::OrderScope& scope, bool* unresolved)
{
    if(unresolved) *unresolved=false;
    if (scope.restricted) {
        countOut = 0;
        qint64 cents = 0;
        QString error;
        const auto db = Database::db();
        for (const auto& debt : PurchasePaymentService::debts(db, &error)) {
            if (debt.source == QStringLiteral("inventory") || !scope.contains(debt.orderUid, debt.orderNumber)) continue;
            ++countOut;
            cents += debt.remainingCents();
        }
        sumOut = cents / 100.0;
        return error.isEmpty();
    }
    QString error;
    const auto db=Database::db();
    const auto debts=PurchasePaymentService::debts(db,&error);
    if(!error.isEmpty()) return false;
    const auto reconciliations=SupplierDebtReconciliationService::load(db,&error);
    if(!error.isEmpty()) return false;
    const auto summary=SupplierInvoiceLedger::summary(db,debts,reconciliations,&error);
    if(!error.isEmpty()) return false;
    qint64 cents=0;
    if(unresolved) *unresolved=summary.unresolved;
    for(auto value:summary.totals) cents+=value;
    countOut=summary.count; sumOut=cents/100.0;
    return true;
}

} // namespace MainWindowMetricsService
