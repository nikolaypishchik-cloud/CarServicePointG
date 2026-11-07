#include "infrastructure/security/MigrationRetainedStores.h"
#include <QCoreApplication>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QDebug>
int main(int argc,char** argv) {
 QCoreApplication app(argc,argv);QTemporaryDir root;QString error,generation;
 auto check=[&](bool ok){if(!ok)qFatal("Retained stores: %s",qPrintable(error));};
 check(root.isValid());QMap<QString,QString> stores;QMap<QString,QByteArray> before;
 for(const auto& role:QStringList{"archive","legacy","registry"}) {
  const auto path=root.filePath(role+".db");stores.insert(role,path);
  { auto db=QSqlDatabase::addDatabase("QSQLITE",role);db.setDatabaseName(path);check(db.open());
    QSqlQuery q(db);check(q.exec("CREATE TABLE sentinel(uid TEXT, cents INTEGER)"));
    q.prepare("INSERT INTO sentinel VALUES(?,?)");q.addBindValue(role+"-old-uid");q.addBindValue(1250);check(q.exec());db.close(); }
  QSqlDatabase::removeDatabase(role);QFile input(path);check(input.open(QIODevice::ReadOnly));before.insert(role,input.readAll());
 }
 const auto vault=root.filePath("retained");
 check(MigrationRetainedStores::retain(vault,stores,&generation,&error));QString repeated;
 check(MigrationRetainedStores::retain(vault,stores,&repeated,&error));check(repeated==generation);
 for(auto it=stores.cbegin();it!=stores.cend();++it) {
  QFile file(it.value());check(file.open(QIODevice::WriteOnly|QIODevice::Truncate));check(file.write("replacement")==11);file.close();
  QFile retained(QDir(generation).filePath(it.key()+".enc"));check(retained.open(QIODevice::ReadOnly));check(retained.readAll()==before[it.key()]);retained.close();
  check(QFile::remove(it.value()));check(QFile::copy(retained.fileName(),it.value()));
  {auto db=QSqlDatabase::addDatabase("QSQLITE",it.key());db.setDatabaseName(it.value());check(db.open());QSqlQuery q(db);
   check(q.exec("SELECT uid,cents FROM sentinel") && q.next());check(q.value(0).toString()==it.key()+"-old-uid" && q.value(1).toInt()==1250);db.close();}
  QSqlDatabase::removeDatabase(it.key());
 }
 check(MigrationRetainedStores::retain(vault,stores,&repeated,&error));check(repeated==generation);
 QFile corrupt(QDir(generation).filePath("archive.enc"));check(corrupt.open(QIODevice::WriteOnly|QIODevice::Truncate));corrupt.write("corrupt");corrupt.close();
 check(!MigrationRetainedStores::retain(vault,stores,&repeated,&error));check(error.contains("digest"));
 for(auto it=stores.cbegin();it!=stores.cend();++it){QFile file(it.value());check(file.open(QIODevice::ReadOnly));check(file.readAll()==before[it.key()]);}
 qInfo()<<"PASS: three-store custody; exact readback; repeat generation; synthetic restore UID/cents; corrupt receipt refusal without modifying originals";
}
