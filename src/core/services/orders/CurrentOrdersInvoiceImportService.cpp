#include "core/util/DateFormats.h"
#include "CurrentOrdersInvoiceImportService.h"
#include <algorithm>
#include "core/diagnostics/RuntimeLoggingControl.h"

#include "infrastructure/db/sqlite/Db.h"
#include "core/repository/PartRepository.h"
#include "core/util/FormatUtils.h"
#include "core/util/OilServiceClassifier.h"
#include "core/services/orders/InvoiceImportOwnerSupport.h"
#include "core/services/AccessControlService.h"
#include "core/services/orders/OrderFinancialService.h"
#include "core/services/orders/OrderStatusService.h"
#include "core/services/orders/OrderAttachmentService.h"
#include "runtime/AppRuntime.h"
#include "core/services/orders/PartImportTextSupport.h"
#include "core/services/orders/PartRowPayloadBuilderService.h"
#include "core/services/orders/WorkPartsMutationService.h"
#include "core/services/storage/StorageDirectoriesGuard.h"
#include "core/io/SafeFileOps.h"

#include <QDebug>
#include <QCryptographicHash>
#include <QTemporaryFile>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QHash>
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QVector>
#include <QtGlobal>

namespace {

static QString invoiceTraceId()
{
    return QStringLiteral("INV-%1").arg(QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMddHHmmsszzz")));
}

static QString safeLogValue(QString value, int maxLen = 160)
{
    value = value.simplified();
    if (value.size() > maxLen) value = value.left(maxLen) + QStringLiteral("...");
    return value;
}

static CurrentOrdersInvoiceUnifiedRow toUnifiedRow(const CurrentOrdersImportServiceRow& row, int sourceRowIndex)
{
    CurrentOrdersInvoiceUnifiedRow out;
    out.sourceRowIndex = sourceRowIndex;
    out.sourceKind = QStringLiteral("CurrentOrdersImportServiceRow");
    out.orderedAt = row.orderedAt.trimmed();
    out.deliveryDate = row.deliveryDate.trimmed();
    out.orderNumberText = row.orderNumberText.trimmed();
    out.store = row.store.trimmed();
    out.oemNumber = row.oemNumber.trimmed();
    out.name = row.name.trimmed();
    out.brandName = row.brandName.trimmed();
    out.qtyText = row.qtyText.trimmed();
    out.priceText = row.priceText.trimmed();
    return out;
}

static void logInvoiceRowStage(const QString& traceId, const QString& stage, const CurrentOrdersInvoiceUnifiedRow& row)
{
    if (RuntimeLoggingControl::deepDebugEnabled()) qInfo().noquote() << QStringLiteral("INVOICE_IMPORT_%1 trace=%2 srcRow=%3 kind=%4 invoice=%5 invoiceDate=%6 order=%7 orderedAt=%8 deliveryDate=%9 store=%10 brand=%11 oem=%12 name=%13 qty=%14 price=%15")
                         .arg(stage)
                         .arg(traceId)
                         .arg(row.sourceRowIndex)
                         .arg(safeLogValue(row.sourceKind))
                         .arg(safeLogValue(row.invoiceNumber))
                         .arg(safeLogValue(row.invoiceDate))
                         .arg(safeLogValue(row.orderNumberText))
                         .arg(safeLogValue(row.orderedAt))
                         .arg(safeLogValue(row.deliveryDate))
                         .arg(safeLogValue(row.store))
                         .arg(safeLogValue(row.brandName))
                         .arg(safeLogValue(row.oemNumber))
                         .arg(safeLogValue(row.name))
                         .arg(safeLogValue(row.qtyText))
                         .arg(safeLogValue(row.priceText));
}

static bool bindOwners(QList<CurrentOrdersImportServiceRow>& rows, QString* error)
{
    QHash<int,QList<OrderSearchHit>> candidatesByNumber;
    QSet<QPair<int,QString>> verified;
    for (auto& row : rows) {
        const int visible=row.orderNumberText.toInt();
        auto it=candidatesByNumber.constFind(visible);
        if (it==candidatesByNumber.cend()) {
            const auto candidates=CurrentOrdersInvoiceImportService::findOrderCandidates(visible,error);
            if (error && !error->isEmpty()) return false;
            it=candidatesByNumber.insert(visible,candidates);
        }
        const auto& hits=it.value();
        row.orderUid=InvoiceImportOwnerSupport::select(hits,row.orderUid,error);
        if (row.orderUid.isEmpty()) return false;
        const auto key=qMakePair(visible,row.orderUid);
        if (verified.contains(key)) continue;
        for (const auto& hit:hits) if (hit.orderUid==row.orderUid
                && !InvoiceImportOwnerSupport::validateStoredOwner(Database::db(),hit,error)) return false;
        verified.insert(key);
    }
    return true;
}

QString attachmentBaseDir(int orderNumber, const QString& bucket)
{
    QString errorText;
    const auto kind = bucket == QStringLiteral("services")
        ? StorageDirectoriesGuard::StorageKind::OrderPhoto
        : StorageDirectoriesGuard::StorageKind::Invoice;
    return StorageDirectoriesGuard::managedSubdir(kind, orderNumber, bucket, &errorText);
}

QString copiedAttachmentPath(int orderNumber, const QString& bucket, int ownerId, const QString& sourcePath, QString* errorText)
{
    if (ownerId <= 0 || sourcePath.trimmed().isEmpty()) return QString();
    const QFileInfo fi(sourcePath);
    if (!fi.exists() || !fi.isFile()) {
        if (errorText) *errorText = QString::fromUtf8("Файл не знайдено: %1").arg(QDir::toNativeSeparators(sourcePath));
        return QString();
    }
    const auto kind = bucket == QStringLiteral("services")
        ? StorageDirectoriesGuard::StorageKind::OrderPhoto
        : StorageDirectoriesGuard::StorageKind::Invoice;
    return StorageDirectoriesGuard::copyFileToManagedStorage(kind, orderNumber, bucket, ownerId, fi.absoluteFilePath(), errorText);
}

QString importedInvoiceTargetFileName(const CurrentOrdersInvoiceAttachmentSpec& spec)
{
    const QFileInfo sourceInfo(spec.sourceFilePath.trimmed().isEmpty() ? spec.displayName : spec.sourceFilePath);
    const QString ext = sourceInfo.suffix().trimmed().isEmpty() ? QStringLiteral("pdf") : sourceInfo.suffix().trimmed().toLower();
    const QString store = spec.storeName.trimmed();
    const QString invoice = spec.invoiceNumber.trimmed();

    QString base;
    if (!store.isEmpty() && !invoice.isEmpty()) {
        base = QStringLiteral("%1_%2").arg(store, invoice);
    } else if (!invoice.isEmpty()) {
        base = invoice;
    } else if (!store.isEmpty() && !sourceInfo.completeBaseName().trimmed().isEmpty()) {
        base = QStringLiteral("%1 %2").arg(store, sourceInfo.completeBaseName().trimmed());
    } else {
        base = sourceInfo.completeBaseName().trimmed();
    }
    if (base.trimmed().isEmpty()) base = QStringLiteral("invoice");
    QDate documentDate = QDate::fromString(spec.invoiceDate.trimmed(), Qt::ISODate);
    if (!documentDate.isValid()) documentDate = QDate::fromString(spec.invoiceDate.trimmed(), "dd.MM.yyyy");
    if (documentDate.isValid()) base += "_" + documentDate.toString("dd.MM.yyyy");
    return QStringLiteral("%1.%2").arg(base.trimmed(), ext);
}

} // namespace

QString CurrentOrdersInvoiceUnifiedRow::compactLogText() const
{
    return QStringLiteral("srcRow=%1 kind=%2 invoice=%3 invoiceDate=%4 order=%5 orderedAt=%6 deliveryDate=%7 store=%8 brand=%9 oem=%10 name=%11 qty=%12 price=%13")
        .arg(sourceRowIndex)
        .arg(sourceKind)
        .arg(invoiceNumber)
        .arg(invoiceDate)
        .arg(orderNumberText)
        .arg(orderedAt)
        .arg(deliveryDate)
        .arg(store)
        .arg(brandName)
        .arg(oemNumber)
        .arg(name)
        .arg(qtyText)
        .arg(priceText);
}

MutationResult CurrentOrdersInvoiceImportService::importRowsDetailed(const QList<CurrentOrdersImportServiceRow>& rows,
                                                   const AttachmentHandler& attachmentHandler,
                                                   CurrentOrdersInvoiceImportResult* result,
                                                   QString* errorText)
{
    return importRowsDetailedImpl(rows, attachmentHandler, result, errorText, {});
}

QList<OrderSearchHit> CurrentOrdersInvoiceImportService::findOrderCandidates(int visibleNumber, QString* errorText)
{
    const auto db=Database::db();
    if (!AccessControlService::currentUserHasPermission(db,"parts.order")) {
        if (errorText) *errorText=QStringLiteral("Немає дозволу на імпорт запчастин.");
        return {};
    }
    auto owners=InvoiceImportOwnerSupport::candidates(db,visibleNumber,errorText);
    owners.removeIf([&](const OrderSearchHit& owner){return !AccessControlService::canAccessOrder(db,owner.internalNumber);});
    return owners;
}

MutationResult CurrentOrdersInvoiceImportService::importRowsDetailedImpl(const QList<CurrentOrdersImportServiceRow>& rows,
                                                   const AttachmentHandler& attachmentHandler,
                                                   CurrentOrdersInvoiceImportResult* result,
                                                   QString* errorText,
                                                   const QString& receiptKey,
                                                   const QByteArray& sourceDigest)
{
    if (errorText) errorText->clear();
    if (result) result->affectedOrderNumbers.clear();
    const QString traceId = invoiceTraceId();
    if (RuntimeLoggingControl::deepDebugEnabled()) qInfo().noquote() << QStringLiteral("INVOICE_IMPORT_BEGIN trace=%1 rows=%2").arg(traceId).arg(rows.size());
    if (rows.isEmpty()) { MutationResult r; r.sourceModule = QStringLiteral("CurrentOrdersInvoiceImportService"); r.entityType = QStringLiteral("work_order_parts_invoice_batch"); r.entityId = QStringLiteral("0"); r.operation = QStringLiteral("import_invoice_rows"); r.description = QString::fromUtf8("Імпорт рядків накладної в наряди"); r.changedTables = {QStringLiteral("work_order_parts"), QStringLiteral("work_orders")}; r.errorText = QString::fromUtf8("Немає рядків для імпорту."); return r; }

    QSqlDatabase db = Database::db();
    auto boundRows=rows;
    QString ownerError;
    if (!bindOwners(boundRows,&ownerError)) {
        MutationResult failed; failed.errorText=ownerError; return failed;
    }
    QSet<int> uniqueOrders;
    QHash<QString,int> preparedOwners;
    QHash<int, int> resolvedOrderBySourceRow;
    for (int sourceRowIndex = 0; sourceRowIndex < rows.size(); ++sourceRowIndex) {
        const CurrentOrdersImportServiceRow& row = boundRows.at(sourceRowIndex);
        logInvoiceRowStage(traceId, QStringLiteral("RAW_ROW"), toUnifiedRow(row, sourceRowIndex));
        bool ok = false;
        const int displayedOrderNumber = row.orderNumberText.toInt(&ok);
        if (!ok || displayedOrderNumber <= 0) {
            MutationResult r; r.sourceModule = QStringLiteral("CurrentOrdersInvoiceImportService"); r.entityType = QStringLiteral("work_order_parts_invoice_batch"); r.entityId = row.orderNumberText; r.operation = QStringLiteral("import_invoice_rows"); r.description = QString::fromUtf8("Імпорт рядків накладної в наряди"); r.changedTables = {QStringLiteral("work_order_parts"), QStringLiteral("work_orders")}; r.errorText = QString::fromUtf8("Некоректний № наряду: %1").arg(row.orderNumberText); return r;
        }

        QString resolveError;
        int orderNumber=preparedOwners.value(row.orderUid);
        if (!orderNumber && Database::ensureOrderAvailableInActiveByUid(row.orderUid,&resolveError))
            orderNumber=Database::resolveOrderInternalNumberByUid(row.orderUid);
        if (orderNumber>0 && !InvoiceImportOwnerSupport::validateActive(db,orderNumber,displayedOrderNumber,row.orderUid,&resolveError)) orderNumber=0;
        if (orderNumber <= 0) {
            MutationResult r; r.sourceModule = QStringLiteral("CurrentOrdersInvoiceImportService"); r.entityType = QStringLiteral("work_order_parts_invoice_batch"); r.entityId = row.orderNumberText; r.operation = QStringLiteral("import_invoice_rows"); r.description = QString::fromUtf8("Імпорт рядків накладної в наряди"); r.changedTables = {QStringLiteral("work_order_parts"), QStringLiteral("work_orders")}; r.errorText = resolveError.trimmed().isEmpty() ? QString::fromUtf8("Не вдалося визначити внутрішній № наряду для %1.").arg(row.orderNumberText) : resolveError; return r;
        }

        preparedOwners.insert(row.orderUid,orderNumber);
        resolvedOrderBySourceRow.insert(sourceRowIndex, orderNumber);
        uniqueOrders.insert(orderNumber);
    }

    QList<int> affected = uniqueOrders.values();
    std::sort(affected.begin(), affected.end());

    MutationExecutor::Request request;
    request.sourceModule = QStringLiteral("CurrentOrdersInvoiceImportService");
    request.entityType = QStringLiteral("work_order_parts_invoice_batch");
    request.entityId = QString::number(rows.size());
    request.operation = QStringLiteral("import_invoice_rows");
    request.description = QString::fromUtf8("Імпорт рядків накладної в наряди");
    request.changedTables = {QStringLiteral("work_order_parts"), QStringLiteral("work_orders")};
    request.beginErrorFallback = QString::fromUtf8("Не вдалося відкрити транзакцію імпорту.");
    request.commitErrorFallback = QString::fromUtf8("Не вдалося завершити імпорт.");

    const auto execResult = WorkPartsMutationService::execute(request, [&](QSqlDatabase& txDb, QString* txError) {
        const auto allowed = [&] {
            if (AccessControlService::currentUserHasPermission(txDb,"parts.order") &&
                std::all_of(uniqueOrders.cbegin(),uniqueOrders.cend(),[&](int number){return AccessControlService::canAccessOrder(txDb,number);})) return true;
            if (txError) *txError=QStringLiteral("Немає дозволу на імпорт запчастин до вибраного наряду.");
            return false;
        };
        if (!allowed()) return false;
        QList<QString> owners;
        for (int i=0;i<boundRows.size();++i) {
            const auto& row=boundRows.at(i);
            if (!InvoiceImportOwnerSupport::validateActive(txDb,resolvedOrderBySourceRow.value(i),
                    row.orderNumberText.toInt(),row.orderUid,txError)) return false;
            owners.append(row.orderUid);
        }
        QJsonArray insertedIds;
        if (!receiptKey.isEmpty()) {
            QSqlQuery receipt(txDb);
            receipt.prepare(QStringLiteral("SELECT value FROM settings WHERE key=?"));
            receipt.addBindValue(receiptKey);
            if (!receipt.exec()) { if (txError) *txError = receipt.lastError().text(); return false; }
            if (receipt.next()) return InvoiceImportOwnerSupport::verifyReceipt(txDb,
                receipt.value(0).toString().toUtf8(),owners,sourceDigest,txError);
        }
        for (int sourceRowIndex = 0; sourceRowIndex < rows.size(); ++sourceRowIndex) {
            const CurrentOrdersImportServiceRow& row = boundRows.at(sourceRowIndex);
            CurrentOrdersInvoiceUnifiedRow unifiedRow = toUnifiedRow(row, sourceRowIndex);
            logInvoiceRowStage(traceId, QStringLiteral("PARSED_ROW"), unifiedRow);
            bool orderOk = false;
            const int displayedOrderNumber = row.orderNumberText.toInt(&orderOk);
            const int orderNumber = orderOk ? resolvedOrderBySourceRow.value(sourceRowIndex) : 0;
            bool qtyOk = false;
            bool priceOk = false;
            const double qty = qMax(0.0, row.qtyText.trimmed().isEmpty() ? 1.0 : PartImportTextSupport::parseLooseNumber(row.qtyText, &qtyOk));
            const double price = qMax(0.0, row.priceText.trimmed().isEmpty() ? 0.0 : PartImportTextSupport::parseLooseNumber(row.priceText, &priceOk));
            if (!orderOk || orderNumber <= 0 || (!row.qtyText.trimmed().isEmpty() && !qtyOk) || (!row.priceText.trimmed().isEmpty() && !priceOk)) {
                if (txError) *txError = QString::fromUtf8("Не вдалося підготувати рядок для імпорту у наряд №%1.").arg(row.orderNumberText);
                return false;
            }

            PartRowPayloadInput input;
            input.orderedAt = row.orderedAt.trimmed();
            input.deliveryDate = row.deliveryDate.trimmed();
            input.preserveEmptyOrderedAt = true;
            input.preserveEmptyDeliveryDate = true;
            input.store = row.store.trimmed();
            input.partNumber = row.oemNumber.trimmed();
            input.name = row.name.trimmed();
            input.note = QStringLiteral("");
            input.manufacturer = row.brandName.trimmed();
            input.qty = qty > 0.0 ? qty : 1.0;
            input.purchasePrice = FormatUtils::roundMoneyValue(price);
            input.sourceType = QStringLiteral("import");
            input.serviceId = 0;
            unifiedRow.orderedAt = input.orderedAt;
            unifiedRow.deliveryDate = input.deliveryDate;
            unifiedRow.store = input.store;
            unifiedRow.oemNumber = input.partNumber;
            unifiedRow.name = input.name;
            unifiedRow.brandName = input.manufacturer;
            unifiedRow.qtyText = QString::number(input.qty, 'f', 3);
            unifiedRow.priceText = QString::number(input.purchasePrice, 'f', 2);
            logInvoiceRowStage(traceId, QStringLiteral("UI_ROW"), unifiedRow);
            const WorkOrderPartRow payload = PartRowPayloadBuilderService::build(input);
            if (RuntimeLoggingControl::deepDebugEnabled()) qInfo().noquote() << QStringLiteral("PART_PRICING op=import order=%1 partId=0 trace=current_orders name=\"%2\" store=\"%3\" qty=%4 purchase=%5 client=%6 activePct=%7 source=%8 override=%9")
                .arg(orderNumber)
                .arg(payload.name)
                .arg(payload.store)
                .arg(payload.qty, 0, 'f', 3)
                .arg(payload.purchasePrice, 0, 'f', 2)
                .arg(payload.clientPrice, 0, 'f', 2)
                .arg(payload.markupPercent, 0, 'f', 2)
                .arg(payload.markupSource)
                .arg(payload.manualMarkupOverride ? 1 : 0);

            int insertedPartId = 0;
            if (!PartRepository::insert(txDb, orderNumber, payload, &insertedPartId, txError)) {
                return false;
            }
            insertedIds.append(insertedPartId);
            unifiedRow.sourceKind = QStringLiteral("work_order_parts:%1").arg(insertedPartId);
            logInvoiceRowStage(traceId, QStringLiteral("DB_ROW"), unifiedRow);
            if (RuntimeLoggingControl::deepDebugEnabled()) qInfo().noquote() << QStringLiteral("PART_PRICING op=save order=%1 partId=%2 trace=current_orders name=\"%3\" store=\"%4\" qty=%5 purchase=%6 client=%7 activePct=%8 source=%9 override=%10 elapsedMs=0")
                .arg(orderNumber)
                .arg(insertedPartId)
                .arg(payload.name)
                .arg(payload.store)
                .arg(payload.qty, 0, 'f', 3)
                .arg(payload.purchasePrice, 0, 'f', 2)
                .arg(payload.clientPrice, 0, 'f', 2)
                .arg(payload.markupPercent, 0, 'f', 2)
                .arg(payload.markupSource)
                .arg(payload.manualMarkupOverride ? 1 : 0);
            if (!InvoiceImportOwnerSupport::validateActive(txDb,orderNumber,row.orderNumberText.toInt(),row.orderUid,txError)) return false;
            if (attachmentHandler && insertedPartId > 0) {
                if (!attachmentHandler(txDb, orderNumber, insertedPartId, txError)) {
                    return false;
                }
            }
        }

        if (!allowed()) return false;
        for (int i=0;i<boundRows.size();++i) {
            const auto& row=boundRows.at(i);
            if (!InvoiceImportOwnerSupport::validateActive(txDb,resolvedOrderBySourceRow.value(i),
                    row.orderNumberText.toInt(),row.orderUid,txError)) return false;
        }
        for (int orderNumber : affected) {
            QString localError;
            if (!OrderFinancialService::refreshOrderFinancialSums(txDb, orderNumber, &localError)) {
                if (txError) *txError = localError.trimmed().isEmpty()
                    ? QString::fromUtf8("Не вдалося оновити суми наряду №%1 після імпорту.").arg(orderNumber)
                    : localError;
                return false;
            }
            if (!OrderStatusService::promoteToInProgressIfNeeded(txDb, orderNumber, &localError)) {
                if (txError) *txError = localError.trimmed().isEmpty()
                    ? QString::fromUtf8("Не вдалося оновити статус наряду №%1 після імпорту.").arg(orderNumber)
                    : localError;
                return false;
            }
        }
        if (!receiptKey.isEmpty()) {
            QSqlQuery receipt(txDb);
            receipt.prepare(QStringLiteral("INSERT INTO settings(key,value) VALUES(?,?)"));
            receipt.addBindValue(receiptKey);
            receipt.addBindValue(QString::fromUtf8(QJsonDocument(insertedIds).toJson(QJsonDocument::Compact)));
            if (!receipt.exec()) { if (txError) *txError = receipt.lastError().text(); return false; }
        }
        return true;
    });
    if (!execResult.ok) {
        qCritical().noquote() << QStringLiteral("INVOICE_IMPORT_END trace=%1 status=failed error=%2").arg(traceId, execResult.errorText);
        return execResult;
    }
    if (RuntimeLoggingControl::deepDebugEnabled()) qInfo().noquote() << QStringLiteral("INVOICE_IMPORT_END trace=%1 status=ok affectedOrders=%2").arg(traceId).arg(affected.size());

    for (int orderNumber : affected) {
        Database::refreshOrderRegistry(orderNumber, nullptr);
    }
    if (result) result->affectedOrderNumbers = affected;
    return execResult;
}

bool CurrentOrdersInvoiceImportService::importRows(const QList<CurrentOrdersImportServiceRow>& rows,
                           const AttachmentHandler& attachmentHandler,
                           CurrentOrdersInvoiceImportResult* result,
                           QString* errorText)
{
    const auto mutationResult = importRowsDetailed(rows, attachmentHandler, result);
    if (!mutationResult.ok && errorText) *errorText = mutationResult.errorText;
    return mutationResult.ok;
}

MutationResult CurrentOrdersInvoiceImportService::importRowsDetailed(const QList<CurrentOrdersImportServiceRow>& rows,
                                                                    const CurrentOrdersInvoiceAttachmentSpec& attachmentSpec,
                                                                    CurrentOrdersInvoiceImportResult* result,
                                                                    QString* errorText)
{
    if (errorText) errorText->clear();
    if (result) result->affectedOrderNumbers.clear();
    if (RuntimeLoggingControl::deepDebugEnabled()) qInfo().noquote() << QStringLiteral("INVOICE_IMPORT_ATTACHMENT_SPEC store=%1 invoice=%2 invoiceDate=%3 source=%4")
                             .arg(attachmentSpec.storeName, attachmentSpec.invoiceNumber, attachmentSpec.invoiceDate, attachmentSpec.sourceFilePath);
    if (!attachmentSpec.hasAttachment()) return importRowsDetailedImpl(rows, {}, result, errorText, {});
    auto boundRows=rows;
    QString ownerError;
    if (!bindOwners(boundRows,&ownerError)) {
        MutationResult failed; failed.errorText=ownerError;
        if (errorText) *errorText=ownerError;
        return failed;
    }
    QFile source(attachmentSpec.sourceFilePath);
    QTemporaryFile frozenSource(QDir::temp().filePath(QStringLiteral("cs_invoice_XXXXXX.")
        + QFileInfo(attachmentSpec.sourceFilePath).suffix()));
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!source.open(QIODevice::ReadOnly) || !frozenSource.open()) {
        MutationResult failed;
        failed.errorText = QString::fromUtf8("Не вдалося прочитати вкладення. Рядки накладної не додано.");
        if (errorText) *errorText = failed.errorText;
        if (result) result->affectedOrderNumbers.clear();
        return failed;
    }
    while (!source.atEnd()) {
        const QByteArray block = source.read(1024 * 1024);
        if (block.isEmpty() || frozenSource.write(block) != block.size()) {
            MutationResult failed; failed.errorText = QString::fromUtf8("Не вдалося підготувати вкладення. Імпорт не розпочато.");
            if (errorText) *errorText = failed.errorText;
            return failed;
        }
        hash.addData(block);
    }
    if (source.error() != QFileDevice::NoError || !frozenSource.flush()) {
        MutationResult failed; failed.errorText = QString::fromUtf8("Не вдалося завершити підготовку вкладення.");
        if (errorText) *errorText = failed.errorText;
        return failed;
    }
    frozenSource.close();
    if (QFileInfo(attachmentSpec.sourceFilePath).suffix().compare(QStringLiteral("pdf"), Qt::CaseInsensitive) == 0) {
        QFile pdf(frozenSource.fileName());
        bool complete = pdf.open(QIODevice::ReadOnly) && pdf.size() >= 32 && pdf.read(8).startsWith("%PDF-");
        if (complete) {
            pdf.seek(qMax<qint64>(0, pdf.size() - 4096));
            const QByteArray tail = pdf.readAll();
            complete = tail.trimmed().endsWith("%%EOF") && tail.contains("startxref");
        }
        if (!complete) {
            MutationResult failed; failed.errorText = QString::fromUtf8("PDF накладної неповний або пошкоджений. Рядки не додано.");
            if (errorText) *errorText = failed.errorText;
            return failed;
        }
    }
    const QByteArray sourceDigest=hash.result();
    QJsonArray identityRows;
    for (const auto& row : rows) {
        QJsonArray values;
        for (const auto& value : {row.orderNumberText, row.orderedAt, row.deliveryDate, row.store,
                                  row.oemNumber, row.name, row.brandName, row.qtyText, row.priceText})
            values.append(value.trimmed());
        identityRows.append(values);
    }
    hash.addData(QJsonDocument(identityRows).toJson(QJsonDocument::Compact));
    QJsonArray meta{attachmentSpec.storeName, attachmentSpec.invoiceNumber, attachmentSpec.invoiceDate,
                    attachmentSpec.importOperationId};
    hash.addData(QJsonDocument(meta).toJson(QJsonDocument::Compact));
    const QString receiptKey = QStringLiteral("invoice_import.v1.") + QString::fromLatin1(hash.result().toHex());
    QHash<int, QString> storedPaths;
    int evidenceRow = 0;
    const AttachmentHandler handler = [&](QSqlDatabase& txDb, int orderNumber, int partId, QString* txError) {
        if (!storedPaths.contains(orderNumber)) {
            const QString copied=StorageDirectoriesGuard::copyFileToManagedStorageAs(StorageDirectoriesGuard::StorageKind::Invoice,
                orderNumber,QStringLiteral("parts"),importedInvoiceTargetFileName(attachmentSpec),frozenSource.fileName(),txError);
            if (copied.isEmpty()) return false;
            storedPaths.insert(orderNumber,copied);
        }
        const QString path=storedPaths.value(orderNumber);
        if (path.isEmpty() || !QFileInfo::exists(path)) {
            if (txError) *txError = QString::fromUtf8("Підготовлене вкладення недоступне. Збереження скасовано.");
            return false;
        }
        const auto& row=rows.at(evidenceRow++);
        const double quantity=row.qtyText.trimmed().isEmpty()?1.0:PartImportTextSupport::parseLooseNumber(row.qtyText);
        const double price=row.priceText.trimmed().isEmpty()?0.0:PartImportTextSupport::parseLooseNumber(row.priceText);
        return OrderAttachmentService::setPartAttachment(txDb, partId, path, attachmentSpec.displayName,
            attachmentSpec.invoiceNumber, attachmentSpec.invoiceDate, txError, true, quantity*price);
    };
    auto mutationResult = importRowsDetailedImpl(boundRows, handler, result, errorText, receiptKey, sourceDigest);
    if (!mutationResult.ok) {
        if (errorText) *errorText = mutationResult.errorText;
        for (const auto& path : storedPaths) OrderAttachmentService::removeUnreferencedManagedFile(Database::db(), path);
    }
    return mutationResult;
}

bool CurrentOrdersInvoiceImportService::importRows(const QList<CurrentOrdersImportServiceRow>& rows,
                                                   const CurrentOrdersInvoiceAttachmentSpec& attachmentSpec,
                                                   CurrentOrdersInvoiceImportResult* result,
                                                   QString* errorText)
{
    const auto mutationResult = importRowsDetailed(rows, attachmentSpec, result, errorText);
    if (!mutationResult.ok && errorText) *errorText = mutationResult.errorText;
    return mutationResult.ok;
}
