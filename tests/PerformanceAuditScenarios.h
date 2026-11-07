#pragma once
#include "PerformanceAuditSupport.h"
#include "core/services/HistoryQueryService.h"
#include "core/services/OrderStoragePolicyService.h"
#include "core/services/OrderSearchIndex.h"
#include "core/services/orders/CurrentOrdersQueryService.h"
#include "core/services/orders/WorkPartsQueryService.h"
#include "core/services/orders/OrderDialogPrintQueryService.h"
#include "core/services/catalog/CatalogQueryService.h"
#include "core/services/StatisticsSnapshotService.h"
#include "core/cache/ReportsSnapshotCache.h"
#include "core/cache/OrderSnapshotCache.h"
#include "core/services/ReportsPdfRenderer.h"
#include "ui/tabs/crm/CrmPage.h"
#include <QSqlError>
#include <QSqlQuery>
#include <QTableView>
#include <QTableWidget>
#include <QAbstractItemModel>
#include <QLoggingCategory>

namespace PerformanceAudit {
inline int runAudit(QSqlDatabase db, const QString& output) {
    const auto label=qEnvironmentVariable("CS_PERF_DATASET");
    Recorder rec(output+"/samples-"+label+".jsonl",label);
    const QDate from(2000,1,1),to(2099,12,31);
    const auto df=from.toString(Qt::ISODate),dt=to.toString(Qt::ISODate);
    const auto dbPath=db.databaseName();
    auto reset=[] {
        PartsQueryService::invalidateCache();
        ReportsSnapshotCache::clearAll();
        OrderSnapshotCache::invalidateAll("performance-audit");
    };
    QSqlQuery selected(db);
    if(!selected.exec("SELECT order_uid,order_number FROM router_work_orders ORDER BY number DESC LIMIT 1") || !selected.next())return 1;
    const QString uid=selected.value(0).toString(),number=selected.value(1).toString();selected.finish();
    QString openNumber=number;
    if(selected.exec("SELECT order_number FROM router_work_orders wo WHERE NOT ("+OrderStoragePolicyService::closedOrderPredicate("wo")+") ORDER BY number DESC LIMIT 1")&&selected.next())openNumber=selected.value(0).toString();
    selected.finish();
    const auto directories=output+"/documents-"+label;
    QDir().mkpath(directories);
    GeneralSettingsService::DirectoriesSettingsPayload dirs{directories+"/orders",directories+"/invoices",directories+"/reports",directories+"/photos"};
    if(!GeneralSettingsService::saveDirectoriesSettings(dirs))return 1;
    for(const auto& d:{dirs.pdfDir,dirs.invoicesDir,dirs.reportsDir,dirs.orderPhotosDir})QDir().mkpath(d);
    QJsonObject counts;
    for(const auto& table:{"router_work_orders","router_work_order_parts","router_work_order_services","parts_catalog","crm_clients"}) {
        QSqlQuery q(db);if(q.exec("SELECT COUNT(*) FROM "+QString(table))&&q.next())counts[table]=q.value(0).toInt();
    }
    QFile countFile(output+"/runtime-counts-"+label+".json");
    const auto countBytes=QJsonDocument(counts).toJson();
    if(!countFile.open(QIODevice::WriteOnly) || countFile.write(countBytes)!=countBytes.size() || !countFile.flush())return 1;
    countFile.close();
    // Production deep-debug logging is off. Disable test-runner row-by-row diagnostics as well.
    QLoggingCategory::setFilterRules("*.debug=false\n*.info=false");
    for(const auto& filter:QList<QPair<QString,QString>>{{"all",{}},{"number",number},{"missing","AUDIT_MISSING_26XYZ"}}) {
        rec.run("current."+filter.first,"service",[&]{auto r=CurrentOrdersQueryService::loadOpenOrders(filter.first=="number"?openNumber:filter.second);return checked(r.rows.size(),r.errorText);},reset);
        rec.run("history."+filter.first,"service",[&]{HistoryQueryParams p{from,to,filter.second};p.pageSize=200;auto r=HistoryQueryService::loadHistory(p);return checked(r.rows.size(),r.errorText);},reset);
    }
    rec.run("parts.all","service",[&]{QString e;auto r=PartsQueryService::loadRows(db,{},from,to,&e);return checked(r.size(),e);},reset);
    auto snap=[](int n,const QStringList& stats){QString e;for(const auto& s:stats)if(s.contains(QString::fromUtf8("Не вдалося")))e="snapshot_error";return checked(n,e);};
    rec.run("reports.period","service",[&]{auto r=ReportsDataLoader::buildPeriodSnapshot(dbPath,df,dt);return snap(r.rows.size(),r.stats);},reset);
    rec.run("reports.orders","service",[&]{auto r=ReportsDataLoader::buildOrdersSnapshot(dbPath,df,dt);return snap(r.rows.size(),r.stats);},reset);
    rec.run("reports.parts","service",[&]{auto r=ReportsDataLoader::buildPartsSnapshot(dbPath,df,dt,0,0,{},{});return snap(r.rows.size(),r.stats);},reset);
    rec.run("reports.payroll","service",[&]{auto r=ReportsDataLoader::buildPayrollSnapshot(dbPath,df,dt);return snap(r.rows.size(),r.stats);},reset);
    rec.run("reports.outsource","service",[&]{auto r=ReportsDataLoader::buildOutsourceSnapshot(dbPath,df,dt);return snap(r.rows.size(),r.stats);},reset);
    rec.run("statistics.snapshot","service",[&]{auto r=StatisticsSnapshotService::collectFromPath(dbPath,from,to);return checked(r.closedOrders+r.openOrders);},reset);
    rec.run("invoices.registry","service",[&]{QString e;auto r=InvoiceRegistryService::load(db,&e);return checked(r.size(),e);},reset);
    rec.run("debts.list","service",[&]{QString e;auto r=PurchasePaymentService::debts(db,&e);return checked(r.size(),e);},reset);
    rec.run("catalog.all","service",[&]{QString e;auto r=CatalogQueryService::loadAllRows(&e);return checked(r.size(),e);},reset);
    rec.run("inventory.snapshot","service",[&]{auto r=WorkshopInventoryService::buildSnapshot(db,from,to);return checked(r.purchases.size(),r.errorText);},reset);
    rec.run("order.editor-data","service",[&]{auto h=WorkPartsQueryService::loadHeaderSnapshot(uid);auto p=WorkPartsQueryService::loadPartRowsForOrder(uid);auto s=WorkPartsQueryService::loadServiceRowsForOrder(uid);return checked(p.size()+s.size(),h.errorText);},reset);
    rec.run("order.print-data","service",[&]{auto p=OrderDialogPrintQueryService::loadPartRows(uid);auto s=OrderDialogPrintQueryService::loadServiceRows(uid);return checked(p.size()+s.size());},reset);
    const QDate pdfFrom(2026,10,1),pdfTo(2026,10,31);
    rec.run("pdf.period-october-2026","service",[&]{ReportsPdfRenderer::ExportResult r;const bool ok=ReportsPdfRenderer::renderPeriodPdf(pdfFrom,pdfTo,&r);return Result{ok&&r.success,r.recordCount,ok?QString():r.errorText};},reset);
    const auto backupPath=output+"/snapshot-"+label+".db";
    rec.run("backup.workspace-vacuum","service",[&]{QSqlQuery q(db);QString path=backupPath;path.replace("'","''");bool ok=q.exec("VACUUM main INTO '"+path+"'");return Result{ok,1,ok?QString():q.lastError().text()};},[&]{QFile::remove(backupPath);});

    // End-to-end QWidget creation, first completed data model, event processing and close.
    auto tableReady=[](QWidget* page){
        for(auto* t:page->findChildren<QTableView*>()) {
            auto* m=t->model();if(!m||m->rowCount()==0)continue;
            const auto text=m->data(m->index(0,0)).toString();
            if(text.contains(QString::fromUtf8("Завантаж"))||text.contains(QString::fromUtf8("Не вдалося")))continue;
            return true;
        }return false;
    };
    auto openPage=[&](const std::function<QWidget*()>& make,RuntimeJob::Family family,bool useFamily){
        const auto previous=RuntimeJob::RuntimeJobDispatcher::instance().familySnapshot(family).jobId;
        std::unique_ptr<QWidget> page(make());page->resize(1300,760);page->show();
        const bool ready=waitUntil([&]{
            if(useFamily){const auto s=RuntimeJob::RuntimeJobDispatcher::instance().familySnapshot(family);
                if(s.jobId<=previous||s.status!=RuntimeJob::Status::Succeeded)return false;}
            return tableReady(page.get()) && !page->property("debtDataLoading").toBool();
        });
        int rows=0;for(auto* t:page->findChildren<QTableView*>())if(t->model())rows+=t->model()->rowCount();
        const auto error=page->property("debtLoadError").toString();
        QCoreApplication::processEvents();page->close();page.reset();QCoreApplication::processEvents();
        return Result{ready && error.isEmpty(),rows,ready?error:"UI readiness timeout"};
    };
    rec.run("ui.current.open-close","ui",[&]{return openPage([]{return new CurrentOrdersPage;},RuntimeJob::Family::CurrentOrders,true);},reset);
    rec.run("ui.history.open-close","ui",[&]{return openPage([]{return new HistoryPage;},RuntimeJob::Family::History,true);},reset);
    rec.run("ui.parts.open-close","ui",[&]{return openPage([]{auto* p=new PartsPage;p->refresh();return p;},RuntimeJob::Family::Parts,true);},reset);
    rec.run("ui.invoices.open-close","ui",[&]{bool complete=false;InvoicesPage p;p.metricsChanged=[&](int,int,int,qint64){complete=true;};p.resize(1300,760);p.show();p.setPeriod(from,to);p.refresh();bool ok=waitUntil([&]{return complete&&tableReady(&p);});int rows=p.findChild<QTableView*>()->model()->rowCount();p.close();return Result{ok,rows,ok?QString():"UI readiness timeout"};},reset);
    rec.run("ui.debts.open-close","ui",[&]{PurchaseDebtsPage p;p.resize(1300,760);p.show();p.refresh();bool ok=waitUntil([&]{return !p.property("debtDataLoading").toBool();});return checked(p.findChild<QTableWidget*>("purchaseDebtsTable")->rowCount(),ok?p.property("debtLoadError").toString():"UI readiness timeout");},reset);
    rec.run("ui.crm.open-close","ui",[&]{CrmPage p;p.resize(1300,760);p.show();p.refresh();bool ok=waitUntil([&]{return tableReady(&p);});QCoreApplication::processEvents();int rows=0;for(auto* t:p.findChildren<QTableWidget*>())rows+=t->rowCount();return Result{ok,rows,ok?QString():"UI readiness timeout"};},reset);
    rec.run("ui.statistics.open-close","ui",[&]{StatsPage p;p.resize(1300,760);p.show();p.refresh();QCoreApplication::processEvents();return checked(p.property("performanceOrders").toList().size());},reset);

    // Profile real history SQL and focused read-path SQL on the same Qt SQLite connection.
    QJsonArray profiles;
    HistoryQueryParams historyParams{from,to,{}};historyParams.useSearchIndex=OrderSearchIndex::available(db);historyParams.pageSize=200;
    const auto historyAllSql=HistoryQueryService::buildHistorySql(historyParams);
    historyParams.filter="AUDIT_MISSING_26XYZ";
    const QList<QPair<QString,QString>> queries{
        {"history.all",historyAllSql},
        {"history.missing",HistoryQueryService::buildHistorySql(historyParams)},
        {"parts.by-order-uid","SELECT * FROM router_work_order_parts WHERE order_uid=?"},
        {"parts.supplier-totals","SELECT store,COUNT(*),SUM(purchase_amount) FROM router_work_order_parts GROUP BY store"},
        {"crm.order-links","SELECT client_uid,COUNT(*) FROM router_work_orders GROUP BY client_uid"}
    };
    for(const auto& item:queries){
        QJsonArray plan;QSqlQuery explain(db);explain.prepare("EXPLAIN QUERY PLAN "+item.second);
        if(item.second.contains('?'))explain.addBindValue(uid);
        QString planError;if(explain.exec()){while(explain.next())plan.append(QJsonObject{{"id",explain.value(0).toInt()},{"parent",explain.value(1).toInt()},{"detail",explain.value(3).toString()}});}else planError=explain.lastError().text();
        profiles.append(QJsonObject{{"name",item.first},{"sql",item.second},{"plan",plan},{"error",planError},{"parameters",item.second.contains('?')?"selected order UID, redacted":"none"}});
        rec.run("sql."+item.first,"sql",[&]{QSqlQuery q(db);q.prepare(item.second);if(item.second.contains('?'))q.addBindValue(uid);if(!q.exec())return Result{false,0,q.lastError().text()};int n=0;while(q.next())++n;return checked(n,q.lastError().isValid()?q.lastError().text():QString());});
    }
    QFile plans(output+"/sql-profile-"+label+".json");
    const auto planBytes=QJsonDocument(profiles).toJson();
    if(!plans.open(QIODevice::WriteOnly) || plans.write(planBytes)!=planBytes.size() || !plans.flush())return 1;
    return rec.failures;
}
}
