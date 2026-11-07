#pragma once

#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QMap>

#include <functional>

namespace InvoiceImport {

struct ImportedPartDraft {
    QString orderedAt;
    QString oemNumber;
    QString name;
    QString brandName;
    QString qtyText;
    QString priceText;
    QString amountText;
    QString store;
    QString deliveryDate; // Explicitly confirmed in the invoice preview.
    QString orderNumberText;
};

struct InvoiceImportMeta {
    QString documentNumber;
    QString documentDate; // ISO yyyy-MM-dd when recognized

    bool hasAny() const { return !documentNumber.trimmed().isEmpty() || !documentDate.trimmed().isEmpty(); }
};

struct InvoiceColumnMappingPreview {
    QString templateKey;
    bool templateApplied = false;
    bool splitCombinedDescription = true;
    int headerRow = -1;
    int dateColumn = -1;
    int articleColumn = -1;
    int partNameColumn = -1;
    int brandColumn = -1;
    int quantityColumn = -1;
    int priceWithVatColumn = -1;
    int totalWithVatColumn = -1;
    int supplierColumn = -1;
    int supplierScore = 0;
    int headerScore = 0;
    int dataTypeScore = 0;
    int numericConsistencyScore = 0;
    int vatPriceScore = 0;
    int totalConfidence = 0;
    bool priceWithVatConfirmed = false;
    bool automaticAllowed = false;
    bool manualPreviewRequired = false;
    bool manualColumnSelectionRequired = false;
    QString supplierKey;
    QString supplierDisplayName;
    QStringList warnings;
    QMap<QString, int> columnConfidence;
    QMap<int, int> physicalColumnConfidence;
    QMap<int, QString> physicalColumnWarnings;
};

struct UniversalInvoiceDetectionResult {
    InvoiceImportMeta meta;
    InvoiceColumnMappingPreview columnMappingPreview;
    QList<ImportedPartDraft> parsedPartDrafts;
    QString automaticImportError;
    bool performed = false;
    bool automaticImportAllowed = false;
    bool manualMappingRequired = false;

    bool ok() const { return performed && automaticImportAllowed && automaticImportError.trimmed().isEmpty() && !parsedPartDrafts.isEmpty(); }
};

struct ExcelReadResult {
    QList<QStringList> rows;
    QSet<int> highlightedColumns;
    QString documentNumber;
    QString documentDate; // ISO yyyy-MM-dd when recognized

    // Background result of UniversalInvoiceColumnDetector. Filled by ImportJob worker,
    // so UI callbacks do not run header detection, VAT checks or automatic row parsing.
    bool columnDetectionPerformed = false;
    InvoiceColumnMappingPreview columnMappingPreview;
    QList<ImportedPartDraft> parsedPartDrafts;
    QString automaticImportError;
    bool automaticImportAllowed = false;
    bool manualMappingRequired = false;
    bool unsupportedDocument = false;

    // Worker-prepared canonical PDF for spreadsheet-originated invoices.
    // UI code must store/open this path instead of the original .xls/.xlsx/.csv file.
    QString canonicalPdfAttachmentPath;
    QString canonicalPdfAttachmentError;

    bool hasHighlightedColumns() const { return !highlightedColumns.isEmpty(); }
    bool hasDocumentMeta() const { return !documentNumber.trimmed().isEmpty() || !documentDate.trimmed().isEmpty(); }
};

using NormalizeTextFn = std::function<QString(const QString&)>;
using ParseLooseNumberFn = std::function<double(QString, bool*)>;
using MoneyTextFn = std::function<QString(double)>;

} // namespace InvoiceImport
