#include "core/services/InvoiceReportService.h"
// P2 architecture migration: implementation module converted from legacy .ipp implementation-fragment decomposition.
// Compiled as a separate P2 implementation module; include-order dependencies are isolated in the module prelude.

// extracted from WorkPartsDialog.cpp as part of p.4 decomposition
#include "ui/tabs/orders/WorkPartsDialogModulePrelude.h"
#include "ui/common/dialogs/InvoiceDocumentOpenSupport.h"
#include "core/services/InvoiceRegistryService.h"

void WorkPartsDialog::openServiceAttachmentDialog(int row)
{
    if (!m_servicesTable || row < 0 || row >= m_servicesTable->rowCount()) return;
    auto* titleItem = m_servicesTable->item(row, ServiceColTitle);
    if (!titleItem || isSpacerItem(titleItem)) return;
    auto* attachmentItem = m_servicesTable->item(row, ServiceColAttachment);
    if (!attachmentItem) {
        attachmentItem = makeAttachmentItem(QString());
        m_servicesTable->setItem(row, ServiceColAttachment, attachmentItem);
    }

    int serviceId = titleItem->data(RoleId).toInt();
    logWorkPartsDiagnostic(QStringLiteral("attachment_dialog_open"), QStringLiteral("service"), row, serviceId);
    QString currentPath;
    if (serviceId > 0) {
        currentPath = WorkPartsReadQueryService::serviceAttachmentPath(serviceId);
    } else {
        currentPath = attachmentItem->data(RoleAttachmentSource).toString().trimmed();
        if (currentPath.isEmpty()) currentPath = attachmentItem->data(RoleAttachmentPath).toString().trimmed();
    }

    QString selectedSource;
    const auto action = runAttachmentPreviewDialog(this, QString::fromUtf8("Фото роботи"), currentPath, &selectedSource);
    if (action == AttachmentDialogAction::None) {
        logWorkPartsDiagnostic(QStringLiteral("attachment_dialog_close"), QStringLiteral("service"), row, serviceId, QString(), {{QStringLiteral("action"), QStringLiteral("none")}});
        return;
    }

    if (serviceId <= 0) {
        const bool hasAttachment = action != AttachmentDialogAction::RemoveFile && !selectedSource.trimmed().isEmpty();
        if (!hasAttachment) {
            attachmentItem->setData(RoleAttachmentPath, QString());
            attachmentItem->setData(RoleAttachmentName, QString());
            attachmentItem->setData(RoleAttachmentSource, QString());
            attachmentItem->setToolTip(QString::fromUtf8("Натисніть, щоб додати фото"));
        } else {
            attachmentItem->setData(RoleAttachmentPath, selectedSource);
            attachmentItem->setData(RoleAttachmentName, QFileInfo(selectedSource).fileName());
            attachmentItem->setData(RoleAttachmentSource, selectedSource);
            attachmentItem->setToolTip(QString::fromUtf8("Натисніть, щоб переглянути фото"));
        }
        attachmentItem->setText(hasAttachment ? QString::fromUtf8("📎") : QString());
        attachmentItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        attachmentItem->setTextAlignment(Qt::AlignCenter);
        return;
    }

    QString errorText;
    if (action == AttachmentDialogAction::RemoveFile) {
        if (!WorkPartsMutationService::setServiceAttachment(serviceId, QString(), QString(), &errorText)) {
            logWorkPartsDiagnostic(QStringLiteral("attachment_save_fail"), QStringLiteral("service"), row, serviceId, QString(), {{QStringLiteral("action"), QStringLiteral("remove")}, {QStringLiteral("error"), errorText}});
            QMessageBox::critical(this, QString::fromUtf8("Помилка"), errorText);
            return;
        }
        logWorkPartsDiagnostic(QStringLiteral("attachment_save_end"), QStringLiteral("service"), row, serviceId, QString(), {{QStringLiteral("action"), QStringLiteral("remove")}});
        publishWorkPartsBackupEvent(QStringLiteral("order.attachments"),
                                    QStringLiteral("service_attachment"),
                                    QString::number(serviceId),
                                    currentOrderUid(nullptr),
                                    m_orderNumber,
                                    QStringLiteral("service_attachment_remove"),
                                    QString::fromUtf8("Видалено вкладення роботи"),
                                    {QStringLiteral("service_attachments")} );
    } else {
        logWorkPartsDiagnostic(QStringLiteral("attachment_stage"), QStringLiteral("service"), row, serviceId, QString(), {{QStringLiteral("phase"), QStringLiteral("file_copy_begin")}, {QStringLiteral("source"), QFileInfo(selectedSource).fileName()}});
        const QString storedPath = copiedAttachmentPath(m_orderNumber, QStringLiteral("services"), serviceId, selectedSource, &errorText);
        if (storedPath.trimmed().isEmpty()) {
            logWorkPartsDiagnostic(QStringLiteral("attachment_save_fail"), QStringLiteral("service"), row, serviceId, QString(), {{QStringLiteral("phase"), QStringLiteral("file_copy")}, {QStringLiteral("error"), errorText}});
            QMessageBox::critical(this, QString::fromUtf8("Помилка"), errorText); return;
        }
        if (!WorkPartsMutationService::setServiceAttachment(serviceId, storedPath, QFileInfo(selectedSource).fileName(), &errorText)) {
            logWorkPartsDiagnostic(QStringLiteral("attachment_save_fail"), QStringLiteral("service"), row, serviceId, QString(), {{QStringLiteral("phase"), QStringLiteral("metadata")}, {QStringLiteral("error"), errorText}});
            QMessageBox::critical(this, QString::fromUtf8("Помилка"), errorText);
            return;
        }
        logWorkPartsDiagnostic(QStringLiteral("attachment_save_end"), QStringLiteral("service"), row, serviceId, QString(), {{QStringLiteral("path"), QFileInfo(storedPath).fileName()}});
        publishWorkPartsBackupEvent(QStringLiteral("order.attachments"),
                                    QStringLiteral("service_attachment"),
                                    QString::number(serviceId),
                                    currentOrderUid(nullptr),
                                    m_orderNumber,
                                    QStringLiteral("service_attachment_save"),
                                    QString::fromUtf8("Збережено вкладення роботи"),
                                    {QStringLiteral("service_attachments")} );
    }
    refreshServiceAttachmentCell(row);
}

