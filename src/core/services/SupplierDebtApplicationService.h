#pragma once
#include "core/services/ElitDocumentAssociation.h"
#include <atomic>
#include <functional>

namespace SupplierDebtApplicationService {
bool canRead();
bool canReconcile();
using ReadTask = std::function<void(SupplierInvoiceLedger::Ledger&, SupplierInvoiceLedger::Summary&, const std::atomic_bool&)>;
// Capture on the UI thread; execute on the worker. No database handle crosses threads.
ReadTask prepareRead(const QString& account);
QVector<ElitDocumentAssociation::Candidate> candidates(const QString& account, const QString& scope, QString* error);
bool confirmDocuments(const QString& account, const QString& scope,
                      const QVector<ElitDocumentAssociation::Candidate>& selected, QString* error);
bool confirmLinks(const QString& account, const QString& scope, const QString& revision,
                  const QString& invoice, const QVector<QVector<QString>>& links, QString* error);
}
