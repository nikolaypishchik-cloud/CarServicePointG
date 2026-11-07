#include "core/util/DateFormats.h"
#include "core/sync/SyncPayloadUtils.h"
#include "core/io/SafeFileOps.h"

#include "core/services/orders/PartRowPayloadBuilderService.h"
#include "core/util/PlateUtils.h"
#include "core/backup/BackupCommitPublisher.h"
#include "core/services/MutationExecutor.h"
#include "core/diagnostics/RuntimeLoggingControl.h"

#include "infrastructure/db/sqlite/Db.h"

#include <QDate>
#include <QDebug>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlError>
#include <QStandardPaths>
#include <QTextStream>
#include <QStringList>
#include <QUrl>

namespace SyncPayloadUtils {
namespace {

QString firstStringValue(const QJsonObject& obj, const QStringList& keys)
{
    for (const QString& key : keys) {
        const QJsonValue v = obj.value(key);
        if (v.isString()) {
            const QString s = v.toString().trimmed();
            if (!s.isEmpty()) return s;
        } else if (v.isDouble()) {
            return QString::number(v.toDouble());
        }
    }
    return QString();
}

double jsonLooseNumber(const QJsonValue& value, double defValue = 0.0)
{
    if (value.isDouble()) return value.toDouble(defValue);
    if (value.isString()) {
        QString s = value.toString().trimmed();
        s.remove(QChar(' '));
        s.replace(QChar(','), QChar('.'));
        bool ok = false;
        const double v = s.toDouble(&ok);
        if (ok) return v;
    }
    return defValue;
}

QString normalizeDateToIso(const QString& value)
{
    const QString s = value.trimmed();
    if (s.isEmpty()) return QString();
    const QDate isoDate = DateFormats::parseDate(s);
    if (isoDate.isValid()) return isoDate.toString(Qt::ISODate);
    const QStringList formats = {
        QStringLiteral("yyyy-MM-dd"),
        QStringLiteral("dd.MM.yyyy"),
        QStringLiteral("dd.MM.yy"),
        QStringLiteral("dd/MM/yyyy"),
        QStringLiteral("dd/MM/yy")
    };
    for (const QString& fmt : formats) {
        const QDate d = QDate::fromString(s, fmt);
        if (d.isValid()) return d.toString(Qt::ISODate);
    }
    return QString();
}

QJsonObject extractEffectiveSyncPayload(const QJsonObject& root)
{
    if (root.value(QStringLiteral("payloadJson")).isString()) {
        const auto nestedDoc = QJsonDocument::fromJson(root.value(QStringLiteral("payloadJson")).toString().toUtf8());
        if (nestedDoc.isObject()) {
            QJsonObject nested = nestedDoc.object();
            if (!nested.contains(QStringLiteral("supplier")) && root.value(QStringLiteral("supplier")).isString()) {
                nested.insert(QStringLiteral("supplier"), root.value(QStringLiteral("supplier")).toString());
            }
            if (!nested.contains(QStringLiteral("transport")) && root.value(QStringLiteral("transport")).isString()) {
                nested.insert(QStringLiteral("transport"), root.value(QStringLiteral("transport")).toString());
            }
            if (!nested.contains(QStringLiteral("module")) && root.value(QStringLiteral("type")).isString()) {
                nested.insert(QStringLiteral("module"), root.value(QStringLiteral("type")).toString());
            }
            if (!nested.contains(QStringLiteral("schemaVersion")) && root.value(QStringLiteral("schemaVersion")).isString()) {
                nested.insert(QStringLiteral("schemaVersion"), root.value(QStringLiteral("schemaVersion")).toString());
            }
            if (!nested.contains(QStringLiteral("notes")) && root.value(QStringLiteral("notes")).isString()) {
                nested.insert(QStringLiteral("notes"), root.value(QStringLiteral("notes")).toString());
            }
            if (!nested.contains(QStringLiteral("comment")) && root.value(QStringLiteral("comment")).isString()) {
                nested.insert(QStringLiteral("comment"), root.value(QStringLiteral("comment")).toString());
            }
            if (!nested.contains(QStringLiteral("documentNumber")) && root.value(QStringLiteral("documentNumber")).isString()) {
                nested.insert(QStringLiteral("documentNumber"), root.value(QStringLiteral("documentNumber")).toString());
            }
            if (!nested.contains(QStringLiteral("documentDate")) && root.value(QStringLiteral("documentDate")).isString()) {
                nested.insert(QStringLiteral("documentDate"), root.value(QStringLiteral("documentDate")).toString());
            }
            if (!nested.contains(QStringLiteral("target")) && (root.contains(QStringLiteral("targetType")) || root.contains(QStringLiteral("targetId")))) {
                QJsonObject target;
                if (root.value(QStringLiteral("targetType")).isString()) target.insert(QStringLiteral("type"), root.value(QStringLiteral("targetType")).toString());
                if (root.value(QStringLiteral("targetId")).isString()) target.insert(QStringLiteral("id"), root.value(QStringLiteral("targetId")).toString());
                nested.insert(QStringLiteral("target"), target);
            }
            return nested;
        }
    }
    return root;
}

bool payloadShouldAutoMarkDelivered(const SyncPayload& payload)
{
    const QString module = payload.module.trimmed().toLower();
    const QString targetType = payload.targetType.trimmed().toLower();
    return module.contains(QStringLiteral("invoice"))
        || module.contains(QStringLiteral("purchase"))
        || targetType == QStringLiteral("purchase");
}

QString currentDeliveryDateIso()
{
    return QDate::currentDate().toString(Qt::ISODate);
}

QString normalizeAttachmentPath(const QString& uri)
{
    const QString trimmed = uri.trimmed();
    if (trimmed.isEmpty()) return QString();
    const QUrl url(trimmed);
    if (url.isValid() && url.isLocalFile()) {
        return QDir::fromNativeSeparators(url.toLocalFile());
    }
    return QDir::fromNativeSeparators(trimmed);
}

void appendAttachment(const QString& key, const QString& value, QList<SyncAttachmentRef>* out)
{
    if (!out) return;
    SyncAttachmentRef att;
    att.key = key.trimmed();
    att.uri = value.trimmed();
    att.localPath = normalizeAttachmentPath(att.uri);
    if (att.isValid()) out->push_back(att);
}

void appendAttachmentObject(const QString& key, const QJsonObject& obj, QList<SyncAttachmentRef>* out)
{
    const QString uri = firstStringValue(obj, {
        QStringLiteral("uri"), QStringLiteral("url"), QStringLiteral("path"), QStringLiteral("file"), QStringLiteral("localPath")
    });
    appendAttachment(firstStringValue(obj, {QStringLiteral("kind"), QStringLiteral("type"), QStringLiteral("name")}).isEmpty() ? key : firstStringValue(obj, {QStringLiteral("kind"), QStringLiteral("type"), QStringLiteral("name")}), uri, out);
}

void parseAttachments(const QJsonObject& root, QList<SyncAttachmentRef>* out)
{
    if (!out) return;
    const QJsonValue attachments = root.value(QStringLiteral("attachments"));
    if (attachments.isObject()) {
        const QJsonObject obj = attachments.toObject();
        for (auto it = obj.begin(); it != obj.end(); ++it) {
            if (it.value().isString()) appendAttachment(it.key(), it.value().toString(), out);
            else if (it.value().isObject()) appendAttachmentObject(it.key(), it.value().toObject(), out);
            else if (it.value().isArray()) {
                const QJsonArray arr = it.value().toArray();
                for (const QJsonValue& v : arr) {
                    if (v.isString()) appendAttachment(it.key(), v.toString(), out);
                    else if (v.isObject()) appendAttachmentObject(it.key(), v.toObject(), out);
                }
            }
        }
    } else if (attachments.isArray()) {
        const QJsonArray arr = attachments.toArray();
        for (const QJsonValue& v : arr) {
            if (v.isString()) appendAttachment(QStringLiteral("attachment"), v.toString(), out);
            else if (v.isObject()) appendAttachmentObject(QStringLiteral("attachment"), v.toObject(), out);
        }
    }
}

void appendPartFromObject(const QJsonObject& obj, const QString& fallbackSupplier, const QString& orderedAtHint, QList<WorkOrderPartRow>* out)
{
    if (!out) return;
    WorkOrderPartRow row;
    row.orderedAt = normalizeDateToIso(firstStringValue(obj, {QStringLiteral("ordered_at"), QStringLiteral("orderedAt"), QStringLiteral("documentDate"), QStringLiteral("date")}));
    if (row.orderedAt.isEmpty()) row.orderedAt = normalizeDateToIso(orderedAtHint);
    if (row.orderedAt.isEmpty()) row.orderedAt = QDate::currentDate().toString(Qt::ISODate);
    row.store = firstStringValue(obj, {QStringLiteral("store"), QStringLiteral("supplier"), QStringLiteral("shop")});
    if (row.store.isEmpty()) row.store = fallbackSupplier;
    row.partNumber = firstStringValue(obj, {QStringLiteral("part_number"), QStringLiteral("partNumber"), QStringLiteral("article"), QStringLiteral("sku"), QStringLiteral("oem"), QStringLiteral("barcode"), QStringLiteral("qr")});
    row.name = firstStringValue(obj, {QStringLiteral("name"), QStringLiteral("title"), QStringLiteral("description")});
    row.note = firstStringValue(obj, {QStringLiteral("note"), QStringLiteral("notes"), QStringLiteral("sourceLine"), QStringLiteral("source_line")});
    row.manufacturer = firstStringValue(obj, {QStringLiteral("brand"), QStringLiteral("manufacturer"), QStringLiteral("maker"), QStringLiteral("vendorBrand"), QStringLiteral("part_brand")});
    row.qty = qMax(0.0001, jsonLooseNumber(obj.value(QStringLiteral("qty")), jsonLooseNumber(obj.value(QStringLiteral("quantity")), jsonLooseNumber(obj.value(QStringLiteral("count")), 1.0))));
    const double purchasePrice = jsonLooseNumber(obj.value(QStringLiteral("purchase_price")), jsonLooseNumber(obj.value(QStringLiteral("purchasePrice")), jsonLooseNumber(obj.value(QStringLiteral("price")), jsonLooseNumber(obj.value(QStringLiteral("cost")), 0.0))));
    const bool hasExplicitClientPrice = obj.contains(QStringLiteral("client_price")) || obj.contains(QStringLiteral("clientPrice")) || obj.contains(QStringLiteral("sale_price"));
    const bool hasExplicitMarkup = obj.contains(QStringLiteral("markup_percent")) || obj.contains(QStringLiteral("markupPercent"));
    const double explicitClientPrice = jsonLooseNumber(obj.value(QStringLiteral("client_price")), jsonLooseNumber(obj.value(QStringLiteral("clientPrice")), jsonLooseNumber(obj.value(QStringLiteral("sale_price")), 0.0)));
    const double explicitMarkup = jsonLooseNumber(obj.value(QStringLiteral("markup_percent")), jsonLooseNumber(obj.value(QStringLiteral("markupPercent")), 0.0));

    PartRowPayloadInput payloadInput;
    payloadInput.orderedAt = row.orderedAt;
    payloadInput.deliveryDate = row.deliveryDate;
    payloadInput.installedAt = row.installedAt;
    payloadInput.store = row.store;
    payloadInput.partNumber = row.partNumber;
    payloadInput.name = row.name;
    payloadInput.note = row.note;
    payloadInput.manufacturer = row.manufacturer;
    payloadInput.qty = row.qty;
    payloadInput.purchasePrice = qMax(0.0, purchasePrice);
    payloadInput.sourceType = QStringLiteral("sync");
    payloadInput.manualMarkupOverride = hasExplicitClientPrice && hasExplicitMarkup;
    payloadInput.manualMarkupPercent = hasExplicitMarkup ? qMax(0.0, explicitMarkup) : 0.0;
    row = PartRowPayloadBuilderService::build(payloadInput);
    if (row.name.trimmed().isEmpty() && row.partNumber.trimmed().isEmpty()) return;
    if (RuntimeLoggingControl::deepDebugEnabled()) {
        qInfo().noquote() << QStringLiteral("PART_PRICING_SYNC_IMPORT")
                          << QStringLiteral(" partName=") << row.name
                          << QStringLiteral(" store=") << row.store
                          << QStringLiteral(" qty=") << row.qty
                          << QStringLiteral(" purchasePrice=") << row.purchasePrice
                          << QStringLiteral(" clientPrice=") << row.clientPrice
                          << QStringLiteral(" markupPercent=") << row.markupPercent
                          << QStringLiteral(" markupSource=") << row.markupSource
                          << QStringLiteral(" manualMarkupOverride=") << row.manualMarkupOverride;
    }
    out->push_back(row);
}

QString attachmentsRootDir(int orderNumber)
{
    QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (docs.trimmed().isEmpty()) docs = QDir::homePath();
    return QDir(docs).filePath(QStringLiteral("CarServiceSync/order_media/order_%1").arg(orderNumber));
}

bool copyAttachmentsForOrder(const SyncPayload& payload, int orderNumber, QString* infoText)
{
    if (orderNumber <= 0 || payload.attachments.isEmpty()) {
        if (infoText) *infoText = QString();
        return true;
    }

    const bool acceptPhotos = Database::getSetting(QStringLiteral("settings.int.sync.accept_photos"), true).toBool();
    if (!acceptPhotos) {
        if (infoText) *infoText = QString();
        return true;
    }

    QDir dir(attachmentsRootDir(orderNumber));
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        if (infoText) *infoText = QString::fromUtf8("Не вдалося створити папку для вкладень sync-пакета.");
        return false;
    }

