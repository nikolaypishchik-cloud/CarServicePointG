#pragma once
#include "core/services/SupplierInvoiceLedger.h"

// Document evidence only. This service never allocates money or settles debt.
namespace ElitDocumentAssociation {
struct Candidate {
    QString purchaseKey, invoiceId, number, fingerprint;
    int line = 0, orderNumber = 0;
    QString name, article, invoiceName, invoiceArticle;
    QDate partDate, invoiceDate;
    double quantity = 0, availableQuantity = 0;
    bool exact = false;
};
QVector<Candidate> candidates(QSqlDatabase db, const QString& account, QString* error = nullptr);
bool confirm(QSqlDatabase db, const QString& account, const QVector<Candidate>& selected,
             bool automatic, QString* error = nullptr);
bool autoLink(QSqlDatabase db, const QString& account, int* count, QString* error = nullptr);
QHash<QString,SupplierInvoiceLedger::LinkedInvoice> links(QSqlDatabase db, const QString& account);
}
