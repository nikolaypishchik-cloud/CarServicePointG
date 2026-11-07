#include <QApplication>
#include <QFileInfo>
#include <QSqlQuery>
#include <QSqlError>
#include <QTextStream>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include "app/App.h"
#include "app/SingleInstanceCoordinator.h"
#include "runtime/AppRuntime.h"
#include "infrastructure/db/sqlite/Db.h"
#include "infrastructure/security/EncryptedStorage.h"
#include "core/backup/RestoreCoordinator.h"
#include "core/backup/BackupDtos.h"
#include "core/services/NativeBackupRestoreService.h"
#include "core/services/HistoryQueryService.h"
#include "core/services/orders/CurrentOrdersQueryService.h"
#include "core/services/ReportsDataLoader.h"
#include "core/services/OrderSearchIndex.h"
#include "core/cloudsync/CloudSyncOutboxService.h"
#include "core/cloudsync/CloudSyncClient.h"

int main(int argc,char** argv) {
    QApplication app(argc,argv);
    app.setApplicationName(App::appName()); app.setOrganizationName(App::orgName());
    QTextStream out(stdout);
    const auto args=app.arguments();
    if(args.size()<3 || args[1]!="--confirm-data-root" || QFileInfo(args[2]).absoluteFilePath()!=QFileInfo(AppRuntime::dataDir()).absoluteFilePath()) {
        out<<"Explicit --confirm-data-root <current app data directory> required\n"; return 2;
    }
    CarService::SingleInstanceCoordinator lock;
    QString error;
    if(lock.acquireOrActivate(&error)!=CarService::SingleInstanceCoordinator::StartResult::PrimaryInstance) { out<<"Application must be closed\n"; return 3; }
    if(args.size()==4 && (args[3]=="--audit-sync" || args[3]=="--repair-sync" || args[3]=="--sync-once" || args[3]=="--retry-sync")) {
        if(!Database::prepareRuntime(&error)||!Database::openPrepared(&error)) { out<<error; return 4; }
        bool ok=true;
        if(args[3]=="--repair-sync") {
            ok=CloudSyncOutboxService::retireLegacyMetadata(Database::db(),&error);
            out<<"REPAIR "<<ok<<" "<<error<<Qt::endl;
        }
        if(args[3]=="--retry-sync") {
            QSqlQuery retry(Database::db());
            ok=retry.exec("UPDATE sync_outbox SET next_retry_at='' WHERE status='failed'");
            if(!ok) error=retry.lastError().text();
        }
        if(ok && (args[3]=="--sync-once" || args[3]=="--retry-sync")) {
            ok=CloudSyncClient::syncNow(&error);
            out<<"SYNC "<<ok<<" "<<error<<Qt::endl;
        }
        {
            QSqlQuery q(Database::db());
            const QStringList queries={
                "SELECT entity_type,status,COUNT(*),SUBSTR(MAX(last_error),1,700) FROM sync_outbox GROUP BY 1,2",
                "SELECT entity_type,status,COUNT(*),SUBSTR(MAX(apply_error),1,250) FROM sync_inbox GROUP BY 1,2",
                "SELECT entity_type,conflict_type,status,COUNT(*) FROM sync_conflicts GROUP BY 1,2,3",
                "SELECT entity_type,operation,json_extract(payload_json,'$.payload_source'),COUNT(*) FROM sync_outbox WHERE status<>'acked' GROUP BY 1,2,3"
            };
            for(const auto& sql:queries) {
                out<<sql<<Qt::endl;
                if(!q.exec(sql)) { out<<q.lastError().text()<<Qt::endl; ok=false; continue; }
                while(q.next()) {
                    QJsonArray row;
                    for(int i=0;i<4;++i) row.append(QJsonValue::fromVariant(q.value(i)));
                    out<<QJsonDocument(row).toJson(QJsonDocument::Compact)<<Qt::endl;
                }
            }
        }
        Database::close();
        for(const auto& name:QSqlDatabase::connectionNames()) QSqlDatabase::database(name,false).close();
        if(!EncryptedStorage::sealRuntimeDatabases(&error)) { out<<error; return 11; }
        return ok?0:12;
    }
    if(!Database::prepareRuntime(&error)||!Database::openPrepared(&error)) { out<<error; return 4; }
    if(args.size()==5 && args[3]=="--restore") {
        Database::close();
        if(!EncryptedStorage::sealRuntimeDatabases(&error)||!Database::prepareRuntime(&error)||!Database::openPrepared(&error)) { out<<error; return 4; }
        // The coordinator creates its own rollback snapshot and validates before replacing.
        RestoreCoordinator coordinator(nullptr,nullptr);
        SnapshotMetadata meta; meta.snapshotPath=QFileInfo(args[4]).absoluteFilePath();
        NativeBackupRestoreResult result;
        if(!coordinator.restoreSnapshot(meta,&result,&error)) { out<<"RESTORE FAILED: "<<error; Database::close(); return 5; }
        out<<"RESTORED orders="<<result.restoredOrderCount<<" rollback="<<result.rollbackBackupPath<<"\n";
    } else if(args.size()!=3) { Database::close(); return 2; }
    QElapsedTimer startupTimer; startupTimer.start();
    if(!Database::runStartupSchemaCheck(&error)||!Database::runStartupMigrationCheck(&error)||!Database::runStartupRegistryRefresh(&error)) {
        out<<"SCHEMA FAILED: "<<error; Database::close(); return 6;
    }
    out<<"STARTUP_CHECKS ms="<<startupTimer.elapsed()<<Qt::endl;
    QSqlQuery q(Database::db());
    if(!q.exec("PRAGMA integrity_check")||!q.next()||q.value(0).toString()!="ok") { out<<"INTEGRITY FAILED"; Database::close(); return 7; }
    out<<"INTEGRITY ok\n";
    for(const QString& table : {QString("work_orders"),QString("work_order_services"),QString("work_order_parts"),QString("order_search_v1")}) {
        if(!q.exec("SELECT COUNT(*) FROM "+table)||!q.next()) { out<<q.lastError().text(); Database::close(); return 8; }
        out<<table<<"="<<q.value(0).toInt()<<"\n";
    }
    for(const QString& pragma : {QString("journal_mode"),QString("synchronous"),QString("foreign_keys"),QString("busy_timeout"),QString("cache_size"),QString("mmap_size")}) {
        if(!q.exec("PRAGMA "+pragma)||!q.next()) { out<<q.lastError().text(); Database::close(); return 8; }
        out<<"PRAGMA "<<pragma<<"="<<q.value(0).toString()<<Qt::endl;
    }
    q.finish();
    for(const QString& filter : {QString("451"),QString("AA 1"),QString("Audi"),QString("notfoundxyz"),QString("25.09.26"),QString()}) {
        QElapsedTimer timer; timer.start();
        HistoryQueryParams params{QDate(1900,1,1),QDate(9999,12,31),filter}; params.pageSize=200;
        const auto history=HistoryQueryService::loadHistory(params);
        out<<"history filter="<<filter<<" ms="<<timer.elapsed()<<" rows="<<history.rows.size()<<" error="<<history.errorText<<"\n";
        if(!history.errorText.isEmpty()) { Database::close(); return 9; }
        timer.restart();
        const auto current=CurrentOrdersQueryService::loadOpenOrders(filter);
        out<<"current ms="<<timer.elapsed()<<" rows="<<current.rows.size()<<" error="<<current.errorText<<"\n";
        if(!current.errorText.isEmpty()) { Database::close(); return 10; }
        timer.restart();
        const auto reports=ReportsDataLoader::buildOrdersSnapshot(AppRuntime::activeDbFilePath(),"1900-01-01","9999-12-31",filter);
        out<<"reports ms="<<timer.elapsed()<<" rows="<<reports.rows.size()<<Qt::endl;
    }
    q.finish();
    q=QSqlQuery();
    Database::close();
    for(const QString& connection : QSqlDatabase::connectionNames())
        QSqlDatabase::database(connection,false).close();
    if(!EncryptedStorage::sealRuntimeDatabases(&error)) { out<<"SEAL FAILED: "<<error; return 11; }
    out<<"CLOSED\n"; return 0;
}
