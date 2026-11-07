#include "core/services/OrderSearchRepository.h"
#include "ui/common/dialogs/OrderDialogFlowController.h"
#include <QApplication>
#include <QSpinBox>
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>
int main(int argc,char** argv){
 QApplication app(argc,argv);auto db=QSqlDatabase::addDatabase("QSQLITE","synthetic-search");db.setDatabaseName(":memory:");if(!db.open())return 1;
 QSqlQuery q(db);for(const auto& sql:QStringList{
 "CREATE TABLE router_order_locator(order_uid TEXT,number INTEGER,order_number INTEGER,order_year INTEGER,opened_at TEXT,closed_at TEXT,status TEXT,plate TEXT,car TEXT,client TEXT,storage_kind TEXT,storage_path TEXT)",
 "INSERT INTO router_order_locator VALUES('old-uid',101,7,2025,'2025-02-03','2025-03-04','closed','TEST-A','Synthetic A','Synthetic','archive','synthetic-old.db'),('new-uid',202,7,2026,'2026-02-03','','open','TEST-B','Synthetic B','Synthetic','active','')"})if(!q.exec(sql)){qCritical()<<q.lastError();return 2;}
 QString error;const auto hits=OrderSearchRepository::findByVisibleNumber(db,7,100,&error);
 if(!error.isEmpty()||hits.size()!=2||hits[0].orderUid!="new-uid"||hits[1].orderUid!="old-uid"||hits[0].orderYear!=2026||hits[1].orderYear!=2025)return 3;
 QSpinBox number;number.setRange(1,1000000);number.show();
 for(const auto& hit:hits){number.setEnabled(true);number.setValue(hit.internalNumber);OrderDialogFlowController::applyResolvedDisplayNumber(&number,hit.visibleNumber,0);app.processEvents();if(number.text()!="7"||number.isEnabled())return 4; qInfo()<<hit.orderUid<<hit.internalNumber<<hit.orderYear<<number.text();}
 if(!OrderSearchRepository::findByVisibleNumber(db,999,100,&error).isEmpty()||!error.isEmpty())return 5;
 qInfo()<<"PASS actual search repository returns both stable identities; actual spinbox controller displays only 7 for both";
}
