#include "infrastructure/db/sqlite/Db.h"
#include "core/services/orders/OrderAttachmentService.h"

#include "runtime/AppRuntime.h"
#include "core/backup/BackupCommitPublisher.h"
#include "core/services/storage/StorageDirectoriesGuard.h"
#include "core/io/SafeFileOps.h"
#include "core/services/InvoiceEvidenceService.h"
#include "core/services/SupplierInvoiceLedger.h"
#include <QScopeGuard>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QSqlRecord>
#include <QDebug>
#include "infrastructure/db/sqlite/DbContext.h"

namespace OrderAttachmentService {
namespace {
struct AttachmentOwnerInfo {
    QString title;
    QString orderUid;
    int orderNumber = 0;
};

AttachmentOwnerInfo loadServiceOwnerInfo(QSqlDatabase db, int serviceId)
{
    AttachmentOwnerInfo info;
    if (serviceId <= 0) return info;
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT COALESCE(title,''), COALESCE(order_uid,''), COALESCE(order_number,0) FROM work_order_services WHERE id=?"));
    q.addBindValue(serviceId);
    if (q.exec() && q.next()) {
        info.title = q.value(0).toString().trimmed();
        info.orderUid = q.value(1).toString().trimmed();
        info.orderNumber = q.value(2).toInt();
    }
    return info;
}

AttachmentOwnerInfo loadPartOwnerInfo(QSqlDatabase db, int partId)
{
    AttachmentOwnerInfo info;
    if (partId <= 0) return info;
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT COALESCE(name,''), COALESCE(order_uid,''), COALESCE(order_number,0) FROM work_order_parts WHERE id=?"));
    q.addBindValue(partId);
    if (q.exec() && q.next()) {
        info.title = q.value(0).toString().trimmed();
        info.orderUid = q.value(1).toString().trimmed();
        info.orderNumber = q.value(2).toInt();
    }
    return info;
}

void publishAttachmentBackupEvent(QSqlDatabase db, const QString& operationType,
                                  const QString& entityType,
                                  const QString& entityId,
                                  const QString& orderUid,
                                  int orderNumber,
                                  const QString& description,
                                  const QStringList& changedTables)
{
    const auto publish = [=]() { BackupCommitPublisher::publish(
        BackupCommitPublisher::makeEvent(QStringLiteral("OrderAttachmentService"),
                                         QStringLiteral("order.attachments"),
                                         entityType,
                                         entityId,
                                         orderUid,
                                         orderNumber,
                                         operationType,
                                         description,
                                         changedTables),
        nullptr); };
    if (!TransactionContext::deferUntilCommit(db, publish)) publish();
}

void deferAttachmentCleanup(QSqlDatabase db, const QString& path)
{
    if (path.trimmed().isEmpty()) return;
    // An unknown/raw transaction cannot prove COMMIT. Retaining an orphan is
    // safer than deleting a file that a rollback could still reference.
    TransactionContext::deferUntilCommit(db, [db, path]() { removeUnreferencedManagedFile(db, path); });
}

} // namespace

void removeUnreferencedManagedFile(QSqlDatabase db, const QString& path)
{
    if (path.isEmpty() || !StorageDirectoriesGuard::isPathInsideManagedRoots(path)) return;
    // Archive databases can retain legacy absolute attachment paths. Until a
    // cross-database reference check is available, keep their files intact.
    QDir shards(QDir(AppRuntime::dataDir()).filePath(QStringLiteral("archive_shards")));
    if (!shards.entryList({QStringLiteral("*.db"), QStringLiteral("*.db.enc")}, QDir::Files).isEmpty()
        || QFileInfo::exists(AppRuntime::archiveDbFilePath())
        || QFileInfo::exists(AppRuntime::sealedArchiveDbFilePath())
        || QFileInfo::exists(AppRuntime::legacyDbFilePath())
        || QFileInfo::exists(AppRuntime::sealedLegacyDbFilePath())) return;
    QSqlQuery lock(db);
    if (!lock.exec(QStringLiteral("BEGIN IMMEDIATE"))) return;
    bool referenced = false;
    const QString normalized = QDir::cleanPath(QDir::fromNativeSeparators(path));
    for (const auto& table : db.tables(QSql::AllTables)) {
        const QSqlRecord record = db.record(table);
        if (record.isEmpty()) { referenced = true; break; }
        QString quotedTable = table;
        quotedTable.replace(QLatin1Char('"'), QStringLiteral("\"\""));
        for (int i = 0; i < record.count(); ++i) {
            const QString column = record.fieldName(i);
            if (!column.endsWith(QStringLiteral("_path"), Qt::CaseInsensitive)) continue;
            QString quotedColumn = column;
            quotedColumn.replace(QLatin1Char('"'), QStringLiteral("\"\""));
            QSqlQuery refs(db);
            refs.prepare(QStringLiteral("SELECT 1 FROM \"%1\" WHERE replace(\"%2\",char(92),'/')=? COLLATE NOCASE LIMIT 1")
                .arg(quotedTable, quotedColumn));
            refs.addBindValue(normalized);
            if (!refs.exec() || refs.next()) { referenced = true; break; }
        }
        if (referenced) break;
    }
    if (!referenced && !SafeFileOps::safeRemoveFile(path, StorageDirectoriesGuard::managedRoots()))
        qWarning() << "Unreferenced attachment retained: cleanup could not complete";
    lock.exec(QStringLiteral("ROLLBACK"));
}

QString serviceAttachmentPath(QSqlDatabase db, int serviceId, QString* displayName)
{
    if (displayName) displayName->clear();
    if (serviceId <= 0) return QString();
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT COALESCE(file_path,''), COALESCE(display_name,'') FROM work_order_service_attachments WHERE service_id=?"));
    q.addBindValue(serviceId);
    if (!q.exec() || !q.next()) return QString();
    if (displayName) *displayName = q.value(1).toString();
    return q.value(0).toString();
}


QString partAttachmentPath(QSqlDatabase db, int partId, QString* displayName, QString* invoiceNumber, QString* invoiceDate)
{
    if (displayName) displayName->clear();
    if (invoiceNumber) invoiceNumber->clear();
    if (invoiceDate) invoiceDate->clear();
    if (partId <= 0) return QString();
    const auto info=partAttachmentMap(db,{partId}).value(partId);
    if(displayName) *displayName=info.displayName;
    if(invoiceNumber) *invoiceNumber=info.invoiceNumber;
    if(invoiceDate) *invoiceDate=info.invoiceDate;
    return info.filePath;
}

QHash<int, AttachmentInfo> serviceAttachmentMap(QSqlDatabase db, const QList<int>& serviceIds)
{
    QHash<int, AttachmentInfo> result;
    if (serviceIds.isEmpty()) return result;

    constexpr int kBatchSize = 400;
    for (int offset = 0; offset < serviceIds.size(); offset += kBatchSize) {
        const int count = qMin(kBatchSize, serviceIds.size() - offset);
        QStringList placeholders;
        placeholders.reserve(count);
        for (int i = 0; i < count; ++i) placeholders << QStringLiteral("?");

        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "SELECT service_id, COALESCE(file_path,''), COALESCE(display_name,'') "
            "FROM work_order_service_attachments WHERE service_id IN (%1)")
            .arg(placeholders.join(QStringLiteral(","))));
        for (int i = 0; i < count; ++i) q.addBindValue(serviceIds.at(offset + i));
        if (!q.exec()) continue;
        while (q.next()) {
            AttachmentInfo info;
            info.filePath = q.value(1).toString();
            info.displayName = q.value(2).toString();
            result.insert(q.value(0).toInt(), info);
        }
    }
    return result;
}

