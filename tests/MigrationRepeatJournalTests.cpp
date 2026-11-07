// Focused production journal contract; not a replacement for the service fixture.
#include "core/services/legacyimport/LegacyImportJournalSupport.h"
#include "core/services/legacyimport/LegacyImportTypes.h"
#include "core/services/LegacyMigrationService.h"
#include "runtime/AppRuntime.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QDebug>
#include <QStandardPaths>
int main(int argc,char** argv) {
 QCoreApplication app(argc,argv);QTemporaryDir root;QString error;
 auto check=[&](bool ok){if(!ok) qFatal("Journal repeat: %s",qPrintable(error));};
 QCoreApplication::setApplicationName("Safety-RepeatJournal");QStandardPaths::setTestModeEnabled(true);
 check(root.isValid());check(qputenv("CS_TEST_DATA_ROOT",root.path().toUtf8()));
 check(AppRuntime::securityDir().startsWith(root.path()+"/"));
 LegacyImportBundle bundle;LegacyImportAuditReport audit;LegacyImportRunContext context;
 audit.sourceFingerprint="synthetic-source|100|aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
 check(LegacyImportJournalSupport::ensureCanImportFingerprint(audit.sourceFingerprint,&error));
 check(LegacyImportJournalSupport::beginRun(bundle,audit,&context,&error));
 LegacyMigrationResult result;LegacyImportVerifyReport verify;
 check(LegacyImportJournalSupport::finishSuccess(context,result,verify,&error));
 QFile history(QDir(AppRuntime::securityDir()).filePath("legacy_import_history.json"));
 check(history.open(QIODevice::ReadOnly));const auto before=history.readAll();history.close();
 QFile log(context.logFilePath);check(log.open(QIODevice::ReadOnly));const auto logBefore=log.readAll();log.close();
 for(int repeat=0;repeat<2;++repeat) {
  error.clear();check(!LegacyImportJournalSupport::ensureCanImportFingerprint(audit.sourceFingerprint,&error));
  check(error.contains(QString::fromUtf8("вже було успішно імпортоване")));
  check(history.open(QIODevice::ReadOnly));check(history.readAll()==before);history.close();
  check(log.open(QIODevice::ReadOnly));check(log.readAll()==logBefore);log.close();
 }
 check(LegacyImportJournalSupport::ensureCanImportFingerprint("different-synthetic-source",&error));
 qInfo()<<"PASS: production journal refuses successful fingerprint repeats; history and log unchanged";
}
