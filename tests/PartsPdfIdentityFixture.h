#pragma once
#include "FinancialPdfReadFixture.h"
namespace PartsPdfIdentityFixture {
inline bool run(QSqlDatabase db,QString* error) {
 if(!FinancialPdfReadFixture::run(db,error))return false;
 QSqlQuery q(db);auto sql=[&](const QString& text){if(q.exec(text))return true;*error=q.lastError().text();return false;};
 for(const auto& text:QStringList{
 "ALTER TABLE router_work_orders ADD COLUMN order_uid TEXT","ALTER TABLE router_work_orders ADD COLUMN opened_at TEXT","ALTER TABLE router_work_orders ADD COLUMN closed_at TEXT","ALTER TABLE router_work_orders ADD COLUMN plate TEXT","ALTER TABLE router_work_orders ADD COLUMN car TEXT","ALTER TABLE router_work_orders ADD COLUMN vin TEXT",
 "UPDATE router_work_orders SET order_uid='order-old',opened_at='2026-01-01',closed_at='2026-01-02'",
 "CREATE TABLE router_work_order_parts(id INTEGER,order_number INTEGER,order_uid TEXT,ordered_at TEXT,name TEXT,store TEXT,part_number TEXT,qty REAL,amount REAL,purchase_amount REAL,purchase_price REAL,markup_percent REAL,manual_markup_override INTEGER,pricing_calc_version INTEGER,part_uid TEXT)",
 "CREATE TABLE work_order_parts(id INTEGER,order_number INTEGER,order_uid TEXT,part_uid TEXT)","CREATE TABLE work_order_part_attachments(part_id INTEGER,invoice_number TEXT,file_path TEXT,invoice_date TEXT)",
 "CREATE TABLE workshop_spare_movements(part_id INTEGER,order_number INTEGER,operation TEXT)","CREATE TABLE workshop_inventory_purchases(id INTEGER,material_key TEXT,material_name TEXT,unit TEXT,stock_kind TEXT,purchased_on TEXT,client_price REAL,unit_cost REAL)",
 "INSERT INTO router_work_order_parts VALUES(21,11,'order-old','2026-01-01','Synthetic part','Shop','OEM',1,45.25,30.10,30.10,0,0,3,'part-old')",
 "INSERT INTO work_order_parts VALUES(21,11,'order-old','part-current')","INSERT INTO work_order_part_attachments VALUES(21,'CURRENT-INVOICE','','2026-01-01')"})if(!sql(text))return false;
 QStringList failures;auto expect=[&](bool value,const QString& message){if(!value)failures<<message;};
 auto read=[&]{return ReportsPdfSupport::collectPartsPdfGroups("2026-01-01","2026-01-31",error);};
 auto row=[](const QMap<QString,QList<ReportsPdfSupport::PartPdfRow>>& groups){return groups.isEmpty() || groups.first().isEmpty()?ReportsPdfSupport::PartPdfRow{}:groups.first().first();};
 auto groups=read();expect(error->isEmpty() && groups.size()==1,"positive UID owner");auto value=row(groups);expect(value.invoiceNumber.isEmpty(),"foreign child invoice borrowed");expect(qRound64(value.purchase*100)==3010 && qRound64(value.clientPrice*100)==4525 && qRound64(value.added*100)==1515,"exact cents");expect(value.date=="01.01.26" && value.orderNumber=="7" && value.oem=="OEM","public fields changed");
 if(!sql("UPDATE router_work_order_parts SET order_uid='unknown'"))return false;
 groups=read();expect(groups.isEmpty() && !error->isEmpty(),"unknown UID silently omitted");
 if(!sql("UPDATE router_work_order_parts SET ordered_at='2025-01-01'"))return false;
 groups=read();expect(groups.isEmpty() && error->isEmpty(),"outside-period owner changed public filter");
 if(!sql("UPDATE router_work_order_parts SET ordered_at=''"))return false;
 groups=read();expect(groups.isEmpty() && !error->isEmpty(),"undated unknown owner omitted");
 if(!sql("UPDATE router_work_order_parts SET ordered_at='2026-01-01'"))return false;
 if(!sql("UPDATE router_work_order_parts SET order_uid=''"))return false;
 if(!sql("INSERT INTO router_work_orders(number,order_number,order_uid,opened_at) VALUES(11,7,'order-other','2026-01-01')"))return false;
 groups=read();expect(groups.isEmpty() && !error->isEmpty(),"ambiguous numeric owner accepted");
 if(!sql("UPDATE router_work_order_parts SET order_uid='order-old'"))return false;
 groups=read();expect(error->isEmpty() && groups.size()==1 && groups.first().size()==1,"stable UID rejected on numeric collision");
 if(!sql("UPDATE router_work_orders SET order_uid='order-old' WHERE order_uid='order-other'"))return false;
 groups=read();expect(groups.isEmpty() && !error->isEmpty(),"duplicate order UID accepted");
 if(!sql("DELETE FROM router_work_orders WHERE order_uid='order-old' AND closed_at IS NULL"))return false;
 if(!sql("DELETE FROM router_work_orders WHERE order_uid='order-other'"))return false;
 if(!sql("UPDATE router_work_order_parts SET order_number=999,ordered_at=''"))return false;
 groups=read();expect(error->isEmpty() && row(groups).orderNumber=="7" && row(groups).date=="02.01.26","UID route or fallback date changed");
 if(!sql("UPDATE router_work_order_parts SET order_number=11,ordered_at='2026-01-01'"))return false;
 if(!sql("UPDATE work_order_parts SET part_uid='part-old'"))return false;
 groups=read();expect(error->isEmpty() && row(groups).invoiceNumber=="CURRENT-INVOICE","matching child invoice missing");
 if(!sql("UPDATE router_work_order_parts SET store='База'"))return false;
 if(!sql("INSERT INTO workshop_inventory_purchases VALUES(1,'Synthetic part','Synthetic part','шт','material','2026-01-01',NULL,30.10)"))return false;
 if(!sql("INSERT INTO workshop_spare_movements VALUES(21,99,'issue')"))return false;
 groups=read();expect(error->isEmpty() && row(groups).store==QString::fromUtf8("Склад СТО"),"foreign order stock issue borrowed");
 if(!sql("UPDATE workshop_spare_movements SET order_number=11"))return false;
 groups=read();expect(error->isEmpty() && row(groups).store!=QString::fromUtf8("Склад СТО"),"matching stock issue lost");
 if(!sql("INSERT INTO workshop_spare_movements VALUES(21,11,'issue')"))return false;
 groups=read();expect(error->isEmpty() && groups.size()==1 && groups.first().size()==1,"duplicate movement multiplied parts");
 if(!sql("UPDATE work_order_parts SET part_uid='part-current'"))return false;
 groups=read();expect(error->isEmpty() && row(groups).store==QString::fromUtf8("Склад СТО"),"foreign child stock issue borrowed");
 if(!sql("UPDATE workshop_spare_movements SET operation='receipt'"))return false;
 if(!sql("UPDATE work_order_parts SET part_uid='part-old'"))return false;
 groups=read();expect(error->isEmpty() && row(groups).store==QString::fromUtf8("Склад СТО"),"receipt treated as issue");
 // The old schema has neither child UID column: retain only the exact existing tuple match.
 if(!sql("ALTER TABLE router_work_order_parts DROP COLUMN part_uid"))return false;
 groups=read();expect(error->isEmpty() && row(groups).invoiceNumber.isEmpty(),"mixed schema inferred a child identity");
 if(!sql("ALTER TABLE work_order_parts DROP COLUMN part_uid"))return false;
 groups=read();expect(error->isEmpty() && row(groups).invoiceNumber=="CURRENT-INVOICE","old-schema exact attachment match");
 if(!sql("UPDATE router_work_order_parts SET order_uid=''"))return false;
 groups=read();expect(error->isEmpty() && groups.size()==1,"unique numeric old owner rejected");
 if(!sql("UPDATE router_work_order_parts SET order_uid='order-old'"))return false;
 if(!sql("ALTER TABLE work_order_part_attachments RENAME COLUMN invoice_number TO broken_invoice"))return false;
 groups=read();expect(groups.isEmpty() && !error->isEmpty(),"SQL failure hidden");
 if(!sql("ALTER TABLE work_order_part_attachments RENAME COLUMN broken_invoice TO invoice_number"))return false;
 for(int i=0;i<2;++i){groups=read();expect(error->isEmpty() && row(groups).invoiceNumber=="CURRENT-INVOICE","repair repeat failed");}
 *error=failures.join("; ");return failures.isEmpty();
}
}
