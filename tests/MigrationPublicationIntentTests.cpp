#include "core/services/storage/MigrationRestoreJournal.h"
#include "runtime/AppRuntime.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QDebug>
#include <cstdlib>
int main(int argc,char** argv) {
 QCoreApplication app(argc,argv);QStandardPaths::setTestModeEnabled(true);QCoreApplication::setApplicationName("Safety-PublicationIntent");
 QString error;auto check=[&](bool ok){if(!ok)qFatal("Publication intent: %s",qPrintable(error));};
 if(app.arguments().size()==3 && app.arguments()[1]=="arm") {
  check(qputenv("CS_TEST_DATA_ROOT",app.arguments()[2].toUtf8()));
  const auto snapshot=QDir(migrationSnapshotsRootPath()).filePath("snapshot_synthetic");check(QDir().mkpath(snapshot));
  QFile manifest(QDir(snapshot).filePath("manifest.json"));check(manifest.open(QIODevice::WriteOnly));check(manifest.write("{}") == 2);manifest.close();
  check(MigrationRestoreJournal::beginForSnapshot(snapshot,&error));std::_Exit(87);
 }
 QTemporaryDir root;check(root.isValid());check(qputenv("CS_TEST_DATA_ROOT",root.path().toUtf8()));
 QProcess child;child.start(app.applicationFilePath(),{"arm",root.path()});check(child.waitForStarted());check(child.waitForFinished(30000));check(child.exitCode()==87);
 QString pending;check(MigrationRestoreJournal::pending(&pending,&error));check(!pending.isEmpty());
 check(MigrationRestoreJournal::beginForSnapshot(pending,&error));
 const auto other=QDir(migrationSnapshotsRootPath()).filePath("snapshot_other");check(QDir().mkpath(other));
 QFile otherManifest(QDir(other).filePath("manifest.json"));check(otherManifest.open(QIODevice::WriteOnly));otherManifest.write("{}");otherManifest.close();
 check(!MigrationRestoreJournal::beginForSnapshot(other,&error));
 QFile manifest(QDir(pending).filePath("manifest.json"));check(manifest.open(QIODevice::WriteOnly|QIODevice::Truncate));manifest.write("changed");manifest.close();
 QString ignored;check(!MigrationRestoreJournal::pending(&ignored,&error));
 check(manifest.open(QIODevice::WriteOnly|QIODevice::Truncate));manifest.write("{}");manifest.close();
 check(MigrationRestoreJournal::pending(&ignored,&error));check(ignored==pending);
 check(MigrationRestoreJournal::finish(&error));check(MigrationRestoreJournal::pending(&ignored,&error));check(ignored.isEmpty());
 qInfo()<<"PASS: production intent survives process exit; repeat arm; conflicting snapshot and modified manifest rejected; finish clears intent";
}
