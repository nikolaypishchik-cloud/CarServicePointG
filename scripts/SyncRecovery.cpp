#include <QApplication>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFileInfo>
#include <QDebug>
#include "core/cloudsync/ConflictResolverService.h"
#include "core/cloudsync/CloudSyncOutboxService.h"
#include "core/cloudsync/CloudSyncClient.h"
#include "infrastructure/db/sqlite/TransactionRunner.h"

int main(int argc,char** argv) {
    QApplication app(argc,argv);
    app.setOrganizationName("CarServicePointG"); app.setApplicationName("CarServicePointG");
    const auto args=app.arguments();
    if(args.size()!=4 || (args[3]!="offline" && args[3]!="push" && args[3]!="verify")) return 2;
    if(QFileInfo::exists(args[2])) return 3;
    auto db=QSqlDatabase::addDatabase("QSQLITE","carservice_main"); db.setDatabaseName(args[1]);
    if(!QFileInfo::exists(args[1]) || !db.open()) return 4;
    QSqlQuery q(db); q.prepare("VACUUM INTO ?"); q.addBindValue(args[2]);
    if(!q.exec()) { qCritical()<<"Backup failed"; return 5; }
    QString error;
    if(args[3]=="verify") {
        int applied=0;
        if(!CloudSyncClient::syncNow(&error,&applied)) { qCritical()<<"Verification failed"<<error; return 17; }
        qInfo()<<"Full synchronization completed"<<applied;
        return 0;
    }
    q.prepare("SELECT payload_json FROM sync_inbox WHERE entity_type='planning_day' AND entity_uid='2026-05-28' ORDER BY id DESC LIMIT 1");
    if(!q.exec() || !q.next()) return 6;
    const auto remote=QJsonDocument::fromJson(q.value(0).toString().toUtf8()).object();
    if(!q.exec("SELECT day,note FROM planning_calendar WHERE day IN ('28.05.26','2026-05-28')") || !q.next()) return 7;
    const QJsonObject local{{"day",q.value(0).toString()},{"note",q.value(1).toString()}};
    QString uid;
    if(!ConflictResolverService::recordConflict(db,"planning_day","2026-05-28",local,remote,
            "planning_local_push_rejected",&uid,&error)
        || !ConflictResolverService::resolveConflict(db,uid,"keep_local","authorized-desktop-authority",&error)) {
        qCritical()<<"Calendar recovery failed"<<error; return 8;
    }
    if(args[3]=="offline") { qInfo()<<"Calendar rebased; desktop calendar unchanged; no network"; return 0; }
    QStringList orders;
    q.prepare("SELECT order_uid FROM work_orders WHERE number IN (7,9)");
    if(!q.exec()) return 9;
    while(q.next()) orders.append(q.value(0).toString());
    if(orders.size()!=2) return 10;
    const auto queued=TransactionRunner::run(db,"Recovery transaction failed","Recovery commit failed",[&](QString* e) {
        for(const auto& order:orders) {
            MutationResult mutation; mutation.ok=true; mutation.entityType="work_order"; mutation.entityId=order;
            mutation.operation="update"; mutation.sourceModule="AuthorizedDesktopRecovery";
            mutation.changedTables={"work_orders","work_order_services","work_order_parts"};
            if(!CloudSyncOutboxService::appendMutation(db,mutation,e)) return false;
        }
        return true;
    });
    if(!queued.ok) return 11;
    // Do not clear conflicts before the server confirms the authoritative snapshots.
    if(!CloudSyncClient::pushPendingEvents(&error)) { qCritical()<<"Server confirmation failed"<<error; return 12; }
    for(const auto& order:orders) {
        q.prepare("SELECT 1 FROM sync_outbox WHERE entity_type='work_order' AND entity_uid=? AND json_extract(payload_json,'$.source_module')='AuthorizedDesktopRecovery' AND status='acked' ORDER BY id DESC LIMIT 1");
        q.addBindValue(order); if(!q.exec() || !q.next()) return 13;
        q.prepare("SELECT conflict_uid FROM sync_conflicts WHERE entity_type='work_order' AND entity_uid=? AND resolution='pending'");
        q.addBindValue(order); if(!q.exec()) return 14;
        QStringList conflicts; while(q.next()) conflicts.append(q.value(0).toString());
        for(const auto& conflict:conflicts)
            if(!ConflictResolverService::resolveConflict(db,conflict,"keep_local","authorized-desktop-authority",&error)) return 15;
        q.prepare("UPDATE sync_inbox SET status='resolved_authoritative',apply_error='',error='',last_error='' WHERE entity_type='work_order' AND entity_uid=? AND status='conflict'");
        q.addBindValue(order); if(!q.exec()) return 16;
    }
    qInfo()<<"Server confirmed desktop snapshots and calendar";
    return 0;
}