    int copied = 0;
    QStringList manifestLines;
    manifestLines << QString::fromUtf8("Order: %1").arg(orderNumber);
    manifestLines << QString::fromUtf8("Source packet: %1").arg(QDir::toNativeSeparators(payload.sourceFile));
    manifestLines << QString::fromUtf8("Created: %1").arg(QDateTime::currentDateTime().toString(Qt::ISODate));
    manifestLines << QString();

    int seq = 1;
    for (const SyncAttachmentRef& att : payload.attachments) {
        const QString tag = att.key.isEmpty() ? QStringLiteral("attachment") : att.key;
        if (!att.localPath.trimmed().isEmpty() && QFileInfo::exists(att.localPath)) {
            const QFileInfo fi(att.localPath);
            const QString targetName = QStringLiteral("%1_%2_%3").arg(seq++, 3, 10, QChar('0')).arg(tag, fi.fileName());
            const QString targetPath = dir.filePath(targetName);
            SafeFileOps::safeRemoveFile(targetPath, {dir.absolutePath()});
            if (QFile::copy(att.localPath, targetPath)) {
                ++copied;
                manifestLines << QString::fromUtf8("%1 -> %2").arg(QDir::toNativeSeparators(att.localPath), QDir::toNativeSeparators(targetPath));
                continue;
            }
        }
        manifestLines << QString::fromUtf8("%1 -> %2").arg(tag, att.uri);
    }

