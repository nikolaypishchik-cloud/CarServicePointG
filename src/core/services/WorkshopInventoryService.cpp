#include "core/util/DateFormats.h"
#include "core/util/StoreNameNormalizer.h"
#include "core/services/WorkshopInventoryService.h"
#include "core/services/SupplierInvoiceLedger.h"
#include "core/services/PurchasePaymentService.h"
#include "core/cache/SnapshotCacheInvalidator.h"
#include "core/backup/BackupCommitPublisher.h"
#include "runtime/RuntimeServices.h"

#include "infrastructure/db/sqlite/Db.h"
#include "core/services/ActivityAuditService.h"
#include "core/services/AccessControlService.h"

#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QRegularExpression>
#include "core/services/orders/PartMarkupPolicyService.h"
#include <QStringList>

#include <algorithm>
#include <cmath>

namespace {

constexpr double kQuantityEpsilon = 0.000001;

void setError(QString* errorText, const QString& text)
{
    if (errorText) *errorText = text;
}

QDate parseBusinessDate(const QString& raw)
{
    const QString value = raw.trimmed();
    if (value.isEmpty()) return QDate();

    QDate date = DateFormats::parseDate(value.left(10));
    if (date.isValid()) return date;
    date = QDate::fromString(value, QStringLiteral("dd.MM.yyyy"));
    if (date.isValid()) return date;
    return QDate::fromString(value, QStringLiteral("dd.MM.yy"));
}

bool tableOrViewExists(QSqlDatabase database, const QString& name)
{
    if (!database.isValid() || !database.isOpen()) return false;
    const QStringList objects = database.tables(static_cast<QSql::TableType>(QSql::Tables | QSql::Views));
    return std::any_of(objects.cbegin(), objects.cend(), [&name](const QString& objectName) {
        return objectName.compare(name, Qt::CaseInsensitive) == 0;
    });
}

QString displayUnit(const QString& unit)
{
    const QString trimmed = unit.trimmed();
    return trimmed.isEmpty() ? QString::fromUtf8("од.") : trimmed;
}

struct LedgerState
{
    QString materialName;
    QString unit;
    double receivedQuantity = 0.0;
    double consumedQuantity = 0.0;
    double balanceQuantity = 0.0;
    double stockValue = 0.0;
    double lastUnitCost = 0.0;
};

struct LedgerEvent
{
    enum class Type { Purchase, Consumption };

    Type type = Type::Purchase;
    QDate date;
    int sourceId = 0;
    int rowIndex = -1;
};

bool dateWithin(const QDate& date, const QDate& from, const QDate& to)
{
    return date.isValid() && date >= from && date <= to;
}

} // namespace

namespace WorkshopInventoryService {

QString canonicalMaterialName(const QString& materialName)
{
    static const QRegularExpression coolant(
        QString::fromUtf8("(?<![\\p{L}\\p{N}])ант[иі]фр[иі]з[ау]?(?![\\p{L}\\p{N}])"),
        QRegularExpression::CaseInsensitiveOption);
    QString name = materialName.trimmed();
    return name.replace(coolant, QString::fromUtf8("Антифриз"));
}

QString normalizedMaterialKey(const QString& materialName)
{
    QString folded = canonicalMaterialName(materialName).toCaseFolded();
    static const QRegularExpression oilWords(QString::fromUtf8("^(?:масло|олива|моторна олива|олива моторна|масло моторное|моторное масло|масло моторне|моторне масло)(?:\\s+|$)"));
    static const QRegularExpression grade(QStringLiteral("(\\d{1,2})\\s*[wW]\\s*[- ]?\\s*(\\d{2})"));
    folded.replace(grade, QStringLiteral("\\1w\\2"));
    // Keep grades and brands distinct; only reconcile the common oil-name synonyms.
    if (oilWords.match(folded).hasMatch()) {
        folded.remove(oilWords);
        folded.remove(QRegularExpression(QString::fromUtf8("^(?:моторна|моторне|моторное)\\s*")));
        folded.prepend(QString::fromUtf8("олива моторна "));
    }
    QString result;
    result.reserve(folded.size());
    bool previousWasSeparator = true;
    for (const QChar ch : folded) {
        if (ch.isLetterOrNumber()) {
            result.append(ch);
            previousWasSeparator = false;
        } else if (!previousWasSeparator) {
            result.append(QLatin1Char(' '));
            previousWasSeparator = true;
        }
    }
    return result.trimmed();
}

bool ensureSchema(QSqlDatabase database, QString* errorText)
{
    if (!database.isValid()) {
        setError(errorText, QString::fromUtf8("Немає з'єднання з базою даних складу."));
        return false;
    }
    if (!database.isOpen() && !database.open()) {
        setError(errorText, database.lastError().text());
        return false;
    }

    QSqlQuery query(database);
    if (!query.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS workshop_inventory_purchases ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "purchased_on TEXT NOT NULL,"
            "material_name TEXT NOT NULL,"
            "material_key TEXT NOT NULL,"
            "unit TEXT NOT NULL DEFAULT 'од.',"
            "quantity REAL NOT NULL CHECK(quantity > 0),"
            "unit_cost REAL NOT NULL DEFAULT 0 CHECK(unit_cost >= 0),"
            "supplier TEXT NOT NULL DEFAULT '',"
            "note TEXT NOT NULL DEFAULT '',"
            "created_at TEXT NOT NULL DEFAULT (datetime('now'))"
            ")"))) {
        setError(errorText, query.lastError().text());
        return false;
    }
    if (database.record("workshop_inventory_purchases").indexOf("client_price") < 0
        && !query.exec("ALTER TABLE workshop_inventory_purchases ADD COLUMN client_price REAL CHECK(client_price >= 0)")) {
        setError(errorText, query.lastError().text());
        return false;
    }
    for (const auto& column : {qMakePair(QStringLiteral("stock_kind"), QStringLiteral("TEXT NOT NULL DEFAULT 'material'")),
                               qMakePair(QStringLiteral("metadata_json"), QStringLiteral("TEXT NOT NULL DEFAULT '{}'"))}) {
        if (database.record("workshop_inventory_purchases").indexOf(column.first) < 0
            && !query.exec(QStringLiteral("ALTER TABLE workshop_inventory_purchases ADD COLUMN %1 %2").arg(column.first, column.second))) {
            setError(errorText, query.lastError().text()); return false;
        }
    }
    if (!query.exec(QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_workshop_inventory_purchases_material_key "
            "ON workshop_inventory_purchases(material_key)"))
        || !query.exec(QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_workshop_inventory_purchases_date "
            "ON workshop_inventory_purchases(purchased_on)"))) {
        setError(errorText, query.lastError().text());
        return false;
    }
    return true;
}

