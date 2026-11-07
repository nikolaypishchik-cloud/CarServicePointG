#include "core/services/WorkshopInventoryService.h"
#include "core/services/InventoryPriceInput.h"
#include "core/services/WorkshopSpareStockService.h"
#include "core/services/orders/PartPricingEngine.h"
#include "core/services/PurchasePaymentService.h"
#include "core/import/InvoiceCatalogIdentity.h"
#include "core/services/InventoryStockSummary.h"
#include <QJsonDocument>

#include <QCoreApplication>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

#include <cmath>
#include <iostream>

namespace {

bool execute(QSqlDatabase database, const QString& sql)
{
    QSqlQuery query(database);
    if (query.exec(sql)) return true;
    std::cerr << query.lastError().text().toStdString() << '\n';
    return false;
}

bool require(bool condition, const char* message)
{
    if (condition) return true;
    std::cerr << message << '\n';
    return false;
}

bool approximatelyEqual(double left, double right)
{
    return std::abs(left - right) < 0.0001;
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    const QString connectionName = QStringLiteral("workshop_inventory_regression");
    QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
    database.setDatabaseName(QStringLiteral(":memory:"));
    if (!database.open()) {
        std::cerr << database.lastError().text().toStdString() << '\n';
        return 1;
    }

    QString errorText;
    if (!require(WorkshopInventoryService::ensureSchema(database, &errorText), "inventory schema was not created")) return 1;
    if (!execute(database, QStringLiteral("CREATE TABLE work_orders(number INTEGER PRIMARY KEY, order_number INTEGER, closed_at TEXT, car TEXT, client TEXT)"))
        || !execute(database, QStringLiteral("CREATE TABLE work_order_parts(id INTEGER PRIMARY KEY, order_number INTEGER, name TEXT, qty REAL, installed_at TEXT)"))
        || !execute(database, QStringLiteral("CREATE VIEW router_work_orders AS SELECT number, order_number, closed_at, car, client FROM work_orders"))
        || !execute(database, QStringLiteral("CREATE VIEW router_work_order_parts AS SELECT id, order_number, name, qty, installed_at FROM work_order_parts"))) return 1;

    WorkshopInventoryPurchaseInput firstPurchase;
    firstPurchase.purchasedOn = QDate(2026, 1, 2);
    firstPurchase.materialName = QString::fromUtf8("Олива моторна");
    firstPurchase.unit = QString::fromUtf8("л");
    firstPurchase.quantity = 10.0;
    firstPurchase.unitCost = 200.0;
    if (!WorkshopInventoryService::addPurchase(database, firstPurchase, nullptr, &errorText)) {
        std::cerr << "first purchase was not saved: " << errorText.toStdString() << '\n';
        return 1;
    }

    WorkshopInventoryPurchaseInput secondPurchase = firstPurchase;
    secondPurchase.purchasedOn = QDate(2026, 1, 10);
    secondPurchase.materialName = QString::fromUtf8("  олива   МОТОРНА ");
    secondPurchase.quantity = 5.0;
    secondPurchase.unitCost = 300.0;
    secondPurchase.clientPrice = 450.25;
    qint64 purchaseId=0;
    if (!require(WorkshopInventoryService::addPurchase(database, secondPurchase, &purchaseId, &errorText), "case-insensitive material purchase was not saved")) return 1;

    PartPricingInput pricing;
    pricing.partName=firstPurchase.materialName;
    pricing.store=QString::fromUtf8("База"); pricing.purchasePrice=999; pricing.qty=2;
    auto result=PartPricingEngine::calculate(pricing,PartMarkupPolicyService::loadPolicySnapshot(database));
    if (!require(approximatelyEqual(result.clientPrice,450.25),"inventory client price not used")
        || !require(approximatelyEqual(result.clientAmount,900.50),"inventory client total incorrect")
        || !require(approximatelyEqual(result.purchaseAmount,600),"client price changed purchase cost")) return 1;
    pricing.sourceType="report"; pricing.purchasePrice=200;
    if (!require(approximatelyEqual(PartPricingEngine::calculate(pricing,PartMarkupPolicyService::loadPolicySnapshot(database)).purchasePrice,200),"historical purchase price was overwritten")) return 1;
    pricing.sourceType="manual_inline"; pricing.purchasePrice=300;
    pricing.store="External shop";
    if (!require(approximatelyEqual(PartPricingEngine::calculate(pricing,PartMarkupPolicyService::loadPolicySnapshot(database)).clientPrice,300),"inventory price leaked to another store")) return 1;
    pricing.store=QString::fromUtf8("Склад СТО"); pricing.manualMarkupOverride=true; pricing.manualMarkupPercent=10;
    if (!require(approximatelyEqual(PartPricingEngine::calculate(pricing,PartMarkupPolicyService::loadPolicySnapshot(database)).clientPrice,450.25),"warehouse sale price was overridden by order markup")) return 1;
    pricing.manualMarkupOverride=false;
    if (!require(WorkshopInventoryService::setClientPrice(database,purchaseId,0,&errorText),"zero client price rejected")
        || !require(PartPricingEngine::calculate(pricing,PartMarkupPolicyService::loadPolicySnapshot(database)).clientPrice==0,"zero client price ignored")
        || !require(!WorkshopInventoryService::setClientPrice(database,purchaseId,-2,&errorText),"negative client price accepted")
        || !require(WorkshopInventoryService::setClientPrice(database,purchaseId,450.25,&errorText),"client price update failed")) return 1;

    WorkshopInventoryPurchaseInput invalidUnit = firstPurchase;
    invalidUnit.purchasedOn = QDate(2026, 1, 12);
    invalidUnit.unit = QString::fromUtf8("шт.");
    if (!require(!WorkshopInventoryService::addPurchase(database, invalidUnit, nullptr, &errorText), "mismatched units must be rejected")) return 1;

    if (!execute(database, QStringLiteral("INSERT INTO work_orders VALUES(11,11,'2026-01-05','Mazda 6','Іван')"))
        || !execute(database, QStringLiteral("INSERT INTO work_orders VALUES(12,12,'2026-01-08','Toyota Camry','Олег')"))
        || !execute(database, QStringLiteral("INSERT INTO work_order_parts VALUES(1,11,'олива моторна',2,'2026-01-03')"))
        || !execute(database, QStringLiteral("INSERT INTO work_order_parts VALUES(2,12,'Олива моторна',1,'')"))
        || !execute(database, QStringLiteral("INSERT INTO work_order_parts VALUES(3,12,'Фільтр оливи',1,'2026-01-08')"))) return 1;

    const WorkshopInventorySnapshot snapshot = WorkshopInventoryService::buildSnapshot(database, QDate(2026, 1, 1), QDate(2026, 1, 31));
    if (!require(snapshot.errorText.isEmpty(), "snapshot returned an error")
        || !require(snapshot.purchases.size() == 2, "period must include two purchases")
        || !require(approximatelyEqual(snapshot.purchases[0].clientPrice,450.25), "client price not persisted")
        || !require(snapshot.purchases[1].clientPrice==-1.0, "legacy unset client price changed")
        || !require(snapshot.consumptions.size() == 2, "only registered material consumptions must be included")
        || !require(snapshot.balanceRows.size() == 1, "balance must contain one registered material")
        || !require(approximatelyEqual(snapshot.balanceRows.constFirst().receivedQuantity, 15.0), "received quantity is incorrect")
        || !require(approximatelyEqual(snapshot.balanceRows.constFirst().consumedQuantity, 3.0), "consumed quantity is incorrect")
        || !require(approximatelyEqual(snapshot.balanceRows.constFirst().balanceQuantity, 12.0), "closing balance is incorrect")) return 1;

    if (!execute(database,"ALTER TABLE work_order_parts ADD COLUMN order_uid TEXT")
        || !execute(database,"CREATE TABLE masters(id TEXT PRIMARY KEY,name TEXT,active INTEGER,role TEXT)")
        || !execute(database,"INSERT INTO masters VALUES('m1','Test master',1,'master')")) return 1;
    WorkshopSpareOrder spare;
    spare.name=firstPurchase.materialName; spare.brand="Audi";spare.model="Q7";spare.plate="AA1234AA";spare.masterId="m1";
    spare.supplier="Shop";spare.quantity=2;spare.unitCost=200.25;spare.orderedOn=QDate::currentDate().addDays(-5);spare.dueOn=QDate::currentDate().addDays(9);
    qint64 spareId=0;
    if(!require(WorkshopSpareStockService::addOrder(database,spare,&spareId,&errorText),qPrintable(errorText))) return 1;
    auto orders=WorkshopSpareStockService::orders(database,&errorText);
    if(!require(orders.size()==1 && orders[0].received==0 && orders[0].debtCents==0,"unreceived order counted as stock/debt")) return 1;
    auto stale=orders[0];
    if(!require(WorkshopSpareStockService::receive(database,orders[0],1,QDate::currentDate(),50.25,&errorText),qPrintable(errorText))
        || !require(!WorkshopSpareStockService::receive(database,stale,1,QDate::currentDate(),0,&errorText),"duplicate receipt was accepted")) return 1;
    orders=WorkshopSpareStockService::orders(database,&errorText);
    if(!require(orders[0].received==1 && orders[0].debtCents==15000,"partial receipt payment wrong")
        || !require(!WorkshopSpareStockService::receive(database,orders[0],2,QDate::currentDate(),0,&errorText),"excess receipt accepted")) return 1;
    const auto supplies=WorkshopSpareStockService::receipts(database,spareId,&errorText);
    const auto debt=PurchasePaymentService::detail(database,"inventory",supplies[0].purchaseId,{},&errorText);
    if(!require(debt.dueDate==spare.dueOn,"absolute payment deadline lost")
        || !require(WorkshopInventoryService::loadMaterials(database,&errorText).size()==1,"spare leaked into material choices")) return 1;
    const auto separated=WorkshopInventoryService::buildSnapshot(database,QDate(2026,1,1),QDate::currentDate());
    if(!require(separated.purchases.size()==2,"spare leaked into material ledger")) return 1;
    // An audit failure must roll back the receipt, purchase, payment and optimistic revision.
    if(!execute(database,"CREATE TRIGGER refuse_spare_receipt BEFORE INSERT ON workshop_spare_movements BEGIN SELECT RAISE(ABORT,'test failure'); END")) return 1;
    if(!require(!WorkshopSpareStockService::receive(database,orders[0],1,QDate::currentDate(),0,&errorText),"failed receipt committed")) return 1;
    const auto rolledBack=WorkshopSpareStockService::orders(database,&errorText);
    if(!require(rolledBack[0].received==1 && rolledBack[0].revision==orders[0].revision,"failed receipt changed quantity/revision")) return 1;
    if(!execute(database,"DROP TRIGGER refuse_spare_receipt")) return 1;
    if(!require(WorkshopSpareStockService::receive(database,rolledBack[0],1,QDate::currentDate(),0,&errorText),qPrintable(errorText))) return 1;
    if(!require(!WorkshopSpareStockService::returnReserve(database,WorkshopSpareStockService::orders(database)[0],"OEM","Brakes",&errorText),"received reserve deleted"))return 1;
    if(!execute(database,"CREATE TABLE parts_catalog(id INTEGER PRIMARY KEY,vehicle_brand TEXT,vehicle_model TEXT,year_from INTEGER,year_to INTEGER,fuel_type TEXT,group_name TEXT,part_number TEXT,oem_number TEXT,name TEXT,brand TEXT,supplier TEXT,price REAL,note TEXT,currency TEXT,unit TEXT,source_type TEXT,first_imported_at TEXT,last_imported_at TEXT,manually_edited INTEGER,is_active INTEGER)"))return 1;
    if(!require(WorkshopSpareStockService::addOrder(database,spare,&spareId,&errorText),qPrintable(errorText)))return 1;
    const auto returning=WorkshopSpareStockService::orders(database)[0];
    if(!execute(database,"CREATE TRIGGER refuse_return BEFORE DELETE ON workshop_spare_orders BEGIN SELECT RAISE(ABORT,'test return failure'); END"))return 1;
    if(!require(!WorkshopSpareStockService::returnReserve(database,returning,"OEM-1","Brakes",&errorText),"failed return committed"))return 1;
    QSqlQuery verify(database);verify.exec("SELECT COUNT(*) FROM parts_catalog");verify.next();
    if(!require(verify.value(0).toInt()==0,"catalog escaped rollback"))return 1;verify.finish();
    if(!execute(database,"DROP TRIGGER refuse_return"))return 1;
    if(!require(WorkshopSpareStockService::returnReserve(database,returning,"OEM-1","Brakes",&errorText),qPrintable(errorText))
        || !require(!WorkshopSpareStockService::returnReserve(database,returning,"OEM-1","Brakes",&errorText),"duplicate return accepted"))return 1;
    verify.exec("SELECT vehicle_brand,vehicle_model,oem_number,group_name,supplier,price FROM parts_catalog");verify.next();
    if(!require(verify.value(0).toString()=="AUDI" && verify.value(1).toString()=="Q7" && verify.value(2).toString()=="OEM-1" && verify.value(3).toString()=="Brakes" && verify.value(4).toString().isEmpty() && verify.value(5).toDouble()==0,"catalog incorrect or contains purchase information"))return 1;verify.finish();
    if(!WorkshopSpareStockService::addOrder(database,spare,&spareId,&errorText))return 1;
    if(!require(WorkshopSpareStockService::returnReserve(database,WorkshopSpareStockService::orders(database)[0],"","",&errorText),qPrintable(errorText)))return 1;
    verify.exec("SELECT COUNT(*) FROM sqlite_master WHERE name='workshop_spare_references'");verify.next();
    if(!require(verify.value(0).toInt()==0,"incomplete identification retained"))return 1;verify.finish();
    InvoiceImport::ImportedPartDraft invoiceDraft;invoiceDraft.name="Brake pad";invoiceDraft.oemNumber="INVOICE-OEM";
    QList<QStringList> invoiceRows={{"Name","OEM","Vehicle make","Vehicle model","Vehicle node"},{"Brake pad","INVOICE-OEM","VW","Golf","Brakes"}};
    const auto identity=InvoiceImport::catalogIdentity(invoiceRows,invoiceDraft);
    if(!require(identity["oem"].toString()=="INVOICE-OEM" && identity["brand"].toString()=="VW","invoice identity not extracted"))return 1;
    auto incomplete=invoiceRows;incomplete[1][4]="";
    if(!require(InvoiceImport::catalogIdentity(incomplete,invoiceDraft).isEmpty(),"incomplete invoice accepted"))return 1;
    auto ambiguous=invoiceRows;ambiguous.append(invoiceRows[1]);
    if(!require(InvoiceImport::catalogIdentity(ambiguous,invoiceDraft).isEmpty(),"ambiguous invoice accepted"))return 1;
    if(!WorkshopSpareStockService::addOrder(database,spare,&spareId,&errorText))return 1;
    verify.prepare("INSERT INTO workshop_spare_invoice_identity(spare_id,payload) VALUES(?,?)");verify.addBindValue(spareId);verify.addBindValue(QString::fromUtf8(QJsonDocument(identity).toJson()));
    if(!require(verify.exec(),"invoice identity not saved"))return 1;
    if(!require(WorkshopSpareStockService::returnReserve(database,WorkshopSpareStockService::orders(database)[0],{},{},&errorText),qPrintable(errorText)))return 1;
    verify.exec("SELECT vehicle_brand,vehicle_model,group_name FROM parts_catalog WHERE oem_number='INVOICE-OEM'");
    if(!require(verify.next() && verify.value(0)=="VW" && verify.value(1)=="Golf" && verify.value(2)=="Brakes","return did not use invoice identity"))return 1;verify.finish();
    verify.prepare("SELECT COUNT(*) FROM workshop_spare_invoice_identity WHERE spare_id=?");verify.addBindValue(spareId);verify.exec();verify.next();
    if(!require(verify.value(0).toInt()==0,"returned invoice identity remained"))return 1;verify.finish();
    WorkshopInventoryPurchaseInput bottle;
    bottle.purchasedOn=QDate::currentDate();bottle.materialName="Bottle lubricant";bottle.unit=QString::fromUtf8("мл");
    bottle.quantity=300;bottle.unitCost=InventoryPriceInput::unitPrice(500,300,true);
    bottle.clientPrice=InventoryPriceInput::unitPrice(600,300,true);bottle.paidAmount=0;
    qint64 bottleId=0;
    if(!require(WorkshopInventoryService::addPurchase(database,bottle,&bottleId,&errorText),qPrintable(errorText)))return 1;
    const auto bottleDebt=PurchasePaymentService::detail(database,"inventory",bottleId,{},&errorText);
    if(!require(bottleDebt.totalCents==50000 && bottleDebt.remainingCents()==50000,"300ml bottle debt must be 500 UAH"))return 1;
    auto bottlePricing=pricing;bottlePricing.partName=bottle.materialName;bottlePricing.sourceType="manual_inline";
    bottlePricing.store=QString::fromUtf8("Склад СТО");bottlePricing.qty=300;bottlePricing.purchasePrice=bottle.unitCost;
    auto bottleResult=PartPricingEngine::calculate(bottlePricing,PartMarkupPolicyService::loadPolicySnapshot(database));
    if(!require(approximatelyEqual(bottleResult.purchaseAmount,500) && approximatelyEqual(bottleResult.clientAmount,600),"whole bottle pricing rounded too early"))return 1;
    bottlePricing.qty=30;
    bottleResult=PartPricingEngine::calculate(bottlePricing,PartMarkupPolicyService::loadPolicySnapshot(database));
    if(!require(approximatelyEqual(bottleResult.purchaseAmount,50) && approximatelyEqual(bottleResult.clientAmount,60),"partial bottle pricing incorrect"))return 1;
    const auto bottleSnapshot=WorkshopInventoryService::buildSnapshot(database,QDate(2026,1,1),QDate::currentDate());
    bool bottleFound=false;
    for(const auto& balance:bottleSnapshot.balanceRows)if(balance.materialName==bottle.materialName){
        bottleFound=true;if(!require(approximatelyEqual(balance.balanceQuantity,300) && approximatelyEqual(balance.stockValue,500),"bottle stock value incorrect"))return 1;
    }
    if(!require(bottleFound,"bottle missing from stock") || !require(InventoryPriceInput::unitPrice(500,300,false)==500,"per-unit mode broken")
       || !require(InventoryPriceInput::unitPrice(500,0,true)<0,"zero quantity accepted"))return 1;
    for(const QString settlement:{QString("refund"),QString("credit")}) {
        if(!WorkshopSpareStockService::addOrder(database,spare,&spareId,&errorText))return 1;
        auto toReturn=WorkshopSpareStockService::orders(database)[0];
        if(!WorkshopSpareStockService::receive(database,toReturn,1,QDate::currentDate(),50.25,&errorText))return 1;
        toReturn=WorkshopSpareStockService::orders(database)[0];
        const auto receipt=WorkshopSpareStockService::receipts(database,toReturn.id)[0];
        verify.exec("SELECT COUNT(*) FROM purchase_payment_events");verify.next();const auto eventCount=verify.value(0).toInt();verify.finish();
        if(!execute(database,"CREATE TRIGGER refuse_return_settlement BEFORE INSERT ON workshop_spare_returns BEGIN SELECT RAISE(ABORT,'test settlement failure'); END"))return 1;
        if(!require(!WorkshopSpareStockService::returnReserve(database,toReturn,"OEM-1","Brakes",&errorText,settlement,5025),"failed settlement committed"))return 1;
        if(!execute(database,"DROP TRIGGER refuse_return_settlement"))return 1;
        if(!require(WorkshopSpareStockService::returnReserve(database,toReturn,"OEM-1","Brakes",&errorText,settlement,5025),qPrintable(errorText)))return 1;
        if(!require(!WorkshopSpareStockService::returnReserve(database,toReturn,"OEM-1","Brakes",&errorText,settlement,5025),"duplicate settlement accepted"))return 1;
        if(!require(PurchasePaymentService::detail(database,"inventory",receipt.purchaseId,{},&errorText).id==0,"returned purchase remains payable"))return 1;
        for(const auto& active:WorkshopSpareStockService::orders(database))if(!require(active.id!=toReturn.id,"returned reserve still active"))return 1;
        verify.exec("SELECT COUNT(*) FROM purchase_payment_events");verify.next();if(!require(verify.value(0).toInt()==eventCount,"financial history erased"))return 1;verify.finish();
        verify.prepare("SELECT paid_cents,settlement FROM workshop_spare_returns WHERE spare_id=?");verify.addBindValue(toReturn.id);verify.exec();verify.next();
        if(!require(verify.value(0).toLongLong()==5025 && verify.value(1).toString()==settlement,"wrong settlement amount/type"))return 1;verify.finish();
    }
    QVector<WorkshopInventoryBalanceRow> metricRows;
    const auto metricRow=[&](QString name,QString unit,double quantity,double value) {
        WorkshopInventoryBalanceRow row;row.materialName=name;row.unit=unit;row.balanceQuantity=quantity;row.stockValue=value;metricRows.append(row);
    };
    metricRow(QString::fromUtf8("Олива 5W40"),QString::fromUtf8("л"),175,35000);
    metricRow(QString::fromUtf8("Мастило"),QString::fromUtf8("мл"),300,500);
    metricRow(QString::fromUtf8("Антифриз"),QString::fromUtf8("л"),15,4500);
    metricRow(QString::fromUtf8("Очищувач"),QString::fromUtf8("л"),2,100);
    metricRow(QString::fromUtf8("Серветка"),QString::fromUtf8("шт."),5,50);
    const auto metrics=InventoryStockSummary::calculate(metricRows);
    if(!require(approximatelyEqual(metrics.quantities[0].value(QString::fromUtf8("л")),175.3)
        && metrics.quantities[1].value(QString::fromUtf8("л"))==15
        && metrics.quantities[2].value(QString::fromUtf8("л"))==2
        && metrics.quantities[2].value(QString::fromUtf8("шт."))==5
        && metrics.value==40150,"stock tiles quantities, units or value incorrect"))return 1;
    std::cout << "Workshop inventory, stock tiles, reserve return and 300ml/500UAH pricing regression checks passed\n";
    database.close();
    database = QSqlDatabase();
    QSqlDatabase::removeDatabase(connectionName);
    return 0;
}
