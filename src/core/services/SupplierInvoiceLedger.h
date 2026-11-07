#pragma once
#include "core/services/ElitInvoiceStore.h"
#include "core/services/PurchasePaymentService.h"
#include "core/services/SupplierDebtReconciliationService.h"

namespace SupplierInvoiceLedger {
struct LocalItem {
    PurchaseDebt purchase;
    QString key, document;
    QDate documentDate;
    bool attachment = false;
    bool apportioned = false;
    QVector<int> orders;
};
struct LineAllocation {
    int line = 0;
    QString purchaseKey;
    double quantity = 0;
    qint64 totalCents = 0;
};
struct Entry {
    QString key, supplier, number, currency = "UAH", reason;
    QDate date, due;
    qint64 total = 0, remaining = 0, localRemaining = 0;
    bool remote = false, isNew = false, matched = false, discrepancy = false, counted = true;
    bool missingFromLatest = false;
    QVector<ElitInvoiceReader::Line> lines;
    QVector<LocalItem> items;
    QVector<QVector<QString>> links;
    QVector<LineAllocation> allocations;
};
struct Ledger {
    QVector<Entry> entries;
    QVector<LocalItem> local;
    ElitInvoiceStore::Snapshot snapshot;
    QString error;
    bool hasUnresolved = false;
};
QString normalizedArticle(QString value);
bool isElit(const QString& supplier);
QVector<LocalItem> localItems(QSqlDatabase db, QString* error);
Ledger load(QSqlDatabase db, const QString& account);
QVector<LocalItem> linkCandidates(const Ledger& ledger, const Entry& entry, int line);
// Only stores links to existing purchases. Never writes a purchase or payment.
bool confirmLinks(QSqlDatabase db, const QString& account, const QString& revision,
                  const QString& invoiceIdentity, const QVector<QVector<QString>>& links, QString* error);
qint64 debtTotal(const Ledger& ledger);
struct Summary {
    QMap<QString,qint64> totals;
    int count = 0;
    bool remote = false, unresolved = false;
    QSet<QString> manualSuppliers;
};
Summary summarize(const Ledger& ledger, const QVector<PurchaseDebt>& debts,
                  const QMap<QString,SupplierDebtReconciliation>& reconciliations);
Summary summary(QSqlDatabase db, const QVector<PurchaseDebt>& debts,
                const QMap<QString,SupplierDebtReconciliation>& reconciliations, QString* error);
// Reporting only: never pass remote rows to payment commands.
QVector<PurchaseDebt> reportingDebts(QSqlDatabase db, const QVector<PurchaseDebt>& local,
    QMap<QString,SupplierDebtReconciliation>* facts, QString* error);
bool hasRemoteBalances(const Ledger& ledger);
// Publishes observations and unambiguous links in one transaction; no payment writes.
bool publishAndLink(QSqlDatabase db, const QString& account, const ElitInvoiceReader::Result& result,
                    const QString& revision, const std::function<bool()>& stillCurrent,
                    int* linked, int* unresolved, QString* error);
struct LinkedDocument {
    QString identity, number, date;
    double quantity = 0;
    qint64 total = 0, paid = 0;
    QString filePath;
};
struct LinkedInvoice {
    QString number, date;
    bool apportioned = false;
    QVector<LinkedDocument> documents;
    double purchaseQuantity = 0;
    qint64 purchaseTotal = 0;
    QString filePath;
};
QHash<QString,LinkedInvoice> savedLinks(QSqlDatabase db, const QString& account);
QHash<QString,LinkedInvoice> activeLinks(QSqlDatabase db);
}

namespace SupplierInvoiceLedger {
// UI entry points: connection and transaction ownership remain in the service.
QHash<QString,LinkedInvoice> activeLinks();
}
