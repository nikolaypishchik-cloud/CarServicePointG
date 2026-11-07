#include "core/services/WorkshopSpareStockService.h"
#include "infrastructure/db/sqlite/Db.h"
#include "core/util/StoreNameNormalizer.h"
#include "core/services/WorkshopInventoryService.h"
#include "core/services/PurchasePaymentService.h"
#include "core/services/SparePaymentAllocation.h"
#include "core/services/ActivityAuditService.h"
#include "core/services/AccessControlService.h"
#include "core/services/orders/WorkPartsPersistenceRepositoryFacade.h"
#include "core/services/orders/PartPricingEngine.h"
#include "core/services/orders/OrderAttachmentService.h"
#include "core/services/storage/StorageDirectoriesGuard.h"
#include "core/cache/SnapshotCacheInvalidator.h"
#include "core/backup/BackupCommitPublisher.h"
#include "runtime/RuntimeServices.h"
#include "core/util/PlateUtils.h"
#include "core/util/VehicleNameUtils.h"
#include "core/services/catalog/CatalogMutationService.h"
#include "core/services/SupplierInvoiceLedger.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QJsonObject>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QFileInfo>
#include <QScopeGuard>
#include <QUuid>
#include <cmath>
#include <QCryptographicHash>
#include <QJsonArray>