bool addPurchase(QSqlDatabase database,
                 const WorkshopInventoryPurchaseInput& input,
                 qint64* purchaseId,
                 QString* errorText)
{
    if (!ensureSchema(database, errorText)) return false;
    const ActivityAuditActor actor = ActivityAuditService::currentActor();
    if (actor.isValid() && !AccessControlService::currentUserHasPermission(database, QStringLiteral("inventory.purchase"))) {
        setError(errorText, QString::fromUtf8("Недостатньо прав для додавання придбань матеріалів."));
        return false;
    }

    const QString materialName = canonicalMaterialName(input.materialName);
    const QString materialKey = normalizedMaterialKey(materialName);
    const QString unit = displayUnit(input.unit);
    QString supplier = input.supplier.trimmed();
    QString note = input.note.trimmed();
    if (supplier.isNull()) supplier = QStringLiteral("");
    if (note.isNull()) note = QStringLiteral("");
    if (!input.purchasedOn.isValid()) {
        setError(errorText, QString::fromUtf8("Вкажіть коректну дату придбання."));
        return false;
    }
    if (materialKey.isEmpty()) {
        setError(errorText, QString::fromUtf8("Вкажіть назву матеріалу."));
        return false;
    }
    if (!std::isfinite(input.quantity) || input.quantity <= kQuantityEpsilon) {
        setError(errorText, QString::fromUtf8("Кількість матеріалу має бути більшою за нуль."));
        return false;
    }
    if (!std::isfinite(input.unitCost) || input.unitCost < 0.0
        || !std::isfinite(input.minimumQuantity) || input.minimumQuantity < 0
        || (input.stockKind != "material" && input.stockKind != "spare")) {
        setError(errorText, QString::fromUtf8("Ціна одиниці не може бути від'ємною."));
        return false;
    }
    if (!std::isfinite(input.clientPrice) || (input.clientPrice < 0.0 && input.clientPrice != -1.0)) {
        setError(errorText, QString::fromUtf8("Ціна клієнту має бути невід'ємною."));
        return false;
    }

    QSqlQuery unitCheck(database);
    unitCheck.prepare(QStringLiteral(
        "SELECT unit, material_name FROM workshop_inventory_purchases WHERE stock_kind=? ORDER BY id"));
    unitCheck.addBindValue(input.stockKind);
    if (!unitCheck.exec()) {
        setError(errorText, unitCheck.lastError().text());
        return false;
    }
    while (unitCheck.next()) {
        if (normalizedMaterialKey(unitCheck.value(1).toString()) != materialKey) continue;
        const QString existingUnit = displayUnit(unitCheck.value(0).toString());
        if (existingUnit.compare(unit, Qt::CaseInsensitive) != 0) {
            setError(errorText, QString::fromUtf8("Матеріал уже обліковується в одиниці: %1.").arg(existingUnit));
            return false;
        }
    }

    const auto totalCents = PurchasePaymentService::cents(input.quantity * input.unitCost);
    const auto paidCents = input.paidAmount == -1.0 ? totalCents : PurchasePaymentService::cents(input.paidAmount);
    if (totalCents < 0 || paidCents < 0 || paidCents > totalCents) {
        setError(errorText, QString::fromUtf8("Некоректна сума оплати.")); return false;
    }
    if (!PurchasePaymentService::ensureSchema(database, errorText)) return false;
    QSqlQuery transaction(database);
    if (!transaction.exec("SAVEPOINT inventory_purchase_payment")) {
        setError(errorText, transaction.lastError().text()); return false;
    }
    auto rollback = [&]() { transaction.exec("ROLLBACK TO inventory_purchase_payment"); transaction.exec("RELEASE inventory_purchase_payment"); return false; };
    QSqlQuery insert(database);
    insert.prepare(QStringLiteral(
        "INSERT INTO workshop_inventory_purchases("
        "purchased_on, material_name, material_key, unit, quantity, unit_cost, supplier, note, client_price, stock_kind, metadata_json) "
        "VALUES(?,?,?,?,?,?,?,?,?,?,?)"));
    insert.addBindValue(input.purchasedOn.toString(Qt::ISODate));
    insert.addBindValue(materialName);
    insert.addBindValue(materialKey);
    insert.addBindValue(unit);
    insert.addBindValue(input.quantity);
    insert.addBindValue(input.unitCost);
    insert.addBindValue(supplier);
    insert.addBindValue(note);
    insert.addBindValue(input.clientPrice < 0.0 ? QVariant() : QVariant(input.clientPrice));
    insert.addBindValue(input.stockKind);
    const QJsonObject metadata{{"category", input.category}, {"article", input.article}, {"batch", input.batch},
        {"shelf", input.shelf}, {"document", input.document}, {"minimum", input.minimumQuantity},
        {"ordered_on", input.orderedOn.toString(Qt::ISODate)}, {"expires_on", input.expiresOn.toString(Qt::ISODate)}};
    insert.addBindValue(QString::fromUtf8(QJsonDocument(metadata).toJson(QJsonDocument::Compact)));
    if (!insert.exec()) {
        setError(errorText, insert.lastError().text());
        return rollback();
    }
    const qint64 insertedId = insert.lastInsertId().toLongLong();
    if (!PurchasePaymentService::initializeInventory(database, insertedId, paidCents, errorText, input.deferredDays)) return rollback();
    if (input.dueOn.isValid() && paidCents < totalCents) {
        QSqlQuery due(database);
        due.prepare("UPDATE purchase_payment_terms SET due_date=? WHERE payment_key=?");
        due.addBindValue(input.dueOn.toString(Qt::ISODate));
        due.addBindValue(PurchasePaymentService::key("inventory", insertedId));
        if (!due.exec()) { setError(errorText, due.lastError().text()); return rollback(); }
    }
    QJsonObject purchasedMaterial;
    purchasedMaterial.insert(QStringLiteral("name"), materialName);
    purchasedMaterial.insert(QStringLiteral("purchased_on"), input.purchasedOn.toString(Qt::ISODate));
    purchasedMaterial.insert(QStringLiteral("unit"), unit);
    purchasedMaterial.insert(QStringLiteral("quantity"), input.quantity);
    purchasedMaterial.insert(QStringLiteral("unit_cost"), input.unitCost);
    if (input.clientPrice >= 0.0) purchasedMaterial.insert(QStringLiteral("client_price"), input.clientPrice);
    purchasedMaterial.insert(QStringLiteral("total_cost"), input.quantity * input.unitCost);
    purchasedMaterial.insert(QStringLiteral("supplier"), supplier);
    purchasedMaterial.insert(QStringLiteral("note"), note);
    QJsonObject auditPayload;
    auditPayload.insert(QStringLiteral("before"), QJsonObject{});
    auditPayload.insert(QStringLiteral("after"), purchasedMaterial);

    QString auditError;
    if (!ActivityAuditService::record(database,
                                 QStringLiteral("inventory"),
                                 QStringLiteral("purchase_add"),
                                 QString::fromUtf8("Придбано матеріал: %1").arg(materialName),
                                 0,
                                 QStringLiteral("workshop_inventory_purchase"),
                                 QString::number(insertedId),
                                 materialName,
                                 QString::fromUtf8(QJsonDocument(auditPayload).toJson(QJsonDocument::Compact)),
                                 &auditError)) { setError(errorText, auditError); return rollback(); }
    if (!transaction.exec("RELEASE inventory_purchase_payment")) {
        setError(errorText, transaction.lastError().text()); return rollback();
    }
    if (purchaseId) *purchaseId = insertedId;
    if (input.publishCommitted) SnapshotCacheInvalidator::invalidateReportsAndStats("inventory_purchase_payment");
    if (input.publishCommitted && RuntimeServices::backupManager()) {
        BackupCommitPublisher::publish(BackupCommitPublisher::makeEvent(
            "inventory", "purchases", "workshop_inventory_purchase", QString::number(insertedId),
            {}, 0, "purchase_add", materialName,
            {"workshop_inventory_purchases", "purchase_payment_balances", "purchase_payment_events", "purchase_payment_terms", "activity_audit_events"}));
    }
    return true;
}