QHash<int, AttachmentInfo> partAttachmentMap(QSqlDatabase db, const QList<int>& partIds, QString* error)
{
    if (error) error->clear();
    QHash<int, AttachmentInfo> result;
    if (partIds.isEmpty()) return result;
    const auto remote=SupplierInvoiceLedger::activeLinks(db);

    constexpr int kBatchSize = 400;
    for (int offset = 0; offset < partIds.size(); offset += kBatchSize) {
        const int count = qMin(kBatchSize, partIds.size() - offset);
        QStringList placeholders;
        placeholders.reserve(count);
        for (int i = 0; i < count; ++i) placeholders << QStringLiteral("?");

        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "SELECT part_id, COALESCE(file_path,''), COALESCE(display_name,''), "
            "COALESCE(invoice_number,''), COALESCE(invoice_date,'') "
            "FROM work_order_part_attachments WHERE part_id IN (%1)")
            .arg(placeholders.join(QStringLiteral(","))));
        for (int i = 0; i < count; ++i) q.addBindValue(partIds.at(offset + i));
        if (!q.exec() && error) { *error = q.lastError().text(); return {}; }
        while (q.next()) {
            AttachmentInfo info;
            info.filePath = q.value(1).toString();
            info.displayName = q.value(2).toString();
            info.invoiceNumber = q.value(3).toString();
            info.invoiceDate = q.value(4).toString();
            result.insert(q.value(0).toInt(), info);
        }
        if (error && q.lastError().isValid()) { *error = q.lastError().text(); return {}; }
        if(db.tables().contains("workshop_spare_movements")) {
            QSqlQuery stock(db);
            stock.prepare(QStringLiteral("SELECT m.part_id,s.document,s.ordered_on FROM workshop_spare_movements m "
                "JOIN workshop_spare_orders s ON s.id=m.spare_id JOIN work_order_parts p ON p.id=m.part_id "
                "AND p.order_number=m.order_number WHERE m.operation='issue' AND m.part_id IN (%1)")
                .arg(placeholders.join(QStringLiteral(","))));
            for(int i=0;i<count;++i) stock.addBindValue(partIds.at(offset+i));
            if(stock.exec()) while(stock.next()) {
                auto& info=result[stock.value(0).toInt()];
                if(info.invoiceNumber.trimmed().isEmpty()) info.invoiceNumber=stock.value(1).toString();
                if(info.invoiceDate.trimmed().isEmpty()) info.invoiceDate=stock.value(2).toString();
            }
            if (error && stock.lastError().isValid()) { *error = stock.lastError().text(); return {}; }
        }
        if(!remote.isEmpty()) {
            QSqlQuery parts(db);
            parts.prepare(QString("SELECT id,order_uid,order_number FROM work_order_parts WHERE id IN (%1)").arg(placeholders.join(',')));
            for(int i=0;i<count;++i) parts.addBindValue(partIds.at(offset+i));
            if(parts.exec()) while(parts.next()) {
                const int partId=parts.value(0).toInt();
                auto linked=remote.value(PurchasePaymentService::key("part",partId,parts.value(1).toString()));
                if(linked.number.isEmpty() && db.tables().contains("workshop_spare_movements")) {
                    QSqlQuery receipts(db);
                    receipts.prepare("SELECT DISTINCT r.purchase_id FROM workshop_spare_movements i JOIN workshop_spare_movements r ON r.spare_id=i.spare_id AND r.operation='receipt' WHERE i.operation='issue' AND i.part_id=? AND i.order_number=?");
                    receipts.addBindValue(partId); receipts.addBindValue(parts.value(2));
                    QSet<QString> identities;
                    int receiptCount=0;
                    if(receipts.exec()) while(receipts.next()) {
                        ++receiptCount;
                        const auto candidate=remote.value(PurchasePaymentService::key("inventory",receipts.value(0).toLongLong()));
                        if(!candidate.number.isEmpty()) {
                            identities.insert(candidate.number+"|"+candidate.date); linked=candidate;
                            // Issued slices must resolve their files through the FIFO registry.
                            if(candidate.apportioned)linked.filePath.clear();
                        }
                    }
                    if (error && receipts.lastError().isValid()) { *error = receipts.lastError().text(); return {}; }
                    if(receiptCount!=1 || identities.size()!=1) linked={};
                }
                auto& info=result[partId];
                if(info.invoiceNumber.isEmpty() && info.filePath.isEmpty() && !linked.number.isEmpty()) {
                    info.invoiceNumber=linked.number; info.invoiceDate=linked.date;
                    info.displayName=QString::fromUtf8("Накладна Еліт %1").arg(linked.number);
                    info.filePath=linked.filePath;
                }
            }
            if (error && parts.lastError().isValid()) { *error = parts.lastError().text(); return {}; }
        }
    }
    return result;
}

