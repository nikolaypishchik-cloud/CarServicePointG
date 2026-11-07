#pragma once

#include "ui/tabs/orders/PartImportDraftUiSupport.h"
#include "ui/tabs/orders/WorkPartsDialogExcelSupport.h"

#include <QList>
#include <QString>
#include <QVector>

namespace WorkPartsImportedRowsViewModel {

struct ImportedPartRow
{
    PartImportDraftUiSupport::UiRow ui;
    QString traceId;
    QString attachmentSource;
    QString attachmentPath;
    QString attachmentName;
    QString documentNumber;
    QString documentDate;
};

struct ImportBatch
{
    QString filePath;
    QString invoicePdfPath;
    QString documentNumber;
    QString documentDate;
    QVector<ImportedPartRow> rows;

    bool isValid() const { return !rows.isEmpty(); }
};

ImportBatch buildBatch(int orderNumber,
                       const QList<WorkPartsDialogExcelSupport::ImportedPartDraft>& drafts,
                       const WorkPartsDialogExcelSupport::ExcelReadResult& excelData,
                       const QString& filePath,
                       const QString& invoicePdfPath);

} // namespace WorkPartsImportedRowsViewModel