bool setClientPrice(QSqlDatabase database, qint64 purchaseId, double price, QString* errorText)
{
    if (!ensureSchema(database,errorText)) return false;
    if (ActivityAuditService::currentActor().isValid()
        && !AccessControlService::currentUserHasPermission(database,QStringLiteral("inventory.purchase"))) {
        setError(errorText,QString::fromUtf8("Недостатньо прав для зміни ціни матеріалу.")); return false;
    }
    if (!std::isfinite(price) || price<0.0 || price>100000000.0) {
        setError(errorText,QString::fromUtf8("Некоректна ціна клієнту.")); return false;
    }
    QSqlQuery tx(database);
    if (!tx.exec("SAVEPOINT inventory_client_price")) { setError(errorText,tx.lastError().text()); return false; }
    auto rollback=[&] { tx.exec("ROLLBACK TO inventory_client_price"); tx.exec("RELEASE inventory_client_price"); return false; };
    QSqlQuery query(database);
    query.prepare("SELECT material_name,client_price FROM workshop_inventory_purchases WHERE id=?");
    query.addBindValue(purchaseId);
    if (!query.exec() || !query.next()) {
        setError(errorText,QString::fromUtf8("Не знайдено придбання матеріалу.")); return rollback();
    }
    const QString name=query.value(0).toString();
    const QJsonValue before=QJsonValue::fromVariant(query.value(1));
    query.finish();
    query.prepare("UPDATE workshop_inventory_purchases SET client_price=? WHERE id=?");
    query.addBindValue(price); query.addBindValue(purchaseId);
    if (!query.exec()) { setError(errorText,query.lastError().text()); return rollback(); }
    const QJsonObject payload{{"before",QJsonObject{{"client_price",before}}},{"after",QJsonObject{{"client_price",price}}}};
    if (!ActivityAuditService::record(database,"inventory","inventory_client_price",
            QString::fromUtf8("Змінено ціну клієнту: %1").arg(name),0,"workshop_inventory_purchase",QString::number(purchaseId),name,
            QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact)),errorText)) return rollback();
    if (!tx.exec("RELEASE inventory_client_price")) { setError(errorText,tx.lastError().text()); return rollback(); }
    SnapshotCacheInvalidator::invalidateReportsAndStats("inventory_client_price");
    if (RuntimeServices::backupManager()) {
        BackupCommitPublisher::publish(BackupCommitPublisher::makeEvent(
            "inventory","purchases","workshop_inventory_purchase",QString::number(purchaseId),{},0,"inventory_client_price",name,
            {"workshop_inventory_purchases","activity_audit_events"}));
    }
    return true;
}