    QFile manifest(dir.filePath(QStringLiteral("manifest.txt")));
    if (manifest.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream ts(&manifest);
        for (const QString& line : manifestLines) ts << line << '\n';
    }

    if (infoText) {
        if (copied > 0) *infoText = QString::fromUtf8("Скопійовано вкладень: %1").arg(copied);
        else *infoText = QString::fromUtf8("Створено manifest для вкладень sync-пакета");
    }
    return true;
}

} // namespace

bool SyncAttachmentRef::isValid() const
{
    return !uri.trimmed().isEmpty() || !localPath.trimmed().isEmpty();
}

bool SyncPayload::isValid() const
{
    return !sourceFile.trimmed().isEmpty()
        && (!plate.trimmed().isEmpty() || !parts.isEmpty() || !vin.trimmed().isEmpty() || !car.trimmed().isEmpty() || !attachments.isEmpty());
}

bool SyncPayload::targetsNewOrder() const
{
    return targetType.compare(QStringLiteral("order"), Qt::CaseInsensitive) == 0
        && (targetId.trimmed().isEmpty() || targetId.compare(QStringLiteral("new"), Qt::CaseInsensitive) == 0);
}

int SyncPayload::targetOrderNumber() const
{
    bool ok = false;
    const int n = targetId.trimmed().toInt(&ok);
    return ok && n > 0 ? n : 0;
}