bool setServiceAttachment(QSqlDatabase db, int serviceId, const QString& filePath, const QString& displayName, QString* errorText)
{
    if (!filePath.trimmed().isEmpty() && !QFileInfo(filePath).isFile()) {
        if (errorText) *errorText = QString::fromUtf8("Файл вкладення недоступний. Попереднє вкладення збережено.");
        return false;
    }
    const QString oldPath = serviceAttachmentPath(db, serviceId);
    const AttachmentOwnerInfo owner = loadServiceOwnerInfo(db, serviceId);
    if (filePath.trimmed().isEmpty()) {
        QSqlQuery del(db);
        del.prepare(QStringLiteral("DELETE FROM work_order_service_attachments WHERE service_id=?"));
        del.addBindValue(serviceId);
        if (!del.exec()) {
            if (errorText) *errorText = del.lastError().text();
            return false;
        }
        if (oldPath != filePath) deferAttachmentCleanup(db, oldPath);
        publishAttachmentBackupEvent(db, QStringLiteral("service_attachment_remove"),
                                     QStringLiteral("service_attachment"),
                                     QString::number(serviceId),
                                     owner.orderUid,
                                     owner.orderNumber,
                                     QString::fromUtf8("Видалено вкладення для роботи: %1").arg(owner.title),
                                     {QStringLiteral("work_order_service_attachments")});
        return true;
    }

    QSqlQuery q(db);
    q.prepare(QStringLiteral("INSERT OR REPLACE INTO work_order_service_attachments(service_id, file_path, display_name) VALUES(?,?,?)"));
    q.addBindValue(serviceId);
    q.addBindValue(filePath);
    q.addBindValue(displayName.isNull() ? QStringLiteral("") : displayName);
    if (!q.exec()) {
        if (errorText) *errorText = q.lastError().text();
        return false;
    }
    if (!oldPath.isEmpty() && oldPath != filePath) deferAttachmentCleanup(db, oldPath);
    publishAttachmentBackupEvent(db, QStringLiteral("service_attachment_save"),
                                 QStringLiteral("service_attachment"),
                                 QString::number(serviceId),
                                 owner.orderUid,
                                 owner.orderNumber,
                                 QString::fromUtf8("Збережено вкладення для роботи: %1").arg(owner.title),
                                 {QStringLiteral("work_order_service_attachments")});
    return true;
}