namespace {
bool fail(QString* error, const QString& text) { if (error) *error = text; return false; }
QString text(const QString& s) { return s.trimmed().isEmpty() ? QStringLiteral("") : s.trimmed(); }
QString plateKey(const QString& s) { return PlateUtils::normalizePlate(s); }
bool sameModel(const WorkshopSpareOrder& reserve, const QString& car) {
    if (reserve.brand.trimmed().isEmpty() || reserve.model.trimmed().isEmpty()) return false;
    const QString model = VehicleNameUtils::normalizeVehicleModel(reserve.brand, reserve.model);
    const auto key = [](const QString& value) {
        return VehicleNameUtils::normalizeVehicleFreeText(value).simplified().toCaseFolded();
    };
    return key(reserve.brand + " " + model) == key(car);
}
bool allowed(QSqlDatabase db, const QString& permission, QString* error) {
    return !ActivityAuditService::currentActor().isValid()
        || AccessControlService::currentUserHasPermission(db, permission)
        || fail(error, QString::fromUtf8("Недостатньо прав для цієї складської операції."));
}
class StockTransaction {
    QSqlQuery query;
    bool active = false;
public:
    explicit StockTransaction(QSqlDatabase db) : query(db) {}
    bool begin(QString* error) { active=query.exec("SAVEPOINT spare_stock"); return active || fail(error, query.lastError().text()); }
    ~StockTransaction() { if(active) { query.exec("ROLLBACK TO spare_stock"); query.exec("RELEASE spare_stock"); } }
    bool commit(QString* error) {
        if(!query.exec("RELEASE spare_stock")) return fail(error,query.lastError().text());
        active=false; return true;
    }
};
bool audit(QSqlDatabase db, const QString& action, qint64 id, const QJsonObject& payload, QString* error) {
    return ActivityAuditService::record(db,"inventory",action,QString::fromUtf8("Запчастини під авто"),0,
        "workshop_spare_order",QString::number(id),QString::number(id),
        QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact)),error);
}
void changed(qint64 id) {
    SnapshotCacheInvalidator::invalidateOrdersAndParts("spare_stock");
    SnapshotCacheInvalidator::invalidateReportsAndStats("spare_stock");
    if(RuntimeServices::backupManager()) BackupCommitPublisher::publish(BackupCommitPublisher::makeEvent(
        "inventory","spare_stock","workshop_spare_order",QString::number(id),{},0,"spare_stock",QString::number(id),
        {"workshop_spare_orders","workshop_spare_movements","workshop_inventory_purchases","purchase_payment_balances",
         "purchase_payment_events","purchase_payment_terms","work_order_parts","work_orders","activity_audit_events",
         "workshop_spare_attachments","work_order_part_attachments","workshop_spare_payment_allocations",
         "parts_catalog","workshop_spare_references","workshop_spare_returns","returned_inventory_purchases","workshop_spare_invoice_identity"}));
}
bool claim(QSqlDatabase db, const WorkshopSpareOrder& expected, QString* error) {
    QSqlQuery q(db); q.prepare("UPDATE workshop_spare_orders SET revision=revision+1 WHERE id=? AND revision=?");
    q.addBindValue(expected.id); q.addBindValue(expected.revision);
    if(!q.exec()) return fail(error,q.lastError().text());
    return q.numRowsAffected()==1 || fail(error,QString::fromUtf8("Запис змінився. Оновіть реєстр і повторіть операцію."));
}
}
namespace WorkshopSpareStockService {
bool importTargets(QVector<ImportTarget>* result, QString* error) {
    if (error) error->clear();
    if (!result) return false;
    result->clear();
    const auto db = Database::db();
    QSqlQuery query(db);
    if (!query.exec("SELECT number,order_number,car,plate FROM work_orders WHERE TRIM(COALESCE(closed_at,''))='' AND LOWER(COALESCE(status,''))<>'closed' ORDER BY number DESC"))
        return fail(error,query.lastError().text());
    QVector<ImportTarget> targets;
    while (query.next()) {
        const int number = query.value(0).toInt();
        if (AccessControlService::canAccessOrder(db,number))
            targets.append({number,query.value(1).toInt(),query.value(2).toString(),query.value(3).toString()});
    }
    if (query.lastError().isValid()) return fail(error,query.lastError().text());
    *result = targets;
    return true;
}
bool issue(const WorkshopSpareOrder& expected,double quantity,int orderNumber,QString* error,bool confirmDifferentVehicle) {
    return issue(Database::db(),expected,quantity,orderNumber,error,confirmDifferentVehicle);
}
bool cancelDraftIssue(qint64 spareId,int orderNumber,QString* error) {
    return cancelDraftIssue(Database::db(),spareId,orderNumber,error);
}
bool returnReserve(QSqlDatabase db,const WorkshopSpareOrder& expected,const QString& suppliedOem,const QString& suppliedNode,QString* error,const QString& settlement,qint64 expectedPaidCents) {
    QString oem=suppliedOem,node=suppliedNode;
    if(error)error->clear();
    if(!ensureSchema(db,error) || !allowed(db,"inventory.delete",error))return false;
    StockTransaction tx(db);if(!tx.begin(error) || !claim(db,expected,error))return false;
    QSqlQuery q(db);
    q.prepare("SELECT COUNT(*) FROM workshop_spare_movements WHERE spare_id=? AND operation='issue'");q.addBindValue(expected.id);
    if(!q.exec() || !q.next())return fail(error,q.lastError().text());
    if(q.value(0).toInt()!=0)return fail(error,QString::fromUtf8("Спочатку поверніть видані позиції з наряду на склад. Установлені позиції не можна видаляти як складський залишок."));
    q.prepare("SELECT 1 FROM workshop_spare_returns WHERE spare_id=?");q.addBindValue(expected.id);
    if(!q.exec())return fail(error,q.lastError().text());if(q.next())return fail(error,QString::fromUtf8("Повернення вже проведено."));q.finish();
    if(!PurchasePaymentService::ensureSchema(db,error))return false;
    const auto links=SupplierInvoiceLedger::activeLinks(db);
    QList<qint64> purchases;qint64 paid=0;
    q.prepare("SELECT m.purchase_id,b.paid_cents FROM workshop_spare_movements m LEFT JOIN purchase_payment_balances b ON b.payment_key='inventory:'||m.purchase_id||':' WHERE m.spare_id=? AND m.operation='receipt'");
    q.addBindValue(expected.id);if(!q.exec())return fail(error,q.lastError().text());
    while(q.next()) {
        const auto id=q.value(0).toLongLong();
        if(links.contains(PurchasePaymentService::key("inventory",id)))return fail(error,QString::fromUtf8("Поставка має зовнішню накладну. Спочатку узгодьте повернення та прив’язку накладної з постачальником; її борг не можна приховати локальним видаленням."));
        if(q.value(1).isNull())return fail(error,QString::fromUtf8("Потрібна звірка оплати поставки."));
        paid+=q.value(1).toLongLong();purchases.append(id);
    }q.finish();
    if(paid>0 && (expectedPaidCents!=paid || (settlement!="refund" && settlement!="credit")))return fail(error,QString::fromUtf8("Оновіть резерв і підтвердіть актуальну суму та спосіб повернення коштів."));
    if(paid==0 && expectedPaidCents>0)return fail(error,QString::fromUtf8("Оплата змінилася. Оновіть резерв."));
    q.prepare("SELECT name,brand,model FROM workshop_spare_orders WHERE id=?");q.addBindValue(expected.id);
    if(!q.exec() || !q.next())return fail(error,q.lastError().text());
    auto name=q.value(0).toString(),brand=q.value(1).toString(),model=q.value(2).toString();q.finish();
    q.prepare("SELECT payload FROM workshop_spare_invoice_identity WHERE spare_id=?");q.addBindValue(expected.id);
    if(!q.exec())return fail(error,q.lastError().text());
    if(q.next()) {
        const auto identity=QJsonDocument::fromJson(q.value(0).toByteArray()).object();
        name=identity["name"].toString().trimmed();brand=identity["brand"].toString().trimmed();model=identity["model"].toString().trimmed();
        oem=identity["oem"].toString().trimmed();node=identity["node"].toString().trimmed();
    }q.finish();
    if(!name.isEmpty() && !oem.trimmed().isEmpty() && !node.trimmed().isEmpty() && !brand.isEmpty() && !model.isEmpty()) {
        if(!q.exec("SELECT 1 FROM main.sqlite_master WHERE type='table' AND name='parts_catalog'") || !q.next())
            return fail(error,QString::fromUtf8("Каталог має бути у робочій БД для спільного збереження. Видалення не виконано."));
        q.prepare("SELECT id FROM parts_catalog WHERE lower(trim(vehicle_brand))=lower(trim(?)) AND lower(trim(vehicle_model))=lower(trim(?)) AND lower(trim(oem_number))=lower(trim(?)) AND lower(trim(group_name))=lower(trim(?)) AND lower(trim(name))=lower(trim(?))");
        for(const auto& v:{brand,model,oem.trimmed(),node.trimmed(),name})q.addBindValue(v);
        if(!q.exec())return fail(error,q.lastError().text());
        if(!q.next()) {
            CatalogMutationService::SaveParams p;p.brand=brand;p.model=model;p.oem=oem.trimmed();p.group=node.trimmed();p.name=name;
            p.fuel="";p.brandName="";p.supplier="";p.note="";
            const auto saved=CatalogMutationService::save(db,p);if(!saved.ok)return fail(error,saved.errorText);
        }
    }
    q.prepare("DELETE FROM workshop_spare_invoice_identity WHERE spare_id=?");q.addBindValue(expected.id);
    if(!q.exec())return fail(error,q.lastError().text());
    QString path;
    if(!purchases.isEmpty()) {
        // Financial history remains immutable. The reversal removes only the live obligation/stock.
        q.prepare("INSERT INTO workshop_spare_returns(spare_id,supplier,paid_cents,settlement,actor) SELECT id,supplier,?,?,? FROM workshop_spare_orders WHERE id=?");
        q.addBindValue(paid);q.addBindValue(paid>0?settlement:QString("unpaid"));q.addBindValue(text(ActivityAuditService::currentActor().displayName));q.addBindValue(expected.id);
        if(!q.exec())return fail(error,q.lastError().text());
        for(const auto id:purchases) {
            q.prepare("INSERT INTO returned_inventory_purchases(purchase_id,spare_id) VALUES(?,?)");q.addBindValue(id);q.addBindValue(expected.id);
            if(!q.exec())return fail(error,q.lastError().text());
        }
        if(!audit(db,"spare_return",expected.id,{{"paid_cents",paid},{"settlement",settlement}},error) || !tx.commit(error))return false;
        changed(expected.id);return true;
    }
    q.prepare("SELECT file_path FROM workshop_spare_attachments WHERE spare_id=?");q.addBindValue(expected.id);
    if(!q.exec())return fail(error,q.lastError().text());if(q.next())path=q.value(0).toString();q.finish();
    for(const auto& sql:{"DELETE FROM workshop_spare_attachments WHERE spare_id=?","DELETE FROM workshop_spare_orders WHERE id=?"}) {
        q.prepare(sql);q.addBindValue(expected.id);if(!q.exec())return fail(error,q.lastError().text());
    }
    if(!audit(db,"spare_return",expected.id,{},error) || !tx.commit(error))return false;
    if(!path.isEmpty())OrderAttachmentService::removeUnreferencedManagedFile(db,path);
    changed(expected.id);return true;
}
bool ensureSchema(QSqlDatabase db, QString* error) {
    if(!WorkshopInventoryService::ensureSchema(db,error)) return false;
    QSqlQuery q(db);
    for(const auto& sql : {
        QStringLiteral("CREATE TABLE IF NOT EXISTS workshop_spare_invoice_identity(spare_id INTEGER PRIMARY KEY REFERENCES workshop_spare_orders(id),payload TEXT NOT NULL)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS workshop_spare_returns(spare_id INTEGER PRIMARY KEY,supplier TEXT NOT NULL,paid_cents INTEGER NOT NULL CHECK(paid_cents>=0),settlement TEXT NOT NULL CHECK(settlement IN ('unpaid','refund','credit')),recorded_at TEXT NOT NULL DEFAULT(datetime('now')),actor TEXT NOT NULL DEFAULT '')"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS workshop_spare_orders (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL, article TEXT NOT NULL DEFAULT '', brand TEXT NOT NULL, model TEXT NOT NULL, plate TEXT NOT NULL, vin TEXT NOT NULL DEFAULT '', master_id TEXT NOT NULL, master_name TEXT NOT NULL, supplier TEXT NOT NULL, shelf TEXT NOT NULL DEFAULT '', document TEXT NOT NULL DEFAULT '', ordered_on TEXT NOT NULL, expected_on TEXT NOT NULL DEFAULT '', due_on TEXT NOT NULL DEFAULT '', quantity REAL NOT NULL CHECK(quantity>0), unit_cost REAL NOT NULL CHECK(unit_cost>=0), client_price REAL NOT NULL DEFAULT -1, revision INTEGER NOT NULL DEFAULT 0)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS workshop_spare_movements (id INTEGER PRIMARY KEY AUTOINCREMENT, spare_id INTEGER NOT NULL REFERENCES workshop_spare_orders(id), operation TEXT NOT NULL CHECK(operation IN ('receipt','issue')), quantity REAL NOT NULL CHECK(quantity>0), moved_on TEXT NOT NULL, purchase_id INTEGER REFERENCES workshop_inventory_purchases(id), part_id INTEGER, order_number INTEGER, actor TEXT NOT NULL DEFAULT '')"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS workshop_spare_movements_order ON workshop_spare_movements(spare_id,id)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS workshop_spare_attachments (spare_id INTEGER PRIMARY KEY REFERENCES workshop_spare_orders(id), file_path TEXT NOT NULL, display_name TEXT NOT NULL)")}) {
        if(!q.exec(sql)) return fail(error,q.lastError().text());
    }
    return true;
}
bool addOrder(QSqlDatabase db, const WorkshopSpareOrder& in, qint64* id, QString* error, const QDate& initialReceiptDate, bool publishCommitted) {
    if(error) error->clear();
    if(!ensureSchema(db,error) || !allowed(db,"inventory.purchase",error)) return false;
    if(text(in.name).isEmpty() || text(in.brand).isEmpty() || text(in.model).isEmpty()
        || text(in.supplier).isEmpty() || !in.orderedOn.isValid()
        || !std::isfinite(in.quantity) || in.quantity<=0 || in.quantity>1000000
        || std::floor(in.quantity)!=in.quantity || PurchasePaymentService::cents(in.unitCost)<0 || PurchasePaymentService::cents(in.unitCost*in.quantity)<0
        || !std::isfinite(in.clientPrice) || (in.clientPrice<0 && in.clientPrice!=-1)
        || (!text(in.vin).isEmpty() && !QRegularExpression("^[A-HJ-NPR-Z0-9]{17}$").match(in.vin.toUpper()).hasMatch())
        || (in.expectedOn.isValid() && in.expectedOn<in.orderedOn)
        || (in.dueOn.isValid() && in.dueOn<in.orderedOn))
        return fail(error,QString::fromUtf8("Перевірте авто, запчастину, кількість, ціну та дати. VIN необов'язковий; за наявності має містити 17 символів."));
    QSqlQuery master(db); master.prepare("SELECT name FROM masters WHERE id=? AND active=1 AND role='master'");
    master.addBindValue(in.masterId);
    if(!master.exec()) return fail(error,master.lastError().text());
    if(!master.next()) return fail(error,QString::fromUtf8("Оберіть чинного майстра, який замовив запчастину."));
    QString invoicePath;
    if (!in.invoicePath.isEmpty()) {
        invoicePath = StorageDirectoriesGuard::copyFileToManagedStorageAs(
            StorageDirectoriesGuard::StorageKind::Invoice, 0, "spare_stock",
            QStringLiteral("spare_%1.%2").arg(QUuid::createUuid().toString(QUuid::WithoutBraces), QFileInfo(in.invoicePath).suffix()),
            in.invoicePath, error);
        if (invoicePath.isEmpty()) return false;
    }
    bool saved = false;
    const auto cleanup = qScopeGuard([&] {
        if (!saved && !invoicePath.isEmpty()) OrderAttachmentService::removeUnreferencedManagedFile(db, invoicePath);
    });
    StockTransaction tx(db); if(!tx.begin(error)) return false;
    QSqlQuery q(db); q.prepare("INSERT INTO workshop_spare_orders(name,article,brand,model,plate,vin,master_id,master_name,supplier,shelf,document,ordered_on,expected_on,due_on,quantity,unit_cost,client_price) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
    for(const auto& value : {text(in.name),text(in.article),VehicleNameUtils::normalizeVehicleFreeText(in.brand).toUpper(),VehicleNameUtils::normalizeVehicleModel(in.brand,in.model).toUpper(),text(PlateUtils::formatPlateDisplay(in.plate)),text(in.vin).toUpper(),text(in.masterId),master.value(0).toString(),text(in.supplier),text(in.shelf),text(in.document),in.orderedOn.toString(Qt::ISODate),text(in.expectedOn.toString(Qt::ISODate)),text(in.dueOn.toString(Qt::ISODate))}) q.addBindValue(value);
    q.addBindValue(in.quantity); q.addBindValue(in.unitCost); q.addBindValue(in.clientPrice);
    if(!q.exec()) return fail(error,q.lastError().text());
    const auto created=q.lastInsertId().toLongLong();
    if(!invoicePath.isEmpty() && !in.invoiceCatalogIdentity.isEmpty()) {
        QSqlQuery identity(db);identity.prepare("INSERT INTO workshop_spare_invoice_identity(spare_id,payload) VALUES(?,?)");
        identity.addBindValue(created);identity.addBindValue(QString::fromUtf8(QJsonDocument(in.invoiceCatalogIdentity).toJson(QJsonDocument::Compact)));
        if(!identity.exec())return fail(error,identity.lastError().text());
    }
    if (!invoicePath.isEmpty()) {
        QSqlQuery attachment(db);
        attachment.prepare("INSERT INTO workshop_spare_attachments(spare_id,file_path,display_name) VALUES(?,?,?)");
        attachment.addBindValue(created); attachment.addBindValue(invoicePath);
        attachment.addBindValue(in.invoiceName.isEmpty() ? QFileInfo(in.invoicePath).fileName() : in.invoiceName);
        if (!attachment.exec()) return fail(error, attachment.lastError().text());
    }
    if(!audit(db,"spare_order",created,{{"name",in.name},{"plate",in.plate},{"master_id",in.masterId},{"quantity",in.quantity},{"ordered_on",in.orderedOn.toString(Qt::ISODate)}},error)) return false;
    if (initialReceiptDate.isValid()) {
        auto expected = in;
        expected.id = created;
        expected.revision = 0;
        if (!receive(db, expected, in.quantity, initialReceiptDate, 0, error, false)) return false;
    }
    if (!tx.commit(error)) return false;
    saved = true;
    if(id) *id=created; if(publishCommitted) changed(created); return true;
}
bool addBatch(QSqlDatabase db, const QVector<BatchLine>& lines, const QDate& receiptDate, QString* error) {
    if(error) error->clear();
    if(lines.isEmpty() || !ensureSchema(db,error) || !allowed(db,"inventory.purchase",error)) return false;
    QStringList copied; bool saved=false;
    const auto cleanup=qScopeGuard([&]{if(!saved) for(const auto& path:copied) OrderAttachmentService::removeUnreferencedManagedFile(db,path);});
    StockTransaction tx(db); if(!tx.begin(error)) return false;
    QString receiptKey;
    QStringList identities;
    bool invoiceBatch=true;
    for(const auto& line:lines){const auto& in=line.part;if(in.document.trimmed().isEmpty()||in.invoicePath.trimmed().isEmpty()){invoiceBatch=false;break;}
        const QJsonArray identity{in.document.trimmed(),in.supplier.trimmed().toCaseFolded(),in.orderedOn.toString(Qt::ISODate),in.article.trimmed(),in.name.trimmed(),in.quantity,in.unitCost,plateKey(in.plate),line.orderNumber};
        identities.append(QString::fromUtf8(QJsonDocument(identity).toJson(QJsonDocument::Compact)));}
    if(invoiceBatch){identities.sort();receiptKey=QString::fromLatin1(QCryptographicHash::hash(identities.join('\n').toUtf8(),QCryptographicHash::Sha256).toHex());
        QSqlQuery receipt(db);if(!receipt.exec("CREATE TABLE IF NOT EXISTS workshop_spare_import_receipts (request_hash TEXT PRIMARY KEY, created_at TEXT NOT NULL)"))return fail(error,receipt.lastError().text());
        receipt.prepare("SELECT 1 FROM workshop_spare_import_receipts WHERE request_hash=?");receipt.addBindValue(receiptKey);if(!receipt.exec())return fail(error,receipt.lastError().text());
        if(receipt.next())return fail(error,QString::fromUtf8("Ці позиції накладної вже імпортовано. Повторне додавання заблоковано."));}
    for(const auto& line:lines) {
        const auto& in=line.part;
        if(line.orderNumber==0) {
            qint64 id=0;
            if(!addOrder(db,in,&id,error,receiptDate,false)) return false;
            QSqlQuery q(db);q.prepare("SELECT file_path FROM workshop_spare_attachments WHERE spare_id=?");q.addBindValue(id);
            if(!q.exec())return fail(error,q.lastError().text());if(q.next())copied<<q.value(0).toString();
            continue;
        }
        if(!allowed(db,"parts.order",error) || !AccessControlService::canAccessOrder(db,line.orderNumber))
            return fail(error,QString::fromUtf8("Наряд недоступний."));
        QSqlQuery order(db);order.prepare("SELECT closed_at,status,order_uid FROM work_orders WHERE number=?");order.addBindValue(line.orderNumber);
        if(!order.exec())return fail(error,order.lastError().text());
        if(!order.next() || !order.value(0).toString().trimmed().isEmpty() || order.value(1).toString().compare("closed",Qt::CaseInsensitive)==0)
            return fail(error,QString::fromUtf8("Оберіть відкритий наряд."));
        if(in.name.trimmed().isEmpty() || in.supplier.trimmed().isEmpty() || !in.orderedOn.isValid()
            || !std::isfinite(in.quantity) || in.quantity<=0 || in.quantity>1000000 || std::floor(in.quantity)!=in.quantity
            || PurchasePaymentService::cents(in.unitCost)<0 || PurchasePaymentService::cents(in.unitCost*in.quantity)<0
            || (in.dueOn.isValid() && in.dueOn<in.orderedOn)
            || (receiptDate.isValid() && (receiptDate<in.orderedOn || receiptDate>QDate::currentDate())))
            return fail(error,QString::fromUtf8("Перевірте назву, кількість, ціну, постачальника та дату."));
        PartPricingInput pricing;pricing.partName=in.name;pricing.store=in.supplier;pricing.qty=in.quantity;pricing.purchasePrice=in.unitCost;
        auto policy=PartMarkupPolicyService::loadPolicySnapshot(db);policy.inventoryClientPrices.clear();policy.inventoryPurchasePrices.clear();
        const auto calculated=PartPricingEngine::calculate(pricing,policy);
        WorkOrderPartRow part;part.name=in.name;part.partNumber=in.article;part.store=in.supplier;part.qty=in.quantity;
        part.orderedAt=in.orderedOn.toString(Qt::ISODate);part.deliveryDate=receiptDate.toString(Qt::ISODate);
        part.purchasePrice=calculated.purchasePrice;part.purchaseAmount=calculated.purchaseAmount;
        part.clientPrice=calculated.clientPrice;part.clientAmount=calculated.clientAmount;part.markupPercent=calculated.markupPercent;part.markupSource=calculated.markupSource;
        int id=0;if(!WorkPartsPersistenceRepositoryFacade::insertPart(db,line.orderNumber,part,&id,error))return false;
        if(in.dueOn.isValid()) {
            if(!PurchasePaymentService::ensureSchema(db,error))return false;
            QSqlQuery terms(db);terms.prepare("INSERT INTO purchase_payment_terms(payment_key,due_date) VALUES(?,?)");
            terms.addBindValue(PurchasePaymentService::key("part",id,order.value(2).toString()));
            terms.addBindValue(in.dueOn.toString(Qt::ISODate));
            if(!terms.exec())return fail(error,terms.lastError().text());
        }
        QString path;
        if(!in.invoicePath.isEmpty()) {
            path=StorageDirectoriesGuard::copyFileToManagedStorageAs(StorageDirectoriesGuard::StorageKind::Invoice,0,"spare_stock",
                QStringLiteral("batch_%1.%2").arg(QUuid::createUuid().toString(QUuid::WithoutBraces),QFileInfo(in.invoicePath).suffix()),in.invoicePath,error);
            if(path.isEmpty())return false;copied<<path;
        }
        if((!path.isEmpty() || !in.document.isEmpty()) && !OrderAttachmentService::setPartAttachment(db,id,path,in.invoiceName,in.document,in.orderedOn.toString(Qt::ISODate),error))return false;
        if(!audit(db,"invoice_to_order",id,{{"order_number",line.orderNumber},{"quantity",in.quantity}},error))return false;
    }
    if(!receiptKey.isEmpty()){QSqlQuery receipt(db);receipt.prepare("INSERT INTO workshop_spare_import_receipts(request_hash,created_at) VALUES(?,datetime('now'))");receipt.addBindValue(receiptKey);if(!receipt.exec())return fail(error,receipt.lastError().text());}
    if(!tx.commit(error))return false;
    saved=true;changed(0);return true;
}
QVector<WorkshopSpareOrder> orders(QSqlDatabase db, QString* error, bool initializeSchema) {
    QVector<WorkshopSpareOrder> result;
    if(error) error->clear();
    if(initializeSchema && !ensureSchema(db,error)) return result;
    const auto paid=PurchasePaymentService::balances(db,error);
    if(error && !error->isEmpty()) return result;
    QSet<qint64> returned;
    QSqlQuery returnRows(db);if(!returnRows.exec("SELECT spare_id FROM workshop_spare_returns")){fail(error,returnRows.lastError().text());return {};}
    while(returnRows.next())returned.insert(returnRows.value(0).toLongLong());if(returnRows.lastError().isValid()){fail(error,returnRows.lastError().text());return {};}returnRows.finish();
    QSqlQuery q(db);
    if(!q.exec("SELECT s.id,s.name,s.article,s.brand,s.model,s.plate,s.vin,s.master_id,s.master_name,s.supplier,s.shelf,s.document,s.ordered_on,s.expected_on,s.due_on,s.quantity,s.unit_cost,s.client_price,s.revision,COALESCE(SUM(CASE WHEN m.operation='receipt' THEN m.quantity ELSE 0 END),0),COALESCE(SUM(CASE WHEN m.operation='issue' THEN m.quantity ELSE 0 END),0),MAX(CASE WHEN m.operation='receipt' THEN m.moved_on END) FROM workshop_spare_orders s LEFT JOIN workshop_spare_movements m ON m.spare_id=s.id GROUP BY s.id ORDER BY s.id DESC")) { fail(error,q.lastError().text()); return result; }
    while(q.next()) {
        if(returned.contains(q.value(0).toLongLong()))continue;
        WorkshopSpareOrder r; r.id=q.value(0).toLongLong(); r.name=q.value(1).toString(); r.article=q.value(2).toString();
        r.brand=q.value(3).toString(); r.model=q.value(4).toString(); r.plate=q.value(5).toString(); r.vin=q.value(6).toString();
        r.masterId=q.value(7).toString(); r.masterName=q.value(8).toString(); r.supplier=StoreNameNormalizer::canonicalDisplayName(q.value(9).toString());
        r.shelf=q.value(10).toString(); r.document=q.value(11).toString(); r.orderedOn=QDate::fromString(q.value(12).toString(),Qt::ISODate);
        r.expectedOn=QDate::fromString(q.value(13).toString(),Qt::ISODate); r.dueOn=QDate::fromString(q.value(14).toString(),Qt::ISODate);
        r.quantity=q.value(15).toDouble(); r.unitCost=q.value(16).toDouble(); r.clientPrice=q.value(17).toDouble(); r.revision=q.value(18).toInt();
        r.received=q.value(19).toDouble(); r.issued=q.value(20).toDouble(); r.deliveredOn=QDate::fromString(q.value(21).toString(),Qt::ISODate); result.append(r);
    }
    if(q.lastError().isValid()){fail(error,q.lastError().text());return {};}
    QSqlQuery attachments(db);
    if (!attachments.exec("SELECT spare_id,file_path,display_name FROM workshop_spare_attachments")) {
        fail(error, attachments.lastError().text()); return {};
    }
    QHash<qint64, QPair<QString, QString>> files;
    while (attachments.next()) files.insert(attachments.value(0).toLongLong(), {attachments.value(1).toString(), attachments.value(2).toString()});
    if(attachments.lastError().isValid()){fail(error,attachments.lastError().text());return {};}
    for (auto& r : result) {
        const auto file = files.value(r.id);
        r.invoicePath = file.first; r.invoiceName = file.second;
    }
    QSqlQuery debt(db);
    if(!debt.exec("SELECT m.spare_id,p.id,p.quantity,p.unit_cost,m.moved_on FROM workshop_spare_movements m JOIN workshop_inventory_purchases p ON p.id=m.purchase_id WHERE m.operation='receipt'")) { fail(error,debt.lastError().text()); return {}; }
    QHash<qint64,qint64> totals;
    QHash<qint64,QDate> oldestUnpaid;
    while(debt.next()) {
        const auto id=debt.value(0).toLongLong();
        const auto remaining=qMax<qint64>(0,PurchasePaymentService::cents(debt.value(2).toDouble()*debt.value(3).toDouble())-paid.value(PurchasePaymentService::key("inventory",debt.value(1).toLongLong()),0));
        totals[id]+=remaining;
        const auto received=QDate::fromString(debt.value(4).toString(),Qt::ISODate);
        if(remaining>0 && received.isValid() && (!oldestUnpaid.value(id).isValid() || received<oldestUnpaid.value(id))) oldestUnpaid[id]=received;
    }
    if(debt.lastError().isValid()){fail(error,debt.lastError().text());return {};}
    const auto today=QDate::currentDate();
    for(auto& r:result) {
        r.debtCents=totals.value(r.id);
        if(oldestUnpaid.contains(r.id)) r.unpaidDays=qMax(0,int(oldestUnpaid.value(r.id).daysTo(today)));
    }
    return result;
}
QVector<WorkshopSpareReceipt> receipts(QSqlDatabase db,qint64 id,QString* error) {
    QVector<WorkshopSpareReceipt> result; QSqlQuery q(db);
    q.prepare("SELECT purchase_id,moved_on,quantity FROM workshop_spare_movements WHERE spare_id=? AND operation='receipt' ORDER BY id"); q.addBindValue(id);
    if(!q.exec()) { fail(error,q.lastError().text()); return result; }
    while(q.next()) result.append({q.value(0).toLongLong(),QDate::fromString(q.value(1).toString(),Qt::ISODate),q.value(2).toDouble()});
    return result;
}
bool receive(QSqlDatabase db,const WorkshopSpareOrder& expected,double qty,const QDate& date,double paid,QString* error,bool publishCommitted) {
    if(error) error->clear();
    if(!ensureSchema(db,error) || !allowed(db,"inventory.purchase",error)) return false;
    StockTransaction tx(db); if(!tx.begin(error) || !claim(db,expected,error)) return false;
    WorkshopSpareOrder current; for(const auto& r:orders(db,error)) if(r.id==expected.id) current=r;
    if(!current.id || !date.isValid() || date<current.orderedOn || date>QDate::currentDate()
        || !std::isfinite(qty) || qty<=0 || std::floor(qty)!=qty || qty>current.quantity-current.received
        || PurchasePaymentService::cents(paid)<0 || paid>qty*current.unitCost)
        return fail(error,QString::fromUtf8("Перевірте дату приймання, кількість та оплату. Приймати більше замовленого не можна."));
    WorkshopInventoryPurchaseInput input;
    input.publishCommitted=false;
    input.stockKind="spare"; input.purchasedOn=date; input.orderedOn=current.orderedOn; input.dueOn=current.dueOn;
    input.materialName=current.name; input.unit=QString::fromUtf8("шт."); input.quantity=qty; input.unitCost=current.unitCost;
    input.supplier=current.supplier; input.paidAmount=paid; input.clientPrice=current.clientPrice;
    input.shelf=current.shelf; input.article=current.article; input.document=current.document;
    input.note=QString::fromUtf8("Резерв: %1 %2 | %3 | %4 | %5").arg(current.brand,current.model,current.plate,current.masterName,current.document);
    qint64 purchase=0; if(!WorkshopInventoryService::addPurchase(db,input,&purchase,error)) return false;
    QSqlQuery q(db); q.prepare("INSERT INTO workshop_spare_movements(spare_id,operation,quantity,moved_on,purchase_id,actor) VALUES(?,'receipt',?,?,?,?)");
    q.addBindValue(current.id); q.addBindValue(qty); q.addBindValue(date.toString(Qt::ISODate)); q.addBindValue(purchase); q.addBindValue(text(ActivityAuditService::currentActor().displayName));
    if(!q.exec()) return fail(error,q.lastError().text());
    if(!audit(db,"spare_receive",current.id,{{"quantity",qty},{"purchase_id",purchase},{"date",date.toString(Qt::ISODate)}},error) || !tx.commit(error)) return false;
    if (publishCommitted) changed(current.id);
    return true;
}
bool checkIssueTarget(QSqlDatabase db, const WorkshopSpareOrder& reserve, int orderNumber,
                      bool* differentVehicle, QString* description, QString* error) {
    if (error) error->clear();
    if (differentVehicle) *differentVehicle = false;
    if (!AccessControlService::canAccessOrder(db,orderNumber)) return fail(error,QString::fromUtf8("Наряд недоступний."));
    QSqlQuery order(db);
    order.prepare("SELECT plate,vin,closed_at,status,car,order_number FROM work_orders WHERE number=?");
    order.addBindValue(orderNumber);
    if (!order.exec()) return fail(error,order.lastError().text());
    if (!order.next() || !order.value(2).toString().trimmed().isEmpty()
        || order.value(3).toString().trimmed().compare("closed",Qt::CaseInsensitive)==0)
        return fail(error,QString::fromUtf8("Оберіть відкритий наряд."));
    const bool different = plateKey(reserve.plate).isEmpty() || plateKey(order.value(0).toString()) != plateKey(reserve.plate)
        || (!reserve.vin.trimmed().isEmpty() && !order.value(1).toString().trimmed().isEmpty()
            && order.value(1).toString().trimmed().compare(reserve.vin.trimmed(),Qt::CaseInsensitive)!=0);
    if (differentVehicle) *differentVehicle = different;
    if (description) *description = QString::fromUtf8("Наряд №%1 · %2 · %3")
        .arg(order.value(5).toString(),order.value(4).toString(),order.value(0).toString());
    return true;
}
bool issue(QSqlDatabase db,const WorkshopSpareOrder& expected,double qty,int orderNumber,QString* error,bool confirmDifferentVehicle) {
    if(error) error->clear();
    if(!ensureSchema(db,error) || !allowed(db,"inventory.purchase",error) || !allowed(db,"parts.order",error)) return false;
    if(!AccessControlService::canAccessOrder(db,orderNumber)) return fail(error,QString::fromUtf8("Наряд недоступний."));
    StockTransaction tx(db); if(!tx.begin(error) || !claim(db,expected,error)) return false;
    WorkshopSpareOrder current; for(const auto& r:orders(db,error)) if(r.id==expected.id) current=r;
    if(!current.id || !std::isfinite(qty) || qty<=0 || std::floor(qty)!=qty || qty>current.received-current.issued)
        return fail(error,QString::fromUtf8("Недостатньо отриманих запчастин у резерві."));
    bool differentVehicle = false;
    if (!checkIssueTarget(db,current,orderNumber,&differentVehicle,nullptr,error)) return false;
    if (differentVehicle && !confirmDifferentVehicle)
        return fail(error,QString::fromUtf8("Підтвердьте видачу запчастини для іншого автомобіля."));
    PartPricingInput pricing; pricing.partName=current.name; pricing.store=current.supplier; pricing.qty=qty; pricing.purchasePrice=current.unitCost;
    auto policy=PartMarkupPolicyService::loadPolicySnapshot(db);
    // A car-specific spare reserve is not a bulk warehouse material, even if names match.
    policy.inventoryClientPrices.clear();
    policy.inventoryPurchasePrices.clear();
    const auto calculated=PartPricingEngine::calculate(pricing,policy);
    // Quote and persistence validation must use the same supplier policy.
    WorkOrderPartRow part; part.name=current.name; part.partNumber=current.article; part.store=current.supplier; part.qty=qty;
    part.orderedAt=current.orderedOn.toString(Qt::ISODate); part.deliveryDate=current.deliveredOn.toString(Qt::ISODate);
    part.note=QString::fromUtf8("Резерв складу #%1; замовив %2; постачальник %3; %4").arg(current.id).arg(current.masterName,current.supplier,current.document);
    part.purchasePrice=calculated.purchasePrice; part.purchaseAmount=calculated.purchaseAmount;
    part.clientPrice=calculated.clientPrice; part.clientAmount=calculated.clientAmount; part.markupPercent=calculated.markupPercent; part.markupSource=calculated.markupSource;
    int partId=0;
    if(!WorkPartsPersistenceRepositoryFacade::insertPart(db,orderNumber,part,&partId,error)) return false;
    if ((!current.invoicePath.isEmpty() || !current.document.isEmpty())
        && !OrderAttachmentService::setPartAttachment(db, partId, current.invoicePath, current.invoiceName,
                                                     current.document, current.orderedOn.toString(Qt::ISODate), error)) return false;
    QSqlQuery q(db); q.prepare("INSERT INTO workshop_spare_movements(spare_id,operation,quantity,moved_on,part_id,order_number,actor) VALUES(?,'issue',?,?,?,?,?)");
    q.addBindValue(current.id); q.addBindValue(qty); q.addBindValue(QDate::currentDate().toString(Qt::ISODate));
    q.addBindValue(partId); q.addBindValue(orderNumber); q.addBindValue(text(ActivityAuditService::currentActor().displayName));
    if(!q.exec()) return fail(error,q.lastError().text());
    QVector<SparePaymentAllocation::Slice> slices;
    const auto paid=PurchasePaymentService::balances(db,error);
    if((error && !error->isEmpty()) || !SparePaymentAllocation::load(db,paid,&slices,error)
        || !SparePaymentAllocation::save(db,slices,error)) return false;
    if(!audit(db,"spare_issue",current.id,{{"quantity",qty},{"part_id",partId},{"order_number",orderNumber},{"different_vehicle_confirmed",differentVehicle && confirmDifferentVehicle},{"reserved_plate",current.plate}},error) || !tx.commit(error)) return false;
    changed(current.id); return true;
}
QVector<QPair<int,QString>> eligibleOrders(QSqlDatabase db,const WorkshopSpareOrder& reserve,QString* error) {
    QVector<QPair<int,QString>> result;QSqlQuery q(db);
    if(!q.exec("SELECT number,order_number,plate,car FROM work_orders WHERE TRIM(COALESCE(closed_at,''))='' AND LOWER(COALESCE(status,''))<>'closed'")) {fail(error,q.lastError().text());return result;}
    while(q.next()) if(AccessControlService::canAccessOrder(db,q.value(0).toInt()))
        result.append({q.value(0).toInt(),QString::fromUtf8("Наряд №%1 · %2 · %3").arg(q.value(1).toString(),q.value(3).toString(),q.value(2).toString())});
    return result;
}
bool cancelDraftIssue(QSqlDatabase db, qint64 spareId, int orderNumber, QString* error) {
    if (!allowed(db,"inventory.purchase",error) || !AccessControlService::canAccessOrder(db,orderNumber)) return false;
    StockTransaction tx(db); if (!tx.begin(error)) return false;
    // Claim the writer before reading the movement ids. A deferred read snapshot
    // cannot be upgraded after another connection commits a change in WAL mode.
    QSqlQuery q(db);
    q.prepare("DELETE FROM workshop_spare_movements WHERE spare_id=? AND order_number=? AND operation='issue' RETURNING part_id");
    q.addBindValue(spareId);
    q.addBindValue(orderNumber);
    if (!q.exec()) return fail(error, q.lastError().text());

    QList<int> parts;
    while (q.next()) parts.push_back(q.value(0).toInt());
    if (q.lastError().isValid()) return fail(error, q.lastError().text());
    q.finish();
    for (int part : parts) {
        if (!WorkPartsPersistenceRepositoryFacade::removePart(db,orderNumber,part,error)) return false;
    }

    q.prepare("UPDATE workshop_spare_orders SET revision=revision+1 WHERE id=?");
    q.addBindValue(spareId);
    if (!q.exec()) return fail(error, q.lastError().text());
    if (q.numRowsAffected()!=1) return fail(error, QStringLiteral("Spare order is missing during issue cancellation"));
    if (!audit(db,"spare_draft_cancel",spareId,{{"order_number",orderNumber}},error) || !tx.commit(error)) return false;
    changed(spareId);return true;
}
}

namespace WorkshopSpareStockService {
QVector<WorkshopSpareOrder> orders(QString* error) { return orders(Database::db(),error); }
QVector<WorkshopSpareReceipt> receipts(qint64 id, QString* error) { return receipts(Database::db(),id,error); }
bool addBatch(const QVector<BatchLine>& lines, const QDate& receiptDate, QString* error) { return addBatch(Database::db(),lines,receiptDate,error); }
bool receive(const WorkshopSpareOrder& expected, double quantity, const QDate& date, double paidAmount, QString* error) { return receive(Database::db(),expected,quantity,date,paidAmount,error); }
bool checkIssueTarget(const WorkshopSpareOrder& reserve, int orderNumber, bool* differentVehicle, QString* description, QString* error) { return checkIssueTarget(Database::db(),reserve,orderNumber,differentVehicle,description,error); }
bool returnReserve(const WorkshopSpareOrder& expected, const QString& oem, const QString& vehicleNode, QString* error, const QString& settlement, qint64 expectedPaidCents) { return returnReserve(Database::db(),expected,oem,vehicleNode,error,settlement,expectedPaidCents); }
QVector<QPair<int, QString>> eligibleOrders(const WorkshopSpareOrder& reserve, QString* error) { return eligibleOrders(Database::db(),reserve,error); }
}