QString defaultSyncInboxDir()
{
    QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (docs.trimmed().isEmpty()) docs = QDir::homePath();
    return QDir(docs).filePath(QStringLiteral("CarServiceSync/inbox"));
}

QString syncInboxDir()
{
    const QString raw = Database::getSetting(QStringLiteral("settings.int.sync.inbox_dir"), defaultSyncInboxDir()).toString().trimmed();
    return raw.isEmpty() ? defaultSyncInboxDir() : QDir::fromNativeSeparators(raw);
}

bool syncModuleEnabled()
{
    return Database::getSetting(QStringLiteral("settings.int.sync.enabled"), false).toBool();
}

SyncPayload loadPayloadFromFile(const QString& filePath)
{
    SyncPayload payload;
    payload.sourceFile = filePath;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) return payload;
    const auto doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) return payload;

    const QJsonObject rawRoot = doc.object();
    const QJsonObject root = extractEffectiveSyncPayload(rawRoot);
    const QJsonObject target = root.value(QStringLiteral("target")).toObject();

    payload.schemaVersion = firstStringValue(root, {QStringLiteral("schemaVersion"), QStringLiteral("schema_version")});
    payload.module = firstStringValue(root, {QStringLiteral("module"), QStringLiteral("type")});
    payload.transport = firstStringValue(root, {QStringLiteral("transport")});
    payload.supplier = firstStringValue(root, {QStringLiteral("supplier"), QStringLiteral("store"), QStringLiteral("shop")});
    payload.targetType = firstStringValue(target, {QStringLiteral("type")});
    payload.targetId = firstStringValue(target, {QStringLiteral("id")});
    payload.plate = firstStringValue(root, {QStringLiteral("plate"), QStringLiteral("vehicle_plate"), QStringLiteral("license_plate"), QStringLiteral("plate_number"), QStringLiteral("scanner_plate")});
    if (payload.plate.isEmpty()) payload.plate = firstStringValue(rawRoot, {QStringLiteral("plate"), QStringLiteral("vehicle_plate"), QStringLiteral("license_plate"), QStringLiteral("plate_number")});
    payload.vin = firstStringValue(root, {QStringLiteral("vin")});
    payload.car = firstStringValue(root, {QStringLiteral("car"), QStringLiteral("vehicle"), QStringLiteral("car_name"), QStringLiteral("auto")});
    payload.client = firstStringValue(root, {QStringLiteral("client"), QStringLiteral("customer"), QStringLiteral("client_name")});
    payload.phone = firstStringValue(root, {QStringLiteral("phone"), QStringLiteral("client_phone"), QStringLiteral("customer_phone")});
    payload.notes = firstStringValue(root, {QStringLiteral("notes"), QStringLiteral("comment"), QStringLiteral("note")});
    payload.documentNumber = firstStringValue(root, {QStringLiteral("documentNumber"), QStringLiteral("docNumber"), QStringLiteral("invoiceNumber"), QStringLiteral("invoiceNo"), QStringLiteral("number")});
    payload.documentDate = firstStringValue(root, {QStringLiteral("documentDate"), QStringLiteral("docDate"), QStringLiteral("invoiceDate"), QStringLiteral("date")});
    payload.documentDate = normalizeDateToIso(payload.documentDate);

    if (payload.plate.isEmpty() && payload.targetType.compare(QStringLiteral("plate"), Qt::CaseInsensitive) == 0) {
        payload.plate = payload.targetId;
    }

    QJsonArray items = root.value(QStringLiteral("items")).toArray();
    if (items.isEmpty()) items = root.value(QStringLiteral("parts")).toArray();
    if (items.isEmpty()) items = root.value(QStringLiteral("positions")).toArray();
    for (const QJsonValue& v : items) {
        if (v.isObject()) appendPartFromObject(v.toObject(), payload.supplier, payload.documentDate, &payload.parts);
    }
    if (payload.parts.isEmpty()) {
        if (root.value(QStringLiteral("item")).isObject()) {
            appendPartFromObject(root.value(QStringLiteral("item")).toObject(), payload.supplier, payload.documentDate, &payload.parts);
        } else {
            appendPartFromObject(root, payload.supplier, payload.documentDate, &payload.parts);
        }
    }

    parseAttachments(root, &payload.attachments);
    return payload;
}