bool setPartAttachment(QSqlDatabase db,
                       int partId,
                       const QString& filePath,
                       const QString& displayName,
                       const QString& invoiceNumber,
                       const QString& invoiceDate,
                       QString* errorText, bool confirmedImport, double confirmedAmount)
{
    if (!filePath.trimmed().isEmpty() && !QFileInfo(filePath).isFile()) {
        if (errorText) *errorText = QString::fromUtf8("Файл накладної недоступний. Попереднє вкладення збережено.");
        return false;
    }
    const QString oldPath = partAttachmentPath(db, partId);
    const AttachmentOwnerInfo owner = loadPartOwnerInfo(db, partId);
    QSqlQuery savepoint(db);
    if (!savepoint.exec(QStringLiteral("SAVEPOINT invoice_attachment_evidence"))) {
        if (errorText) *errorText = savepoint.lastError().text();
        return false;
    }
    bool completed = false;
    const auto rollback = qScopeGuard([&] {
        if (!completed) {
            savepoint.exec(QStringLiteral("ROLLBACK TO invoice_attachment_evidence"));
            savepoint.exec(QStringLiteral("RELEASE invoice_attachment_evidence"));
        }
    });
    const auto finish = [&] {
        if (!savepoint.exec(QStringLiteral("RELEASE invoice_attachment_evidence"))) {
            if (errorText) *errorText = savepoint.lastError().text();
            return false;
        }
        completed = true;
        return true;
    };
    // A manually entered invoice number is useful even without a scanned file.
    if (filePath.trimmed().isEmpty() && invoiceNumber.trimmed().isEmpty()) {
        QSqlQuery del(db);
        del.prepare(QStringLiteral("DELETE FROM work_order_part_attachments WHERE part_id=?"));
        del.addBindValue(partId);
        if (!del.exec()) {
            if (errorText) *errorText = del.lastError().text();
            return false;
        }
        if (!InvoiceEvidenceService::deactivate(db, partId, errorText) || !finish()) return false;
        if (oldPath != filePath) deferAttachmentCleanup(db, oldPath);
        publishAttachmentBackupEvent(db, QStringLiteral("part_attachment_remove"),
                                     QStringLiteral("part_attachment"),
                                     QString::number(partId),
                                     owner.orderUid,
                                     owner.orderNumber,
                                     QString::fromUtf8("Видалено вкладення/накладну для запчастини: %1").arg(owner.title),
                                     {QStringLiteral("work_order_part_attachments")});
        return true;
    }

    QSqlQuery q(db);
    q.prepare(QStringLiteral("INSERT OR REPLACE INTO work_order_part_attachments(part_id, file_path, display_name, invoice_number, invoice_date) VALUES(?,?,?,?,?)"));
    q.addBindValue(partId);
    q.addBindValue(filePath.isNull() ? QStringLiteral("") : filePath);
    q.addBindValue(displayName.isNull() ? QStringLiteral("") : displayName);
    q.addBindValue(invoiceNumber.isNull() ? QStringLiteral("") : invoiceNumber.trimmed());
    q.addBindValue(invoiceDate.isNull() ? QStringLiteral("") : invoiceDate.trimmed());
    if (!q.exec()) {
        if (errorText) *errorText = q.lastError().text();
        return false;
    }
    if (confirmedImport) {
        if (!InvoiceEvidenceService::capture(db, partId, filePath, invoiceNumber, invoiceDate, errorText, confirmedAmount)) return false;
    } else if (oldPath != filePath) {
        if (!InvoiceEvidenceService::deactivate(db, partId, errorText)) return false;
    }
    if (!finish()) return false;
    if (!oldPath.isEmpty() && oldPath != filePath) deferAttachmentCleanup(db, oldPath);
    publishAttachmentBackupEvent(db, QStringLiteral("part_attachment_save"),
                                 QStringLiteral("part_attachment"),
                                 QString::number(partId),
                                 owner.orderUid,
                                 owner.orderNumber,
                                 QString::fromUtf8("Збережено вкладення/накладну для запчастини: %1").arg(owner.title),
                                 {QStringLiteral("work_order_part_attachments"), QStringLiteral("invoice_part_evidence")});
    return true;
}

} // namespace OrderAttachmentService

namespace OrderAttachmentService {
void removeUnreferencedManagedFile(const QString& path) { removeUnreferencedManagedFile(Database::db(), path); }
}
