#pragma once
#include "core/services/SupplierInvoiceLedger.h"
#include "core/services/InvoiceEvidenceService.h"
#include "core/services/PartsQueryService.h"
#include <atomic>
#include <functional>
namespace SupplierDebtReadService {
struct Snapshot {
    QVector<PurchaseDebt> debts;
    QVector<PartRowSnapshot> parts;
    QMap<QString,SupplierDebtReconciliation> reconciliations;
    QHash<QString,InvoiceEvidence> evidence;
    SupplierInvoiceLedger::Summary ledger;
    QString error;
    bool restricted=false;
};
// Capture identity/path on caller thread; each invocation owns a read-only transaction.
using Task=std::function<Snapshot(const std::atomic_bool&)>;
Task prepare(bool analysis=false);
using OrderPaymentTask=std::function<PurchasePaymentService::OrderPaymentPreparation(const std::atomic_bool&,QString*)>;
OrderPaymentTask prepareOrderPayment(int orderNumber);
using PaymentTask=std::function<PurchaseDebt(const std::atomic_bool&,QString*)>;
PaymentTask preparePayment(const QString& source,qint64 id,const QString& uid);
using BannerTask=std::function<QMap<QString,qint64>(const std::atomic_bool&,QString*)>;
BannerTask prepareBanner();
struct ReconciliationInput {
    QStringList names;
    QVector<PurchaseDebt> debts;
    QMap<QString,SupplierDebtReconciliation> previous;
    SupplierInvoiceLedger::Summary summary;
    QString error;
};
ReconciliationInput reconciliationInput();
using ReconciliationTask=std::function<ReconciliationInput(const std::atomic_bool&)>;
ReconciliationTask prepareReconciliation();
bool saveReconciliation(const QVector<SupplierDebtReconciliation>& entries,QString* error);
}