bool removePurchase(QSqlDatabase database, qint64 purchaseId, QString* errorText)
{
    if (database.record("workshop_inventory_purchases").indexOf("stock_kind") >= 0) {
        QSqlQuery reserved(database);
        reserved.prepare("SELECT stock_kind FROM workshop_inventory_purchases WHERE id=?");reserved.addBindValue(purchaseId);
        if(!reserved.exec()) {setError(errorText,reserved.lastError().text());return false;}
        if(reserved.next() && reserved.value(0).toString()=="spare") {
            setError(errorText,QString::fromUtf8("Поставка пов'язана з резервом запчастин. Видалення складської закупівлі заборонено."));return false;
        }
    }
    if (!ensureSchema(database, errorText)) return false;
    const ActivityAuditActor actor = ActivityAuditService::currentActor();
    if (actor.isValid() && !AccessControlService::currentUserHasPermission(database, QStringLiteral("inventory.delete"))) {
        setError(errorText, QString::fromUtf8("Недостатньо прав для видалення придбань матеріалів."));
        return false;
    }
    if (purchaseId <= 0) {
        setError(errorText, QString::fromUtf8("Оберіть придбання для видалення."));
        return false;
    }

    QString materialName;
    QJsonObject removedMaterial;
    QSqlQuery lookup(database);
    lookup.prepare(QStringLiteral(
        "SELECT purchased_on, material_name, unit, quantity, unit_cost, supplier, note "
        "FROM workshop_inventory_purchases WHERE id=?"));
    lookup.addBindValue(purchaseId);
    if (lookup.exec() && lookup.next()) {
        materialName = lookup.value(1).toString().trimmed();
        removedMaterial.insert(QStringLiteral("purchased_on"), lookup.value(0).toString());
        removedMaterial.insert(QStringLiteral("name"), materialName);
        removedMaterial.insert(QStringLiteral("unit"), lookup.value(2).toString());
        removedMaterial.insert(QStringLiteral("quantity"), lookup.value(3).toDouble());
        removedMaterial.insert(QStringLiteral("unit_cost"), lookup.value(4).toDouble());
        removedMaterial.insert(QStringLiteral("supplier"), lookup.value(5).toString());
        removedMaterial.insert(QStringLiteral("note"), lookup.value(6).toString());
    }

    QSqlQuery query(database);
    query.prepare(QStringLiteral("DELETE FROM workshop_inventory_purchases WHERE id=?"));
    query.addBindValue(purchaseId);
    if (!query.exec()) {
        setError(errorText, query.lastError().text());
        return false;
    }
    if (query.numRowsAffected() == 0) {
        setError(errorText, QString::fromUtf8("Запис придбання вже не існує."));
        return false;
    }
    QJsonObject auditPayload;
    auditPayload.insert(QStringLiteral("before"), removedMaterial);
    auditPayload.insert(QStringLiteral("after"), QJsonObject{});
    QString auditError;
    ActivityAuditService::record(database,
                                 QStringLiteral("inventory"),
                                 QStringLiteral("purchase_remove"),
                                 QString::fromUtf8("Видалено придбання матеріалу: %1").arg(materialName),
                                 0,
                                 QStringLiteral("workshop_inventory_purchase"),
                                 QString::number(purchaseId),
                                 materialName,
                                 QString::fromUtf8(QJsonDocument(auditPayload).toJson(QJsonDocument::Compact)),
                                 &auditError);
    return true;
}

