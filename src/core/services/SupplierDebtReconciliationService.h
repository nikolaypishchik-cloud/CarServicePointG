#pragma once
#include <QMap>
#include <QStringList>
#include <QSqlDatabase>
#include <QVector>
struct PurchaseDebt;

struct SupplierDebtReconciliation {
    QString supplier, revision, checkedAt, actor;
    qint64 computedCents=0, actualCents=0;
    bool differs() const { return computedCents!=actualCents; }
};

namespace SupplierDebtReconciliationService {
bool canReconcile();

bool canReconcile(QSqlDatabase db);
QStringList suppliers(QSqlDatabase db, QString* error=nullptr);
QMap<QString,SupplierDebtReconciliation> load(QSqlDatabase db, QString* error=nullptr);
// Apply ledger changes since the reconciliation without changing payment records.
QMap<QString,qint64> effectiveTotals(const QVector<PurchaseDebt>& debts,
    const QMap<QString,SupplierDebtReconciliation>& reconciliations);
// Optimistic revision and balance checks prevent overwriting concurrent edits.
bool save(QSqlDatabase db, const QVector<SupplierDebtReconciliation>& entries, QString* error=nullptr);
}
