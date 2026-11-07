// P2 Logging and crash diagnostics: structured WorkPartsDialog event bridge.
// Keeps UI code on a single helper instead of scattering ad-hoc information traces.

#include "ui/tabs/orders/WorkPartsDialogModulePrelude.h"

#include "core/diagnostics/StructuredDiagnosticsLogger.h"
#include "core/diagnostics/RuntimeLoggingControl.h"

#include <QSet>
#include <QStringList>

namespace {

QTableWidget* diagnosticTableForEntity(const WorkPartsDialog* dialog, const QString& entityType)
{
    if (!dialog) return nullptr;
    const QString normalized = entityType.trimmed().toLower();
    if (normalized.contains(QStringLiteral("part")) || normalized.contains(QStringLiteral("invoice"))) {
        return const_cast<QTableWidget*>(dialog->findChild<QTableWidget*>(QStringLiteral("orderPartsTable")));
    }
    if (normalized.contains(QStringLiteral("service"))) {
        return const_cast<QTableWidget*>(dialog->findChild<QTableWidget*>(QStringLiteral("orderServicesTable")));
    }
    return nullptr;
}


bool shouldLogWorkPartsDiagnosticInProduction(const QString& operation)
{
    if (RuntimeLoggingControl::deepDebugEnabled()) return true;

    const QString op = operation.trimmed().toLower();
    if (op.isEmpty()) return false;

    // Production mode keeps only events required to diagnose data loss, failed
    // persistence, failed imports, rollbacks, timeouts and crash-recovery paths.
    // High-frequency lifecycle/model/editor/click/row events stay available only
    // with CS_DEEP_DEBUG=1. This removes WORKPARTS_DIAGNOSTIC spam from normal
    // app.log while preserving actionable failure breadcrumbs.
    static const QStringList criticalNeedles = {
        QStringLiteral("fail"),
        QStringLiteral("error"),
        QStringLiteral("rollback"),
        QStringLiteral("timeout"),
        QStringLiteral("crash"),
        QStringLiteral("exception"),
        QStringLiteral("lost"),
        QStringLiteral("corrupt"),
        QStringLiteral("recovery"),
        QStringLiteral("transaction_rollback"),
        QStringLiteral("import_end_parse_failed"),
        QStringLiteral("import_end_empty"),
        QStringLiteral("attachment_save_fail"),
        QStringLiteral("editor_commit_close")
    };
    for (const QString& needle : criticalNeedles) {
        if (op.contains(needle)) return true;
    }
    return false;
}

} // namespace

QString WorkPartsDialog::workPartsRowUid(QTableWidget* table, int row) const
{
    if (!table || row < 0 || row >= table->rowCount()) return QString();

    const bool isPartsTable = table == m_partsTable || table->objectName() == QStringLiteral("orderPartsTable");
    const int idColumn = isPartsTable ? PartColName : ServiceColTitle;
    const int invoiceColumn = isPartsTable ? PartColInvoiceNumber : -1;

    if (isPartsTable) {
        const QString importTrace = importedPartTraceId(table, row).trimmed();
        if (!importTrace.isEmpty()) return QStringLiteral("import:%1").arg(importTrace);
    }

    if (auto* idItem = table->item(row, idColumn)) {
        const int id = idItem->data(RoleId).toInt();
        if (id > 0) {
            return QStringLiteral("%1:%2").arg(isPartsTable ? QStringLiteral("part") : QStringLiteral("service")).arg(id);
        }
    }

    if (invoiceColumn >= 0) {
        if (auto* invoiceItem = table->item(row, invoiceColumn)) {
            const QString invoiceNumber = invoiceItem->data(RoleAttachmentDocumentNumber).toString().trimmed();
            if (!invoiceNumber.isEmpty()) return QStringLiteral("invoice:%1:row:%2").arg(invoiceNumber).arg(row);
        }
    }

    return QStringLiteral("%1:ui-row:%2")
        .arg(isPartsTable ? QStringLiteral("part") : QStringLiteral("service"))
        .arg(row);
}

quint64 WorkPartsDialog::bumpWorkPartsTableRevision(const QString& operation)
{
    ++m_tableRevision;
    invalidateWorkPartsRowHeightCache();
    setProperty("csWorkPartsTableRevision", QVariant::fromValue<qulonglong>(m_tableRevision));
    if (m_servicesTable) m_servicesTable->setProperty("csWorkPartsTableRevision", QVariant::fromValue<qulonglong>(m_tableRevision));
    if (m_partsTable) m_partsTable->setProperty("csWorkPartsTableRevision", QVariant::fromValue<qulonglong>(m_tableRevision));
    logWorkPartsDiagnostic(QStringLiteral("table_revision_bump"),
                           QStringLiteral("model"),
                           -1,
                           -1,
                           QString(),
                           {{QStringLiteral("reason"), operation}});
    return m_tableRevision;
}

void WorkPartsDialog::logWorkPartsDiagnostic(const QString& operation,
                                             const QString& entityType,
                                             int row,
                                             int entityId,
                                             const QString& invoiceNumber,
                                             const QVariantMap& details) const
{
    if (!shouldLogWorkPartsDiagnosticInProduction(operation)) {
        return;
    }

    StructuredDiagnosticsLogger::Event event;
    event.module = QStringLiteral("ui.tabs.orders.WorkPartsDialog");
    event.operation = operation;
    event.orderNumber = m_orderNumber;
    event.orderUid = currentOrderUid(nullptr);
    event.partId = entityType.contains(QStringLiteral("part"), Qt::CaseInsensitive) ? entityId : -1;
    event.invoiceNumber = invoiceNumber.trimmed();
    event.transactionId = property("csActiveTransactionId").toString().trimmed();
    event.tableRevision = static_cast<qint64>(m_tableRevision);
    event.correlationId = QStringLiteral("workparts:%1:%2:%3")
        .arg(m_orderNumber)
        .arg(operation.trimmed().isEmpty() ? QStringLiteral("operation") : operation.trimmed())
        .arg(static_cast<qulonglong>(m_tableRevision));

    QTableWidget* table = diagnosticTableForEntity(this, entityType);
    event.rowUid = workPartsRowUid(table, row);

    QVariantMap fullDetails = details;
    if (!entityType.trimmed().isEmpty()) fullDetails.insert(QStringLiteral("entityType"), entityType);
    if (row >= 0) fullDetails.insert(QStringLiteral("row"), row);
    if (entityId > 0) fullDetails.insert(QStringLiteral("entityId"), entityId);
    if (table) fullDetails.insert(QStringLiteral("table"), table->objectName());
    fullDetails.insert(QStringLiteral("dirtyServiceRows"), m_dirtyServiceRows.size());
    fullDetails.insert(QStringLiteral("dirtyPartRows"), m_dirtyPartRows.size());
    fullDetails.insert(QStringLiteral("activeServiceId"), m_activeServiceId);
    fullDetails.insert(QStringLiteral("commitInProgress"), m_commitPendingInProgress);
    fullDetails.insert(QStringLiteral("importBusy"), property("csImportBusy").toBool());
    fullDetails.insert(QStringLiteral("importApplyingRows"), property("csImportApplyingRows").toBool());

    event.details = fullDetails;
    StructuredDiagnosticsLogger::log(StructuredDiagnosticsLogger::Level::Info, event);
}