QVector<WorkshopInventoryMaterial> loadMaterials(QSqlDatabase database, QString* errorText, bool initializeSchema)
{
    QVector<WorkshopInventoryMaterial> materials;
    if (initializeSchema && !ensureSchema(database, errorText)) return materials;

    QSqlQuery query(database);
    if (!query.exec(QStringLiteral(
            "SELECT material_key, material_name, unit "
            "FROM workshop_inventory_purchases WHERE stock_kind='material' "
            "ORDER BY date(CASE WHEN substr(trim(purchased_on),3,1)='.' AND substr(trim(purchased_on),6,1)='.' THEN CASE WHEN substr(trim(purchased_on),7,4) GLOB '[0-9][0-9][0-9][0-9]' THEN substr(trim(purchased_on),7,4) ELSE '20'||substr(trim(purchased_on),7,2) END||'-'||substr(trim(purchased_on),4,2)||'-'||substr(trim(purchased_on),1,2) ELSE purchased_on END), id"))) {
        setError(errorText, query.lastError().text());
        return materials;
    }

    QSet<QString> seen;
    while (query.next()) {
        const QString key = normalizedMaterialKey(query.value(1).toString());
        if (key.isEmpty() || seen.contains(key)) continue;
        seen.insert(key);
        materials.append({key, canonicalMaterialName(query.value(1).toString()), displayUnit(query.value(2).toString())});
    }
    if(query.lastError().isValid()){setError(errorText,query.lastError().text());return {};}
    std::sort(materials.begin(), materials.end(), [](const WorkshopInventoryMaterial& left, const WorkshopInventoryMaterial& right) {
        return QString::compare(left.name, right.name, Qt::CaseInsensitive) < 0;
    });
    return materials;
}
WorkshopInventorySnapshot buildSnapshot(QSqlDatabase database, const QDate& from, const QDate& to, bool initializeSchema)
{
    WorkshopInventorySnapshot snapshot;
    if (!from.isValid() || !to.isValid() || from > to) {
        snapshot.errorText = QString::fromUtf8("Некоректний період складу.");
        return snapshot;
    }
    if (initializeSchema && !ensureSchema(database, &snapshot.errorText)) return snapshot;

    QVector<WorkshopInventoryPurchaseRow> purchasesUntilEnd;
    QHash<QString, LedgerState> states;
    QSqlQuery purchaseQuery(database);
    purchaseQuery.prepare(QStringLiteral(
        "SELECT id, purchased_on, material_name, material_key, unit, quantity, unit_cost, supplier, note, client_price, metadata_json, stock_kind "
        "FROM workshop_inventory_purchases "
        "WHERE date(CASE WHEN substr(trim(purchased_on),3,1)='.' AND substr(trim(purchased_on),6,1)='.' THEN CASE WHEN substr(trim(purchased_on),7,4) GLOB '[0-9][0-9][0-9][0-9]' THEN substr(trim(purchased_on),7,4) ELSE '20'||substr(trim(purchased_on),7,2) END||'-'||substr(trim(purchased_on),4,2)||'-'||substr(trim(purchased_on),1,2) ELSE purchased_on END) <= date(?) "
        "ORDER BY date(CASE WHEN substr(trim(purchased_on),3,1)='.' AND substr(trim(purchased_on),6,1)='.' THEN CASE WHEN substr(trim(purchased_on),7,4) GLOB '[0-9][0-9][0-9][0-9]' THEN substr(trim(purchased_on),7,4) ELSE '20'||substr(trim(purchased_on),7,2) END||'-'||substr(trim(purchased_on),4,2)||'-'||substr(trim(purchased_on),1,2) ELSE purchased_on END), id"));
    purchaseQuery.addBindValue(to.toString(Qt::ISODate));
    if (!purchaseQuery.exec()) {
        snapshot.errorText = purchaseQuery.lastError().text();
        return snapshot;
    }
    const auto payments = PurchasePaymentService::balances(database, &snapshot.errorText);
    if(!snapshot.errorText.isEmpty())return snapshot;
    const auto remoteLinks=SupplierInvoiceLedger::activeLinks(database);
    while (purchaseQuery.next()) {
        if (purchaseQuery.value(11).toString() != "material") continue;
        WorkshopInventoryPurchaseRow row;
        row.id = purchaseQuery.value(0).toLongLong();
        row.purchasedOn = parseBusinessDate(purchaseQuery.value(1).toString());
        row.materialName = canonicalMaterialName(purchaseQuery.value(2).toString());
        row.materialKey = normalizedMaterialKey(row.materialName);
        row.unit = displayUnit(purchaseQuery.value(4).toString());
        row.quantity = purchaseQuery.value(5).toDouble();
        row.unitCost = purchaseQuery.value(6).toDouble();
        row.supplier = StoreNameNormalizer::canonicalDisplayName(purchaseQuery.value(7).toString());
        row.note = purchaseQuery.value(8).toString().trimmed();
        row.clientPrice = purchaseQuery.value(9).isNull() ? -1.0 : purchaseQuery.value(9).toDouble();
        const auto metadata = QJsonDocument::fromJson(purchaseQuery.value(10).toString().toUtf8()).object();
        row.category = metadata.value("category").toString(); row.article = metadata.value("article").toString();
        row.batch = metadata.value("batch").toString(); row.shelf = metadata.value("shelf").toString();
        row.document = metadata.value("document").toString(); row.minimumQuantity = metadata.value("minimum").toDouble();
        if(row.document.isEmpty()) row.document=remoteLinks.value(PurchasePaymentService::key("inventory",row.id)).number;
        row.paidAmount = payments.value(PurchasePaymentService::key("inventory", row.id),
            PurchasePaymentService::cents(row.quantity * row.unitCost)) / 100.0;
        if (!row.purchasedOn.isValid() || row.materialKey.isEmpty() || row.quantity <= kQuantityEpsilon) continue;

        auto stateIt = states.find(row.materialKey);
        if (stateIt == states.end()) {
            LedgerState state;
            state.materialName = row.materialName;
            state.unit = row.unit;
            states.insert(row.materialKey, state);
        }
        purchasesUntilEnd.append(row);
    }

    if(purchaseQuery.lastError().isValid()){snapshot={};snapshot.errorText=purchaseQuery.lastError().text();return snapshot;}
    const QString partsSource = tableOrViewExists(database, QStringLiteral("router_work_order_parts"))
        ? QStringLiteral("router_work_order_parts") : QStringLiteral("work_order_parts");
    const QString ordersSource = tableOrViewExists(database, QStringLiteral("router_work_orders"))
        ? QStringLiteral("router_work_orders") : QStringLiteral("work_orders");
    if (!tableOrViewExists(database, partsSource) || !tableOrViewExists(database, ordersSource)) {
        snapshot.errorText = QString::fromUtf8("Не знайдено дані нарядів для розрахунку витрат матеріалів.");
        return snapshot;
    }

    QVector<WorkshopInventoryConsumptionRow> allConsumptions;
    const bool hasStore = database.record(partsSource).indexOf("store") >= 0;
    QSqlQuery consumptionQuery(database);
    QString consumptionSql = QStringLiteral(
        "SELECT p.id, COALESCE(p.order_number,0), COALESCE(o.order_number,o.number,p.order_number), "
        "COALESCE(p.name,''), COALESCE(p.qty,1), COALESCE(p.installed_at,''), COALESCE(o.closed_at,''), "
        "COALESCE(o.car,''), COALESCE(o.client,''), %3 "
        "FROM %1 p LEFT JOIN %2 o ON o.number=p.order_number "
        "WHERE TRIM(COALESCE(p.name,''))<>''")
        .arg(partsSource, ordersSource, hasStore ? QStringLiteral("COALESCE(p.store,'')") : QStringLiteral("''"));
    if (tableOrViewExists(database, QStringLiteral("workshop_spare_movements")))
        consumptionSql += QStringLiteral(" AND NOT EXISTS (SELECT 1 FROM workshop_spare_movements sm WHERE sm.operation='issue' AND sm.part_id=p.id)");
    if (!consumptionQuery.exec(consumptionSql)) {
        snapshot.errorText = consumptionQuery.lastError().text();
        return snapshot;
    }
    while (consumptionQuery.next()) {
        if (hasStore && !PartMarkupPolicyService::isInventoryStore(consumptionQuery.value(9).toString())) continue;
        const QString materialKey = normalizedMaterialKey(consumptionQuery.value(3).toString());
        const auto stateIt = states.constFind(materialKey);
        if (materialKey.isEmpty() || stateIt == states.cend()) continue;

        const QDate installedDate = parseBusinessDate(consumptionQuery.value(5).toString());
        const QDate closedDate = parseBusinessDate(consumptionQuery.value(6).toString());
        const QDate consumedOn = installedDate.isValid() ? installedDate : closedDate;
        if (!consumedOn.isValid() || consumedOn > to) continue;

        WorkshopInventoryConsumptionRow row;
        row.partId = consumptionQuery.value(0).toLongLong();
        row.consumedOn = consumedOn;
        row.orderNumber = consumptionQuery.value(2).toInt();
        if (row.orderNumber <= 0) row.orderNumber = consumptionQuery.value(1).toInt();
        row.car = consumptionQuery.value(7).toString().trimmed();
        row.client = consumptionQuery.value(8).toString().trimmed();
        row.materialKey = materialKey;
        row.materialName = stateIt->materialName;
        row.unit = stateIt->unit;
        row.quantity = consumptionQuery.value(4).toDouble();
        if (row.quantity <= kQuantityEpsilon) continue;
        allConsumptions.append(row);
    }

    QVector<LedgerEvent> events;
    events.reserve(purchasesUntilEnd.size() + allConsumptions.size());
    for (int index = 0; index < purchasesUntilEnd.size(); ++index) {
        events.append({LedgerEvent::Type::Purchase, purchasesUntilEnd.at(index).purchasedOn, int(purchasesUntilEnd.at(index).id), index});
    }
    for (int index = 0; index < allConsumptions.size(); ++index) {
        events.append({LedgerEvent::Type::Consumption, allConsumptions.at(index).consumedOn, int(allConsumptions.at(index).partId), index});
    }
    std::sort(events.begin(), events.end(), [](const LedgerEvent& left, const LedgerEvent& right) {
        if (left.date != right.date) return left.date < right.date;
        if (left.type != right.type) return left.type == LedgerEvent::Type::Purchase;
        return left.sourceId < right.sourceId;
    });

    QSet<QString> openingAdded;
    for (const LedgerEvent& event : events) {
        const QString key = event.type == LedgerEvent::Type::Purchase
            ? purchasesUntilEnd.at(event.rowIndex).materialKey : allConsumptions.at(event.rowIndex).materialKey;
        auto stateIt = states.find(key);
        if (stateIt == states.end()) continue;
        LedgerState& state = stateIt.value();

        if (event.date >= from && !openingAdded.contains(key)) {
            openingAdded.insert(key);
            if (std::abs(state.balanceQuantity) > kQuantityEpsilon) {
                snapshot.periodRows.append({from, QString::fromUtf8("Вхідний залишок"), 0,
                                            state.materialName, state.unit, 0.0, 0.0,
                                            state.balanceQuantity, 0.0, state.stockValue, 0, state.stockValue});
            }
        }

        if (event.type == LedgerEvent::Type::Purchase) {
            const WorkshopInventoryPurchaseRow& purchase = purchasesUntilEnd.at(event.rowIndex);
            state.receivedQuantity += purchase.quantity;
            state.balanceQuantity += purchase.quantity;
            state.stockValue += purchase.quantity * purchase.unitCost;
            state.lastUnitCost = purchase.unitCost;
            if (dateWithin(event.date, from, to)) {
                snapshot.purchases.append(purchase);
                snapshot.periodRows.append({event.date, QString::fromUtf8("Придбання"), 0,
                                            state.materialName, state.unit, purchase.quantity, 0.0,
                                            state.balanceQuantity, purchase.unitCost,
                                            purchase.quantity * purchase.unitCost, purchase.id, state.stockValue});
            }
        } else {
            WorkshopInventoryConsumptionRow& consumption = allConsumptions[event.rowIndex];
            const double unitCost = state.balanceQuantity > kQuantityEpsilon
                ? state.stockValue / state.balanceQuantity : state.lastUnitCost;
            consumption.unitCost = qMax(0.0, unitCost);
            consumption.totalCost = consumption.quantity * consumption.unitCost;
            state.consumedQuantity += consumption.quantity;
            state.balanceQuantity -= consumption.quantity;
            state.stockValue -= consumption.totalCost;
            if (std::abs(state.stockValue) < kQuantityEpsilon) state.stockValue = 0.0;
            if (dateWithin(event.date, from, to)) {
                snapshot.consumptions.append(consumption);
                snapshot.periodRows.append({event.date,
                                            consumption.orderNumber > 0
                                                ? QString::fromUtf8("Витрата за нарядом")
                                                : QString::fromUtf8("Витрата"),
                                            consumption.orderNumber, state.materialName, state.unit,
                                            0.0, consumption.quantity, state.balanceQuantity,
                                            consumption.unitCost, consumption.totalCost, 0, state.stockValue});
            }
        }
    }

    for (auto stateIt = states.cbegin(); stateIt != states.cend(); ++stateIt) {
        const LedgerState& state = stateIt.value();
        WorkshopInventoryBalanceRow row;
        row.materialKey = stateIt.key();
        row.materialName = state.materialName;
        row.unit = state.unit;
        row.receivedQuantity = state.receivedQuantity;
        row.consumedQuantity = state.consumedQuantity;
        row.balanceQuantity = state.balanceQuantity;
        row.stockValue = state.stockValue;
        for (const auto& purchase : purchasesUntilEnd) if (purchase.materialKey == row.materialKey) {
            row.minimumQuantity = purchase.minimumQuantity; row.shelf = purchase.shelf;
            if (purchase.purchasedOn > row.lastMovement) row.lastMovement = purchase.purchasedOn;
        }
        for (const auto& consumption : allConsumptions)
            if (consumption.materialKey == row.materialKey && consumption.consumedOn > row.lastMovement)
                row.lastMovement = consumption.consumedOn;
        row.averageCost = state.balanceQuantity > kQuantityEpsilon
            ? state.stockValue / state.balanceQuantity : state.lastUnitCost;
        snapshot.balanceRows.append(row);
    }

    std::sort(snapshot.purchases.begin(), snapshot.purchases.end(), [](const WorkshopInventoryPurchaseRow& left, const WorkshopInventoryPurchaseRow& right) {
        return left.purchasedOn != right.purchasedOn ? left.purchasedOn > right.purchasedOn : left.id > right.id;
    });
    std::sort(snapshot.consumptions.begin(), snapshot.consumptions.end(), [](const WorkshopInventoryConsumptionRow& left, const WorkshopInventoryConsumptionRow& right) {
        if (left.consumedOn != right.consumedOn) return left.consumedOn > right.consumedOn;
        return left.orderNumber != right.orderNumber ? left.orderNumber > right.orderNumber : left.partId > right.partId;
    });
    std::reverse(snapshot.periodRows.begin(), snapshot.periodRows.end());
    std::stable_sort(snapshot.periodRows.begin(), snapshot.periodRows.end(), [](const WorkshopInventoryPeriodRow& left, const WorkshopInventoryPeriodRow& right) {
        if (left.date != right.date) return left.date > right.date;
        return QString::compare(left.materialName, right.materialName, Qt::CaseInsensitive) < 0;
    });
    std::sort(snapshot.balanceRows.begin(), snapshot.balanceRows.end(), [](const WorkshopInventoryBalanceRow& left, const WorkshopInventoryBalanceRow& right) {
        return QString::compare(left.materialName, right.materialName, Qt::CaseInsensitive) < 0;
    });
    if(consumptionQuery.lastError().isValid()){snapshot={};snapshot.errorText=consumptionQuery.lastError().text();}
    return snapshot;
}
bool addPurchase(const WorkshopInventoryPurchaseInput& input, qint64* purchaseId, QString* errorText)
{
    QString databaseError;
    QSqlDatabase database = Database::db(&databaseError);
    if (!databaseError.isEmpty()) {
        setError(errorText, databaseError);
        return false;
    }
    return addPurchase(database, input, purchaseId, errorText);
}

bool removePurchase(qint64 purchaseId, QString* errorText)
{
    QString databaseError;
    QSqlDatabase database = Database::db(&databaseError);
    if (!databaseError.isEmpty()) {
        setError(errorText, databaseError);
        return false;
    }
    return removePurchase(database, purchaseId, errorText);
}

QVector<WorkshopInventoryMaterial> loadMaterials(QString* errorText)
{
    QString databaseError;
    QSqlDatabase database = Database::db(&databaseError);
    if (!databaseError.isEmpty()) {
        setError(errorText, databaseError);
        return {};
    }
    return loadMaterials(database, errorText);
}

WorkshopInventorySnapshot buildSnapshot(const QDate& from, const QDate& to)
{
    QString databaseError;
    QSqlDatabase database = Database::db(&databaseError);
    if (!databaseError.isEmpty()) {
        WorkshopInventorySnapshot snapshot;
        snapshot.errorText = databaseError;
        return snapshot;
    }
    return buildSnapshot(database, from, to);
}

} // namespace WorkshopInventoryService

namespace WorkshopInventoryService {
bool setClientPrice(qint64 purchaseId, double price, QString* errorText) { return setClientPrice(Database::db(),purchaseId,price,errorText); }
}