SyncPayload loadLatestPendingPayload()
{
    SyncPayload payload;
    if (!syncModuleEnabled()) return payload;

    const QDir dir(syncInboxDir());
    if (!dir.exists()) return payload;

    const QFileInfoList files = dir.entryInfoList(QStringList() << QStringLiteral("*.json"), QDir::Files | QDir::Readable, QDir::Time);
    for (const QFileInfo& fi : files) {
        if (fi.fileName().startsWith(QStringLiteral("."))) continue;
        if (fi.absoluteFilePath().contains(QStringLiteral("/processed/"), Qt::CaseInsensitive)) continue;
        const auto candidate = loadPayloadFromFile(fi.absoluteFilePath());
        if (candidate.isValid()) return candidate;
    }
    return payload;
}

QList<SyncPayload> listPendingPayloads(int limit)
{
    QList<SyncPayload> out;
    const QDir dir(syncInboxDir());
    if (!dir.exists()) return out;
    const QFileInfoList files = dir.entryInfoList(QStringList() << QStringLiteral("*.json"), QDir::Files | QDir::Readable, QDir::Time);
    for (const QFileInfo& fi : files) {
        if (fi.fileName().startsWith(QStringLiteral("."))) continue;
        if (fi.absoluteFilePath().contains(QStringLiteral("/processed/"), Qt::CaseInsensitive)) continue;
        const auto candidate = loadPayloadFromFile(fi.absoluteFilePath());
        if (!candidate.isValid()) continue;
        out.push_back(candidate);
        if (limit > 0 && out.size() >= limit) break;
    }
    return out;
}

