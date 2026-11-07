#include "core/services/SupplierDebtReadService.h"
#include "core/services/AccessControlService.h"
#include "core/util/StoreNameNormalizer.h"
#include "infrastructure/db/sqlite/Db.h"
#include <QSqlError>
#include <QScopeGuard>
#include <QUuid>
#include <algorithm>
namespace SupplierDebtReadService {
namespace {
template<class F> void withRead(const QString& path,QString& error,F action) {
    const auto name="supplier_read_"+QUuid::createUuid().toString(QUuid::Id128);
    const auto remove=qScopeGuard([name]{QSqlDatabase::removeDatabase(name);});
    auto db=QSqlDatabase::addDatabase("QSQLITE",name);
    db.setDatabaseName(path);db.setConnectOptions("QSQLITE_OPEN_READONLY;QSQLITE_BUSY_TIMEOUT=3000");
    if(path.isEmpty() || !db.open() || !db.transaction()) {
        error=db.lastError().text();if(error.isEmpty())error="Database path is unavailable";return;
    }
    const auto close=qScopeGuard([&db]{db.rollback();db.close();});
    action(db);
}
}
PaymentTask preparePayment(const QString& source,qint64 id,const QString& uid) {
    const auto path=Database::db().databaseName();
    return [path,source,id,uid](const std::atomic_bool& cancel,QString* errorOut) {
        PurchaseDebt result;QString error;
        if(!cancel.load())withRead(path,error,[&](QSqlDatabase db) {
            if(!cancel.load())result=PurchasePaymentService::detail(db,source,id,uid,&error);
        });
        if(errorOut)*errorOut=error;
        if(cancel.load() || !error.isEmpty())result={};
        return result;
    };
}
OrderPaymentTask prepareOrderPayment(int orderNumber) {
    const auto path=Database::db().databaseName();
    return [path,orderNumber](const std::atomic_bool& cancel,QString* errorOut) {
        PurchasePaymentService::OrderPaymentPreparation result;QString error;
        if(!cancel.load())withRead(path,error,[&](QSqlDatabase db) {
            if(!cancel.load())result=PurchasePaymentService::prepareOrderPayment(db,orderNumber,&error);
        });
        if(errorOut)*errorOut=error;
        if(cancel.load() || !error.isEmpty())result={};
        return result;
    };
}
Task prepare(bool analysis) {
    const auto path=Database::db().databaseName();
    const auto scope=AccessControlService::currentOrderScope();
    return [path,scope,analysis](const std::atomic_bool& cancel) {
        Snapshot result;result.restricted=scope.restricted;
        if(cancel.load())return result;
        withRead(path,result.error,[&](QSqlDatabase db) {
            auto& error=result.error;
            result.debts=PurchasePaymentService::debts(db,&error);
            if(error.isEmpty() && !cancel.load() && analysis)result.parts=PartsQueryService::loadRows(db,{},{},{},&error);
            if(error.isEmpty() && !cancel.load())result.reconciliations=SupplierDebtReconciliationService::load(db,&error);
            if(error.isEmpty() && !cancel.load())result.debts=SupplierInvoiceLedger::reportingDebts(db,result.debts,&result.reconciliations,&error);
            if(scope.restricted) {
                result.debts.erase(std::remove_if(result.debts.begin(),result.debts.end(),[&](const auto& d){return d.source!="part" || !scope.contains(d.orderUid,d.orderNumber);}),result.debts.end());
                result.parts.erase(std::remove_if(result.parts.begin(),result.parts.end(),[&](const auto& p){return !scope.contains(p.orderUid,p.orderNumber);}),result.parts.end());
                // Reconciliations/remote supplier totals are global, never expose them to a master.
                result.reconciliations.clear();
            }
            if(analysis && error.isEmpty() && !cancel.load()) {
                QSet<QString> keys;
                for(const auto& d:result.debts)if(d.source=="part")keys.insert(InvoiceEvidenceService::key(d.orderUid,int(d.id)));
                QVector<PartRowSnapshot> selected;
                for(const auto& p:result.parts)if(keys.contains(InvoiceEvidenceService::key(p.orderUid,p.id)))selected.append(p);
                result.evidence=InvoiceEvidenceService::load(db,selected,&error);
            }
            if(!scope.restricted && error.isEmpty() && !cancel.load())result.ledger=SupplierInvoiceLedger::summary(db,result.debts,result.reconciliations,&error);
        });
        if(!result.error.isEmpty() || cancel.load()) {
            const auto error=result.error;result={};result.error=error;result.restricted=scope.restricted;
        }
        return result;
    };
}
BannerTask prepareBanner() {
    const auto path=Database::db().databaseName();const auto scope=AccessControlService::currentOrderScope();
    return [path,scope](const std::atomic_bool& cancel,QString* errorOut) {
        QMap<QString,qint64> totals;QString error;
        if(!cancel.load())withRead(path,error,[&](QSqlDatabase db) {
            const auto debts=PurchasePaymentService::debts(db,&error);
            if(!error.isEmpty() || cancel.load())return;
            if(scope.restricted) {
                for(const auto& d:debts) {
                    if(d.source!="part" || !scope.contains(d.orderUid,d.orderNumber))continue;
                    totals[StoreNameNormalizer::canonicalDisplayNameOrDefault(d.supplier,QString::fromUtf8("Без магазину"))]+=d.remainingCents();
                }
            } else {
                const auto facts=SupplierDebtReconciliationService::load(db,&error);
                if(error.isEmpty() && !cancel.load())totals=SupplierInvoiceLedger::summary(db,debts,facts,&error).totals;
                for(auto it=totals.begin();it!=totals.end();)if(it.value()<=0)it=totals.erase(it);else ++it;
            }
        });
        if(errorOut)*errorOut=error;
        if(!error.isEmpty() || cancel.load())totals.clear();
        return totals;
    };
}
ReconciliationInput reconciliationInput() {
    const std::atomic_bool cancel{false};
    return prepareReconciliation()(cancel);
}
ReconciliationTask prepareReconciliation() {
    const auto path=Database::db().databaseName();
    const bool allowed=SupplierDebtReconciliationService::canReconcile();
    return [path,allowed](const std::atomic_bool& cancel) {
        ReconciliationInput r;
        if(!allowed){r.error=QString::fromUtf8("Недостатньо прав для звірки.");return r;}
        if(cancel.load())return r;
        withRead(path,r.error,[&](QSqlDatabase db) {
            r.names=SupplierDebtReconciliationService::suppliers(db,&r.error);
            if(r.error.isEmpty() && !cancel.load())r.debts=PurchasePaymentService::debts(db,&r.error);
            if(r.error.isEmpty() && !cancel.load())r.previous=SupplierDebtReconciliationService::load(db,&r.error);
            if(r.error.isEmpty() && !cancel.load())r.summary=SupplierInvoiceLedger::summary(db,r.debts,r.previous,&r.error);
        });
        if(!r.error.isEmpty() || cancel.load()){const auto error=r.error;r={};r.error=error;}
        return r;
    };
}

bool saveReconciliation(const QVector<SupplierDebtReconciliation>& entries,QString* error) {
    return SupplierDebtReconciliationService::save(Database::db(),entries,error);
}
}
