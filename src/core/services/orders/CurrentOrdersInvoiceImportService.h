#pragma once

#include "core/services/MutationResult.h"
#include "core/services/OrderSearchRepository.h"

#include <QList>
#include <QHash>
#include <QString>
#include <functional>

class QSqlDatabase;

struct CurrentOrdersImportServiceRow {
    QString orderedAt;
    QString deliveryDate;
    QString orderNumberText;
    QString store;
    QString oemNumber;
    QString name;
    QString brandName;
    QString qtyText;
    QString priceText;
    QString orderUid; // Explicit owner selected in the review form; never inferred from invoice date.
};

struct CurrentOrdersInvoiceUnifiedRow {
    int sourceRowIndex = -1;
    QString sourceKind;
    QString invoiceNumber;
    QString invoiceDate;
    QString orderedAt;
    QString deliveryDate;
    QString orderNumberText;
    QString store;
    QString oemNumber;
    QString name;
    QString brandName;
    QString qtyText;
    QString priceText;

    QString compactLogText() const;
};

struct CurrentOrdersInvoiceImportResult {
    QList<int> affectedOrderNumbers;
};

struct CurrentOrdersInvoiceAttachmentSpec {
    QString sourceFilePath;
    QString displayName;
    QString storeName;
    QString invoiceNumber;
    QString invoiceDate;

    bool hasAttachment() const { return !sourceFilePath.trimmed().isEmpty(); }
    QString importOperationId; // Explicitly different IDs allow intentional re-imports.
};

class CurrentOrdersInvoiceImportService {
public:
    static QList<OrderSearchHit> findOrderCandidates(int visibleNumber, QString* errorText = nullptr);

    using AttachmentHandler = std::function<bool(QSqlDatabase&, int, int, QString*)>;

    static MutationResult importRowsDetailed(const QList<CurrentOrdersImportServiceRow>& rows,
                                             const AttachmentHandler& attachmentHandler,
                                             CurrentOrdersInvoiceImportResult* result = nullptr,
                                             QString* errorText = nullptr);

    static bool importRows(const QList<CurrentOrdersImportServiceRow>& rows,
                           const AttachmentHandler& attachmentHandler,
                           CurrentOrdersInvoiceImportResult* result,
                           QString* errorText = nullptr);

    static MutationResult importRowsDetailed(const QList<CurrentOrdersImportServiceRow>& rows,
                                             const CurrentOrdersInvoiceAttachmentSpec& attachmentSpec,
                                             CurrentOrdersInvoiceImportResult* result = nullptr,
                                             QString* errorText = nullptr);

    static bool importRows(const QList<CurrentOrdersImportServiceRow>& rows,
                           const CurrentOrdersInvoiceAttachmentSpec& attachmentSpec,
                           CurrentOrdersInvoiceImportResult* result,
                           QString* errorText = nullptr);
private:
    static MutationResult importRowsDetailedImpl(const QList<CurrentOrdersImportServiceRow>& rows,
                                                const AttachmentHandler& attachmentHandler,
                                                CurrentOrdersInvoiceImportResult* result,
                                                QString* errorText,
                                                const QString& receiptKey,
                                                const QByteArray& sourceDigest = {});
};
