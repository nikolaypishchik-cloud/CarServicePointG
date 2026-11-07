#include "ui/tabs/orders/WorkPartsImportedRowsViewModel.h"

#include <QDateTime>
#include <QFileInfo>

namespace WorkPartsImportedRowsViewModel {


static bool isSpreadsheetInvoiceSource(const QString& filePath)
{
    const QString ext = QFileInfo(filePath).suffix().trimmed().toLower();
    return ext == QStringLiteral("xlsx") || ext == QStringLiteral("xls") || ext == QStringLiteral("xlsm") ||
           ext == QStringLiteral("xltx") || ext == QStringLiteral("xltm") || ext == QStringLiteral("csv") ||
           ext == QStringLiteral("txt");
}

ImportBatch buildBatch(int orderNumber,
                       const QList<WorkPartsDialogExcelSupport::ImportedPartDraft>& drafts,
                       const WorkPartsDialogExcelSupport::ExcelReadResult& excelData,
                       const QString& filePath,
                       const QString& invoicePdfPath)
{
    ImportBatch batch;
    batch.filePath = filePath;
    batch.invoicePdfPath = invoicePdfPath.trimmed();
    batch.documentNumber = excelData.documentNumber.trimmed();
    batch.documentDate = excelData.documentDate.trimmed();
    batch.rows.reserve(drafts.size());

    const bool spreadsheetSource = isSpreadsheetInvoiceSource(filePath);
    if (spreadsheetSource && batch.invoicePdfPath.isEmpty()) return batch;
    const QString attachmentSource = batch.invoicePdfPath.isEmpty()
        ? (spreadsheetSource ? QString() : filePath)
        : batch.invoicePdfPath;
    const QString attachmentPath = batch.invoicePdfPath;
    const QString attachmentName = attachmentSource.trimmed().isEmpty() ? QString() : QFileInfo(attachmentSource).fileName();
    const QString traceBase = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMddHHmmsszzz"));

    int importedDraftIndex = 0;
    for (const auto& draft : drafts) {
        ImportedPartRow row;
        row.ui = PartImportDraftUiSupport::buildUiRow(draft);
        row.traceId = QStringLiteral("invoice:%1:%2:%3")
                          .arg(orderNumber)
                          .arg(traceBase)
                          .arg(++importedDraftIndex);
        row.attachmentSource = attachmentSource;
        row.attachmentPath = attachmentPath;
        row.attachmentName = attachmentName;
        row.documentNumber = batch.documentNumber;
        row.documentDate = batch.documentDate;
        batch.rows.push_back(row);
    }

    return batch;
}

} // namespace WorkPartsImportedRowsViewModel