bool markPayloadProcessed(const QString& sourceFile)
{
    if (sourceFile.trimmed().isEmpty()) return true;
    QFileInfo fi(sourceFile);
    if (!fi.exists()) return true;

    QDir dir = fi.dir();
    if (!dir.exists(QStringLiteral("processed"))) dir.mkpath(QStringLiteral("processed"));
    const QString suffix = fi.completeSuffix().isEmpty() ? QStringLiteral("json") : fi.completeSuffix();
    const QString stampedName = fi.completeBaseName()
        + QStringLiteral("__processed_")
        + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"))
        + QStringLiteral(".")
        + suffix;
    const QString targetPath = dir.filePath(QStringLiteral("processed/") + stampedName);
    SafeFileOps::safeRemoveFile(targetPath, {dir.absolutePath()});
    if (QFile::rename(sourceFile, targetPath)) return true;
    if (QFile::copy(sourceFile, targetPath)) {
        SafeFileOps::safeRemoveFile(sourceFile, {fi.absolutePath()});
        return true;
    }
    return false;
}

bool processPayloadIntoOrder(const SyncPayload& payload,
                             int orderNumber,
                             QString* errorText,
                             QString* infoText)
{
    if (errorText) errorText->clear();
    if (infoText) infoText->clear();
    if (!payload.isValid()) {
        if (errorText) *errorText = QString::fromUtf8("Sync-пакет порожній або пошкоджений.");
        return false;
    }
    if (orderNumber <= 0) {
        if (errorText) *errorText = QString::fromUtf8("Не вказано номер наряду для імпорту.");
        return false;
    }

    QString routingError;
    if (!Database::ensureOrderAvailableInActive(orderNumber, &routingError) || !Database::finalizeNewOrderWorkspaceState(orderNumber, &routingError)) {
        if (errorText) *errorText = routingError;
        return false;
    }

    QStringList infoParts;
    const bool importParts = Database::getSetting(QStringLiteral("settings.int.sync.auto_parts"), true).toBool();
    if (importParts && !payload.parts.isEmpty()) {
        QSqlDatabase db = Database::db();
        const auto mutationResult = MutationExecutor::execute(
            db,
            MutationExecutor::Request{
                QStringLiteral("SyncPayloadUtils"),
                QStringLiteral("order.parts"),
                QString::number(orderNumber),
                QStringLiteral("sync_import_parts"),
                QString::fromUtf8("Імпорт запчастин із sync payload"),
                {QStringLiteral("work_order_parts")},
                QString::fromUtf8("Не вдалося почати транзакцію імпорту sync-запчастин."),
                QString::fromUtf8("Не вдалося завершити транзакцію імпорту sync-запчастин.")
            },
            [&](QSqlDatabase& txDb, QString* stepError) {
                const bool autoMarkDelivered = payloadShouldAutoMarkDelivered(payload);
                for (const WorkOrderPartRow& row : payload.parts) {
                    WorkOrderPartRow effectiveRow = row;
                    if (autoMarkDelivered && effectiveRow.deliveryDate.trimmed().isEmpty()) {
                        effectiveRow.deliveryDate = currentDeliveryDateIso();
                    }
                    if (effectiveRow.clientPrice <= 0.0 || effectiveRow.purchasePrice <= 0.0 || effectiveRow.clientAmount <= 0.0 || effectiveRow.purchaseAmount <= 0.0) {
                        PartRowPayloadInput payloadInput;
                        payloadInput.orderedAt = effectiveRow.orderedAt;
                        payloadInput.deliveryDate = effectiveRow.deliveryDate;
                        payloadInput.installedAt = effectiveRow.installedAt;
                        payloadInput.store = effectiveRow.store;
                        payloadInput.partNumber = effectiveRow.partNumber;
                        payloadInput.name = effectiveRow.name;
                        payloadInput.note = effectiveRow.note;
                        payloadInput.manufacturer = effectiveRow.manufacturer;
                        payloadInput.qty = effectiveRow.qty;
                        payloadInput.purchasePrice = effectiveRow.purchasePrice > 0.0 ? effectiveRow.purchasePrice : effectiveRow.clientPrice;
                        payloadInput.sourceType = QStringLiteral("sync");
                        payloadInput.manualMarkupOverride = effectiveRow.manualMarkupOverride;
                        payloadInput.manualMarkupPercent = effectiveRow.markupPercent;
                        effectiveRow = PartRowPayloadBuilderService::build(payloadInput);
                    }
                    int newPartId = 0;
                    QString partError;
                    if (!PartRepository::insert(txDb, orderNumber, effectiveRow, &newPartId, &partError)) {
                        if (stepError) *stepError = partError;
                        return false;
                    }
                }
                return true;
            });
        if (!mutationResult.ok) {
            if (errorText) *errorText = mutationResult.errorText;
            return false;
        }
        BackupCommitPublisher::publish(
            BackupCommitPublisher::makeEvent(QStringLiteral("SyncPayloadUtils"),
                                             QStringLiteral("order.parts"),
                                             QStringLiteral("part_batch"),
                                             QString::number(payload.parts.size()),
                                             Database::resolveOrderUid(orderNumber),
                                             orderNumber,
                                             QStringLiteral("sync_import_parts"),
                                             QString::fromUtf8("Імпортовано запчастини з sync payload"),
                                             {QStringLiteral("work_order_parts")}));
        infoParts << QString::fromUtf8("Імпортовано запчастин: %1").arg(payload.parts.size());
    }

    QString attachmentInfo;
    if (!copyAttachmentsForOrder(payload, orderNumber, &attachmentInfo)) {
        if (errorText) *errorText = attachmentInfo;
        return false;
    }
    if (!attachmentInfo.trimmed().isEmpty()) infoParts << attachmentInfo;

    if (infoText) *infoText = infoParts.join(QStringLiteral("\n"));
    return true;
}