void WorkPartsDialog::openPartAttachmentDialog(int row)
{
    if (!m_partsTable || row < 0 || row >= m_partsTable->rowCount()) return;
    auto* nameItem = m_partsTable->item(row, PartColName);
    if (!nameItem || isSpacerItem(nameItem)) return;
    int partId = nameItem->data(RoleId).toInt();
    if (partId <= 0) {
        QString saveError;
        if (!savePartRow(row, &saveError, false)) {
            if (!saveError.trimmed().isEmpty()) QMessageBox::critical(this, QString::fromUtf8("Помилка"), saveError);
            return;
        }
        partId = nameItem->data(RoleId).toInt();
        if (partId <= 0) return;
    }
    const QString currentPath = WorkPartsReadQueryService::partAttachmentPath(partId);
    const QString attachmentInvoiceNumber = m_partsTable && m_partsTable->item(row, PartColInvoiceNumber)
        ? m_partsTable->item(row, PartColInvoiceNumber)->data(RoleAttachmentDocumentNumber).toString().trimmed()
        : QString();
    logWorkPartsDiagnostic(QStringLiteral("attachment_dialog_open"), QStringLiteral("part"), row, partId, attachmentInvoiceNumber);
    QString selectedSource;
    const auto action = runAttachmentPreviewDialog(this, QString::fromUtf8("Файл запчастини / накладна"), currentPath, &selectedSource);
    if (action == AttachmentDialogAction::None) {
        logWorkPartsDiagnostic(QStringLiteral("attachment_dialog_close"), QStringLiteral("part"), row, partId, attachmentInvoiceNumber, {{QStringLiteral("action"), QStringLiteral("none")}});
        return;
    }
    QString errorText;
    if (action == AttachmentDialogAction::RemoveFile) {
        if (!WorkPartsMutationService::setPartAttachment(partId, QString(), QString(), QString(), QString(), &errorText)) {
            logWorkPartsDiagnostic(QStringLiteral("attachment_save_fail"), QStringLiteral("part"), row, partId, attachmentInvoiceNumber, {{QStringLiteral("action"), QStringLiteral("remove")}, {QStringLiteral("error"), errorText}});
            QMessageBox::critical(this, QString::fromUtf8("Помилка"), errorText);
            return;
        }
        logWorkPartsDiagnostic(QStringLiteral("attachment_save_end"), QStringLiteral("part"), row, partId, attachmentInvoiceNumber, {{QStringLiteral("action"), QStringLiteral("remove")}});
        publishWorkPartsBackupEvent(QStringLiteral("order.invoices"),
                                    QStringLiteral("part_attachment"),
                                    QString::number(partId),
                                    currentOrderUid(nullptr),
                                    m_orderNumber,
                                    QStringLiteral("part_attachment_remove"),
                                    QString::fromUtf8("Видалено вкладення / накладну запчастини"),
                                    {QStringLiteral("part_attachments")} );
    } else {
        logWorkPartsDiagnostic(QStringLiteral("attachment_stage"), QStringLiteral("part"), row, partId, attachmentInvoiceNumber, {{QStringLiteral("phase"), QStringLiteral("file_copy_begin")}, {QStringLiteral("source"), QFileInfo(selectedSource).fileName()}});
        const QString storedPath = copiedAttachmentPath(m_orderNumber, QStringLiteral("parts"), partId, selectedSource, &errorText);
        if (storedPath.trimmed().isEmpty()) {
            logWorkPartsDiagnostic(QStringLiteral("attachment_save_fail"), QStringLiteral("part"), row, partId, attachmentInvoiceNumber, {{QStringLiteral("phase"), QStringLiteral("file_copy")}, {QStringLiteral("error"), errorText}});
            QMessageBox::critical(this, QString::fromUtf8("Помилка"), errorText); return;
        }
        if (!WorkPartsMutationService::setPartAttachment(partId, storedPath, QFileInfo(selectedSource).fileName(), QString(), QString(), &errorText)) {
            logWorkPartsDiagnostic(QStringLiteral("attachment_save_fail"), QStringLiteral("part"), row, partId, attachmentInvoiceNumber, {{QStringLiteral("phase"), QStringLiteral("metadata")}, {QStringLiteral("error"), errorText}});
            QMessageBox::critical(this, QString::fromUtf8("Помилка"), errorText);
            return;
        }
        logWorkPartsDiagnostic(QStringLiteral("attachment_save_end"), QStringLiteral("part"), row, partId, attachmentInvoiceNumber, {{QStringLiteral("path"), QFileInfo(storedPath).fileName()}});
        publishWorkPartsBackupEvent(QStringLiteral("order.invoices"),
                                    QStringLiteral("part_attachment"),
                                    QString::number(partId),
                                    currentOrderUid(nullptr),
                                    m_orderNumber,
                                    QStringLiteral("part_attachment_save"),
                                    QString::fromUtf8("Збережено вкладення / накладну запчастини"),
                                    {QStringLiteral("part_attachments")} );
    }
    refreshPartAttachmentCell(row);
    refreshPartInvoiceNumberCell(row);
}

