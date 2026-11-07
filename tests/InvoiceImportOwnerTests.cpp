#include "core/services/orders/InvoiceImportOwnerSupport.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QDebug>

int main(int argc,char** argv)
{
    QCoreApplication app(argc,argv);
    using namespace InvoiceImportOwnerSupport;
    int failures=0;
    auto check=[&](bool ok,const char* label){if(!ok){qWarning()<<"FAIL"<<label;++failures;}};
    auto db=QSqlDatabase::addDatabase("QSQLITE");db.setDatabaseName(":memory:");check(db.open(),"open");
    auto sql=[&](const QString& statement){QSqlQuery q(db);check(q.exec(statement),qPrintable(statement));};
    sql("CREATE TABLE work_orders(number INTEGER PRIMARY KEY,order_uid TEXT,order_number INT,order_year INT,opened_at TEXT,plate TEXT,car TEXT)");
    sql("CREATE TABLE order_registry(order_uid TEXT,order_number INT,visible_order_number INT,order_year INT,opened_at TEXT,plate TEXT,car TEXT,storage_kind TEXT,storage_path TEXT)");
    sql("INSERT INTO work_orders VALUES(99,'old',7,2024,'2024-02-01','OLD','Car A'),(7,'physical-collision',8,2026,'2026-01-01','OTHER','Car B')");
    QString error;
    auto hits=candidates(db,7,&error);
    check(error.isEmpty()&&hits.size()==1&&select(hits,{},&error)=="old","visible only, invoice date independent");
    check(select(candidates(db,99,&error),{},&error).isEmpty()&&!error.isEmpty(),"internal number is not visible");
    sql("INSERT INTO order_registry VALUES('archive',120,7,2023,'2023-01-01','ARC','Car C','archive','synthetic.db')");
    hits=candidates(db,7,&error);check(hits.size()==2,"active plus archive");
    check(select(hits,{},&error).isEmpty(),"ambiguity requires selection");
    check(select(hits,"archive",&error)=="archive","explicit archived UID");
    check(select(hits,"unknown",&error).isEmpty(),"stale choice rejected");
    check(validateActive(db,99,7,"old",&error),"owner accepted");
    check(!validateActive(db,7,7,"old",&error),"physical collision rejected");
    check(db.transaction(),"transaction");sql("UPDATE work_orders SET order_uid='changed' WHERE number=99");
    check(!validateActive(db,99,7,"old",&error),"transaction recheck");check(db.rollback(),"rollback");
    check(validateActive(db,99,7,"old",&error),"repair repeat");
    sql("INSERT INTO order_registry SELECT * FROM order_registry");
    check(candidates(db,7,&error).isEmpty()&&!error.isEmpty(),"duplicate UID is corruption");
    sql("DELETE FROM order_registry");
    QTemporaryDir temp;check(temp.isValid(),"temporary fixture");
    OrderSearchHit archived;archived.internalNumber=120;archived.visibleNumber=7;archived.orderUid="archive";archived.storagePath=temp.filePath("archive.db");
    check(!validateStoredOwner(db,archived,&error)&&!QFileInfo::exists(archived.storagePath),"missing archive never created");
    {
        auto source=QSqlDatabase::addDatabase("QSQLITE","synthetic-archive");source.setDatabaseName(archived.storagePath);check(source.open(),"archive create");
        QSqlQuery q(source);check(q.exec("CREATE TABLE work_orders(number INT,order_number INT,order_uid TEXT)"),"archive schema");
        check(q.exec("INSERT INTO work_orders VALUES(120,7,'archive')"),"archive owner");source.close();
    }
    QSqlDatabase::removeDatabase("synthetic-archive");
    const auto archiveBytes=fileDigest(archived.storagePath);
    check(validateStoredOwner(db,archived,&error),"physical archived owner");
    archived.orderUid="changed";check(!validateStoredOwner(db,archived,&error),"physical archive owner mismatch");archived.orderUid="archive";
    check(fileDigest(archived.storagePath)==archiveBytes,"archive remains unchanged");
    archived.internalNumber=7;check(!validateStoredOwner(db,archived,&error),"occupied archive slot rejected");
    const QString path=temp.filePath("invoice.pdf");QFile file(path);check(file.open(QIODevice::WriteOnly),"file open");file.write("synthetic invoice bytes");file.close();
    auto digest=fileDigest(path);
    sql("CREATE TABLE work_order_parts(id INT,order_number INT,order_uid TEXT,part_uid TEXT)");
    sql("CREATE TABLE work_order_part_attachments(part_id INT,file_path TEXT)");
    sql("INSERT INTO work_order_parts VALUES(1,99,'old','part-one')");
    {QSqlQuery q(db);q.prepare("INSERT INTO work_order_part_attachments VALUES(1,?)");q.addBindValue(path);check(q.exec(),"attachment");}
    check(verifyReceipt(db,"[1]",{"old"},digest,&error),"existing v1 accepted");
    check(verifyReceipt(db,"[1]",{"old"},digest,&error),"v1 repeat");
    check(!verifyReceipt(db,"[1]",{"archive"},digest,&error),"foreign owner blocked");
    check(!verifyReceipt(db,"[1,1]",{"old","old"},digest,&error),"duplicate receipt blocked");
    check(!verifyReceipt(db,"[1]",{"old","old"},digest,&error),"row loss blocked");
    sql("UPDATE work_order_parts SET order_uid='archive'");
    check(!verifyReceipt(db,"[1]",{"old"},digest,&error),"child owner blocked");
    sql("UPDATE work_order_parts SET order_uid='old'");
    check(file.open(QIODevice::WriteOnly|QIODevice::Truncate),"overwrite synthetic file");file.write("different bytes");file.close();
    check(!verifyReceipt(db,"[1]",{"old"},digest,&error),"changed document blocked without repair");
    check(fileDigest(path)!=digest,"bad document remains untouched");
    sql("CREATE TABLE order_deletion_publications(internal_number INT,order_uid TEXT)");
    sql("INSERT INTO order_deletion_publications VALUES(99,'old')");
    check(!validateActive(db,99,7,"old",&error),"desktop retirement guard retained");
    sql("DROP TABLE order_registry");check(candidates(db,7,&error).isEmpty()&&!error.isEmpty(),"missing search dependency");
    sql("DROP TABLE work_order_part_attachments");check(!verifyReceipt(db,"[1]",{"old"},digest,&error),"receipt SQL error");
    qInfo()<<"Invoice owner production Qt/SQLite checks failures:"<<failures;
    return failures?1:0;
}