QString payloadDisplayLabel(const SyncPayload& payload)
{
    const QString moduleText = payload.module.trimmed().isEmpty() ? QString::fromUtf8("payload") : payload.module.trimmed();
    QString targetText;
    if (payload.targetsNewOrder()) {
        targetText = QString::fromUtf8("новий наряд");
    } else if (payload.targetType.compare(QStringLiteral("purchase"), Qt::CaseInsensitive) == 0) {
        targetText = payload.targetId.trimmed().isEmpty()
            ? QString::fromUtf8("закупівля")
            : QString::fromUtf8("закупівля %1").arg(payload.targetId.trimmed());
    } else if (payload.targetOrderNumber() > 0) {
        targetText = QString::fromUtf8("наряд %1").arg(payload.targetOrderNumber());
    } else {
        targetText = payload.targetType.trimmed().isEmpty() ? QString::fromUtf8("без цілі") : payload.targetType.trimmed();
    }
    const QString plateText = payload.plate.trimmed().isEmpty()
        ? (!payload.documentNumber.trimmed().isEmpty() ? payload.documentNumber.trimmed() : QString::fromUtf8("без номера"))
        : PlateUtils::formatPlateDisplay(payload.plate.trimmed());
    return QString::fromUtf8("%1 • %2 • %3 • позицій: %4 • вкладень: %5")
        .arg(moduleText, targetText, plateText)
        .arg(payload.parts.size())
        .arg(payload.attachments.size());
}

