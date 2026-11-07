#pragma once
#include <QHash>
#include <QString>
#include <QVector>
#include <QSqlDatabase>
#include "core/services/PartsQueryService.h"

struct InvoiceEvidence {
    QString documentHash, invoiceNumber, invoiceDate, supplier, name, article, filePath;
    double quantity = 0;
    qint64 purchaseCents = 0;
};

namespace InvoiceEvidenceService {
QString key(const QString& orderUid, int partId);
// Called within the attachment mutation transaction, never from a parsing preview.
bool capture(QSqlDatabase db, int partId, const QString& path, const QString& number,
             const QString& date, QString* error, double confirmedAmount = -1);
bool deactivate(QSqlDatabase db, int partId, QString* error);
QHash<QString, InvoiceEvidence> load(QSqlDatabase db, const QVector<PartRowSnapshot>& visibleParts,
                                   QString* error = nullptr);
}
