#pragma once

#include "core/import/InvoiceImportParsing.h"
#include <QList>
#include <QStringList>
#include <memory>

class QAbstractItemModel;
class QAbstractItemDelegate;
class QSortFilterProxyModel;
class QObject;
class QWidget;
class QMenu;
class InvoiceReviewDialog;

namespace CurrentOrdersSupport {
using InvoiceImport::ImportedPartDraft;
using InvoiceImport::ExcelReadResult;
int modelColumnByHeader(const QAbstractItemModel* model, const QString& header);
void publishCurrentOrdersBackupEvent(const QString& operationType, int orderNumber,
    const QString& entityType, const QString& entityId, const QString& description,
    const QStringList& changedTables);
bool confirmCurrentOrdersInvoiceColumnMapping(QWidget* parent, const QList<QStringList>& rows,
    const InvoiceImport::InvoiceColumnMappingPreview& preview, const QString& sourceFilePath,
    InvoiceImport::InvoiceColumnMappingPreview* confirmedMapping);
QAbstractItemDelegate* createWordStyleDelegate(QObject* parent);
QSortFilterProxyModel* createDisplayProxy(QObject* parent);
std::unique_ptr<InvoiceReviewDialog> createInvoiceReview(const QString& sourceFilePath,
    const ExcelReadResult& data, const QList<ImportedPartDraft>& drafts, QWidget* parent);
bool reviewInvoice(const QString& sourceFilePath, const ExcelReadResult& data,
    const QList<ImportedPartDraft>& drafts, QWidget* parent);
void prepareActionMenu(QMenu& menu);
}

