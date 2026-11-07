#include "FinancialPdfReadFixture.h"
#include "core/services/ReportsPdfExpenseSupport.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QDebug>
int main(int argc,char** argv){QCoreApplication app(argc,argv);QTemporaryDir dir;auto db=QSqlDatabase::addDatabase("QSQLITE","carservice_main");db.setDatabaseName(dir.filePath("synthetic.db"));if(!db.open())return 1;QString error;
 auto check=[&](bool ok){if(!ok)qFatal("Payroll supplement: %s",qPrintable(error));};check(FinancialPdfReadFixture::run(db,&error));QSqlQuery q(db);
 auto sql=[&](const QString& text){if(!q.exec(text)){error=q.lastError().text();check(false);}};
 auto breakdown=[&]{return ReportsPdfExpenseSupport::periodExpenseBreakdown("2026-01-01","2026-01-31",&error);};
 auto notes=[&]{return ReportsPdfExpenseSupport::periodExpenseNotes("2026-01-01","2026-01-31",&error);};
 check(breakdown().total()==0 && error.isEmpty());check(notes().cleaning.isEmpty() && error.isEmpty());
 sql("INSERT INTO expenses(period_from,period_to,cleaning,other_amount,cleaning_note) VALUES('2026-01-01','2026-01-31',12.25,7.10,'Synthetic note')");
 check(qRound64(breakdown().total()*100)==1935 && error.isEmpty());check(notes().cleaning=="Synthetic note" && error.isEmpty());
 sql("ALTER TABLE expenses RENAME COLUMN cleaning_note TO broken_note");check(notes().cleaning.isEmpty() && !error.isEmpty());sql("ALTER TABLE expenses RENAME COLUMN broken_note TO cleaning_note");check(notes().cleaning=="Synthetic note" && error.isEmpty());
 sql("ALTER TABLE expenses RENAME COLUMN cleaning TO broken_cost");check(breakdown().total()==0 && !error.isEmpty());sql("ALTER TABLE expenses RENAME COLUMN broken_cost TO cleaning");check(qRound64(breakdown().total()*100)==1935 && error.isEmpty());
 sql("DROP TABLE settings");ReportsPdfExpenseSupport::loadAdminSalarySources(&error);check(!error.isEmpty());sql("CREATE TABLE settings(key TEXT PRIMARY KEY,value TEXT)");ReportsPdfExpenseSupport::loadAdminSalarySources(&error);check(error.isEmpty());
 sql("INSERT INTO settings VALUES('settings.staff.admin_salary_sources','invalid JSON')");ReportsPdfExpenseSupport::loadAdminSalarySources(&error);check(!error.isEmpty());sql("DELETE FROM settings");ReportsPdfExpenseSupport::loadAdminSalarySources(&error);check(error.isEmpty());
 sql("INSERT INTO access_roles VALUES('master',1)");sql("UPDATE staff_accounts SET role='master'");sql("INSERT INTO access_role_permissions VALUES('master','nav.reports',1)");ActivityAuditService::setCurrentActor({"pdf-owner","Synthetic","master","test"});check(breakdown().total()==0 && !error.isEmpty());check(notes().cleaning.isEmpty() && !error.isEmpty());
 sql("UPDATE staff_accounts SET role='owner'");ActivityAuditService::setCurrentActor({"pdf-owner","Synthetic","owner","test"});check(qRound64(breakdown().total()*100)==1935 && error.isEmpty());
 qInfo()<<"PASS supplemental payroll reads: empty, exact19.35/notes, SQL failure, missing/invalid settings, repair, restricted master denied despite grant";
}