QString payloadPreviewText(const SyncPayload& payload)
{
    QStringList lines;
    lines << QString::fromUtf8("Файл: %1").arg(QDir::toNativeSeparators(payload.sourceFile));
    lines << QString::fromUtf8("Модуль: %1").arg(payload.module.isEmpty() ? QStringLiteral("-") : payload.module);
    lines << QString::fromUtf8("Канал: %1").arg(payload.transport.isEmpty() ? QStringLiteral("-") : payload.transport);
    lines << QString::fromUtf8("Ціль: %1 / %2").arg(payload.targetType.isEmpty() ? QStringLiteral("-") : payload.targetType,
                                                     payload.targetId.isEmpty() ? QStringLiteral("-") : payload.targetId);
    lines << QString::fromUtf8("Номер авто: %1").arg(payload.plate.isEmpty() ? QStringLiteral("-") : PlateUtils::formatPlateDisplay(payload.plate));
    lines << QString::fromUtf8("VIN: %1").arg(payload.vin.isEmpty() ? QStringLiteral("-") : payload.vin);
    lines << QString::fromUtf8("Авто: %1").arg(payload.car.isEmpty() ? QStringLiteral("-") : payload.car);
    lines << QString::fromUtf8("Клієнт: %1").arg(payload.client.isEmpty() ? QStringLiteral("-") : payload.client);
    lines << QString::fromUtf8("Телефон: %1").arg(payload.phone.isEmpty() ? QStringLiteral("-") : payload.phone);
    lines << QString::fromUtf8("Постачальник: %1").arg(payload.supplier.isEmpty() ? QStringLiteral("-") : payload.supplier);
    lines << QString::fromUtf8("Документ: %1  %2").arg(payload.documentNumber.isEmpty() ? QStringLiteral("-") : payload.documentNumber,
                                                         payload.documentDate.isEmpty() ? QStringLiteral("-") : payload.documentDate);
    if (!payload.notes.trimmed().isEmpty()) {
        lines << QString();
        lines << QString::fromUtf8("Нотатки:");
        lines << payload.notes.trimmed();
    }
    if (!payload.parts.isEmpty()) {
        lines << QString();
        lines << QString::fromUtf8("Позиції (%1):").arg(payload.parts.size());
        for (const WorkOrderPartRow& row : payload.parts) {
            lines << QString::fromUtf8("• %1 | %2 | qty=%3 | price=%4 | total=%5")
                        .arg(row.partNumber.isEmpty() ? QStringLiteral("-") : row.partNumber,
                             row.name.isEmpty() ? QStringLiteral("-") : row.name,
                             QString::number(row.qty, 'f', row.qty == static_cast<int>(row.qty) ? 0 : 3),
                             QString::number(row.purchasePrice, 'f', 2),
                             QString::number(row.purchaseAmount, 'f', 2));
        }
    }
    if (!payload.attachments.isEmpty()) {
        lines << QString();
        lines << QString::fromUtf8("Вкладення (%1):").arg(payload.attachments.size());
        for (const SyncAttachmentRef& att : payload.attachments) {
            lines << QString::fromUtf8("• %1 -> %2")
                        .arg(att.key.isEmpty() ? QStringLiteral("attachment") : att.key,
                             att.uri.isEmpty() ? att.localPath : att.uri);
        }
    }
    return lines.join(QStringLiteral("\n"));
}

} // namespace SyncPayloadUtils
