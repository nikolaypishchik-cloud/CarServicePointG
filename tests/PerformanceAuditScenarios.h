#pragma once
#include "PerformanceAuditSupport.h"
#include <QCryptographicHash>
#include <QDataStream>
#include <QSaveFile>
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
#include "core/services/ReportsPdfSupport.h"
#include "ui/tabs/crm/CrmPage.h"
#include <QSqlError>
#include <QSqlQuery>
#include <QTableView>
#include <QTableWidget>
#include <QAbstractItemModel>
#include <QLoggingCategory>

namespace PerformanceAudit {
// Compare the registry before and after performance work without exporting the
// customer's invoice contents. Keep display order and order-to-vehicle links.
inline bool saveInvoiceFingerprint(QSqlDatabase db,const QString& path) {
    QString error;
    const auto invoices=InvoiceRegistryService::load(db,&error);
    if (!error.isEmpty()) return false;
    QCryptographicHash hash(QCryptographicHash::Sha256);
    for (const auto& invoice:invoices) {
        QByteArray bytes;
        QDataStream stream(&bytes,QIODevice::WriteOnly);
        stream.setVersion(QDataStream::Qt_6_0);
        stream << invoice.key << invoice.number << invoice.supplier << invoice.date << invoice.dueDate << invoice.paidAt
               << invoice.remoteIdentity << invoice.orders << invoice.vehicles << invoice.totalCents << invoice.paidCents
               << invoice.amountMismatch << invoice.apportioned << qint64(invoice.parts.size());
        for (const auto& part:invoice.parts)
            stream << part.source << part.id << part.orderUid << part.orderNumber << part.date << part.name << part.supplier
                   << part.oem << part.invoice << part.car << part.note << part.unit << part.quantity << part.dueDate
                   << part.totalCents << part.paidCents;
        stream << qint64(invoice.distribution.size());
        for (const auto& slice:invoice.distribution)
            stream << slice.purchaseId << slice.partId << slice.orderNumber << slice.quantity << slice.total;
        if (stream.status()!=QDataStream::Ok) return false;
        hash.addData(bytes);
    }
    const auto result=QJsonDocument(QJsonObject{{"invoices",invoices.size()},
            {"sha256",QString::fromLatin1(hash.result().toHex())}}).toJson();
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(result)==result.size() && file.commit();
}
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
    const auto selectedScenarios=qEnvironmentVariable("CS_PERF_SCENARIOS");
    if ((selectedScenarios.isEmpty() || selectedScenarios.split(',').contains("invoices.registry"))
            && !saveInvoiceFingerprint(db,output+"/invoice-fingerprint-"+label+".json")) return 1;
    rec.run("debts.list","service",[&]{QString e;auto r=PurchasePaymentService::debts(db,&e);return checked(r.size(),e);},reset);
    rec.run("catalog.all","service",[&]{QString e;auto r=CatalogQueryService::loadAllRows(&e);return checked(r.size(),e);},reset);
    rec.run("inventory.snapshot","service",[&]{auto r=WorkshopInventoryService::buildSnapshot(db,from,to);return checked(r.purchases.size(),r.errorText);},reset);
    rec.run("order.editor-data","service",[&]{auto h=WorkPartsQueryService::loadHeaderSnapshot(uid);auto p=WorkPartsQueryService::loadPartRowsForOrder(uid);auto s=WorkPartsQueryService::loadServiceRowsForOrder(uid);return checked(p.size()+s.size(),h.errorText);},reset);
    rec.run("order.print-data","service",[&]{auto p=OrderDialogPrintQueryService::loadPartRows(uid);auto s=OrderDialogPrintQueryService::loadServiceRows(uid);return checked(p.size()+s.size());},reset);
    const QDate pdfFrom(2026,10,1),pdfTo(2026,10,31);
    rec.run("pdf.period-october-2026","service",[&]{ReportsPdfRenderer::ExportResult r;const bool ok=ReportsPdfRenderer::renderPeriodPdf(pdfFrom,pdfTo,&r);return Result{ok&&r.success,r.recordCount,ok?QString():r.errorText};},reset);
    if(qEnvironmentVariableIsSet("CS_PERF_PERIOD_EVIDENCE")) {
        double parts=0,works=0,markup=0,outsource=0,salary=0,expenses=0;
        QString readError;
        const auto lines=ReportsPdfSupport::collectPeriodPdfRows(pdfFrom.toString(Qt::ISODate),pdfTo.toString(Qt::ISODate),
            parts,works,markup,outsource,salary,expenses,&readError);
        if(!readError.isEmpty())return 1;
        QCryptographicHash hash(QCryptographicHash::Sha256);
        for(const auto& line:lines) {
            QByteArray bytes;
            QDataStream stream(&bytes,QIODevice::WriteOnly);
            stream.setVersion(QDataStream::Qt_6_0);
            stream<<line.date<<line.orderNumber<<line.car<<line.plate<<line.workTitle<<line.partName<<line.qty
                <<qRound64(100*line.partsPrice)<<qRound64(100*line.added)<<qRound64(100*line.worksPrice)<<qRound64(100*line.total);
            if(stream.status()!=QDataStream::Ok)return 1;
            hash.addData(bytes);
        }
        const QJsonObject cents{{"parts",qRound64(parts*100)},{"works",qRound64(works*100)},{"markup",qRound64(markup*100)},
            {"outsource",qRound64(outsource*100)},{"salary",qRound64(salary*100)},{"expenses",qRound64(expenses*100)}};
        const auto content=QJsonDocument(QJsonObject{{"rows",lines.size()},{"sha256",QString::fromLatin1(hash.result().toHex())},
            {"totals_cents",cents}}).toJson();
        QSaveFile file(output+"/period-fingerprint-"+label+".json");
        if(!file.open(QIODevice::WriteOnly)||file.write(content)!=content.size()||!file.commit())return 1;
    }
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
    rec.run("ui.debts.open-close","ui",[&] {
        PurchaseDebtsPage page;
        page.resize(1300,760);
        page.show();
        page.refresh();
        auto* table=page.findChild<QTableView*>("purchaseDebtsTable");
        if (!table || !table->model()) return Result{false,0,"Debt table or model missing"};
        const bool ready=waitUntil([&]{return !page.property("debtDataLoading").toBool();});
        const auto error=ready ? page.property("debtLoadError").toString() : QStringLiteral("UI readiness timeout");
        return checked(table->model()->rowCount(),error);
    },reset);
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