void WorkPartsDialog::openPartInvoiceDocument(int row)
{
    if (!m_partsTable || row < 0 || row >= m_partsTable->rowCount()) return;

    auto* invoiceItem = m_partsTable->item(row, PartColInvoiceNumber);
    auto* attachmentItem = m_partsTable->item(row, PartColAttachment);
    QString path = invoiceItem ? invoiceItem->data(RoleAttachmentPath).toString().trimmed() : QString();
    if (path.isEmpty() && attachmentItem) {
        path = attachmentItem->data(RoleAttachmentPath).toString().trimmed();
        if (path.isEmpty()) path = attachmentItem->data(RoleAttachmentSource).toString().trimmed();
    }

    QString invoiceNumber = invoiceItem ? invoiceItem->data(RoleAttachmentDocumentNumber).toString().trimmed() : QString();
    QString invoiceDate = invoiceItem ? invoiceItem->data(RoleAttachmentDocumentDate).toString().trimmed() : QString();
    if(path.isEmpty()) {
        auto* nameItem=m_partsTable->item(row,PartColName);
        const int partId=nameItem?nameItem->data(RoleId).toInt():0;
        QString registryError;QStringList choices;QVector<RegisteredInvoice> documents;
        for(const auto& document:InvoiceReportService::localInvoices(&registryError)) {
            if(document.filePath.isEmpty())continue;
            bool belongs=false;
            for(const auto& purchase:document.parts)
                if(purchase.source=="part" && purchase.id==partId && purchase.orderNumber==m_orderNumber)belongs=true;
            for(const auto& slice:document.distribution)
                if(slice.partId==partId && slice.orderNumber==m_orderNumber && slice.quantity>0)belongs=true;
            if(belongs){documents.append(document);choices.append(document.number+" / "+document.date.toString("dd.MM.yyyy"));}
        }
        if(!registryError.isEmpty()){QMessageBox::warning(this,QString::fromUtf8("Накладна"),registryError);return;}
        if(!documents.isEmpty()) {
            int index=0;
            if(documents.size()>1) {
                bool accepted=false;
                const auto selected=QInputDialog::getItem(this,QString::fromUtf8("Накладні Еліт"),QString::fromUtf8("Оберіть документ"),choices,0,false,&accepted);
                if(!accepted)return;index=choices.indexOf(selected);if(index<0)return;
            }
            const auto document=documents[index];path=document.filePath;invoiceNumber=document.number;invoiceDate=document.date.toString(Qt::ISODate);
        }
    }
    const QString supplierName = (m_partsTable && m_partsTable->item(row, PartColStore))
        ? m_partsTable->item(row, PartColStore)->text().trimmed()
        : QString();
    if (invoiceNumber.isEmpty() && path.isEmpty()) return;

    qCDebug(csWorkPartsTraceLog).noquote() << QStringLiteral("MOUSE_CLICK [invoice_open_attempt] row=%1 number=%2 path=%3 order=%4")
                          .arg(row)
                          .arg(invoiceNumber.isEmpty() ? QStringLiteral("<empty>") : invoiceNumber)
                          .arg(path.isEmpty() ? QStringLiteral("<empty>") : QFileInfo(path).absoluteFilePath())
                          .arg(m_orderNumber);

    const QString displayNumber = invoiceNumber.isEmpty()
        ? QString::fromUtf8("(без номера)")
        : invoiceNumber;

    if (path.isEmpty() || !QFileInfo::exists(path)) {
        QMessageBox::warning(this,
                             QString::fromUtf8("Накладна"),
                             QString::fromUtf8("PDF накладної для номера %1 не знайдено.").arg(displayNumber));
        return;
    }

    InvoiceDocumentOpenSupport::prepareAsync(this, path, invoiceNumber, invoiceDate, supplierName,
        [this, displayNumber](const QString& openPath, const QString& pdfError) {
        if (openPath.trimmed().isEmpty()) {
            QMessageBox::warning(this,
                                 QString::fromUtf8("Накладна"),
                                 pdfError.trimmed().isEmpty()
                                     ? QString::fromUtf8("Не вдалося підготувати PDF накладної %1.").arg(displayNumber)
                                     : pdfError);
            return;
        }

        if (!QDesktopServices::openUrl(QUrl::fromLocalFile(openPath))) {
            QMessageBox::warning(this,
                                 QString::fromUtf8("Накладна"),
                                 QString::fromUtf8("Не вдалося відкрити PDF накладної %1.").arg(displayNumber));
        }
    });
}

