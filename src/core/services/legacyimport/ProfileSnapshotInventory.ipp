// Included inside the snapshot implementation's anonymous namespace.
namespace ProfileSnapshotInventory {
QString clean(const QString& path) { return QDir::cleanPath(QDir::fromNativeSeparators(path)); }
bool fail(QString* error, const QString& text) { if(error)*error=text;return false; }
bool unlinked(const QString& path, QString* error)
{
    QString cursor=QFileInfo(path).absoluteFilePath();
    for(;;) {
        const QFileInfo info(cursor);
        if(info.isSymLink() || info.isJunction())return fail(error,QStringLiteral("Linked snapshot path: %1").arg(cursor));
        const QString parent=info.absolutePath();
        if(parent==cursor)break;
        cursor=parent;
    }
    return true;
}
bool relativeSafe(const QString& path)
{
    return !path.isEmpty() && !QDir::isAbsolutePath(path) && clean(path)==path
        && !path.contains('\\') && !path.contains(':') && path!=".." && !path.startsWith("../")
        && !path.startsWith('/') && path!=".";
}
QStringList exclusions(const QList<ArtifactPair>& pairs)
{
    QStringList paths{migrationSnapshotsRootPath(),
        QDir(AppRuntime::securityDir()).filePath("migration_snapshot.lock"),
        QDir(AppRuntime::securityDir()).filePath("migration_restore_pending.json")};
    for(const auto& pair:pairs)paths<<pair.livePath;
    return paths;
}
bool excluded(const QString& path,const QStringList& skip)
{
    for(const auto& entry:skip)if(SafeFileOps::isInsideRoot(path,entry))return true;
    return false;
}
bool rootsFromSettings(const QString& settingsPath,QStringList* roots,QString* error)
{
    QMap<QString,QString> settings;
    if(!MigrationProfileSettings::storageRoots(settingsPath,&settings,error))return false;
    const QString profile=clean(QFileInfo(AppRuntime::dataDir()).absoluteFilePath());
    *roots={profile};
    QStringList external;
    for(const auto& value:settings) {
        if(value.trimmed().isEmpty())continue;
        if(!QDir::isAbsolutePath(value))return fail(error,"Snapshot storage root is not absolute");
        const QString path=clean(value);
        if(QDir(path).isRoot() || (SafeFileOps::isInsideRoot(profile,path) && !SafeFileOps::isSamePath(path,profile))
            || SafeFileOps::isInsideRoot(path,migrationSnapshotsRootPath()))
            return fail(error,"Snapshot storage root overlaps a protected ancestor or snapshot tree");
        external<<path;
    }
    std::sort(external.begin(),external.end(),[](const QString& a,const QString& b){return a.size()==b.size()?a<b:a.size()<b.size();});
    for(const auto& path:external)if(!SafeFileOps::isInsideAnyRoot(path,*roots))roots->append(path);
    for(const auto& path:*roots)if(!unlinked(path,error))return false;
    return true;
}
bool visit(const QString& root,const QString& relative,const QStringList& skip,bool hashFiles,QJsonObject* entries,QString* error)
{
    const QString directory=relative.isEmpty()?root:QDir(root).filePath(relative);
    const QFileInfo folder(directory);
    if(!folder.isDir() || !folder.isReadable() || !unlinked(directory,error))return fail(error,"Cannot enumerate snapshot directory: "+directory);
    QSet<QString> names;
    for(const auto& info:QDir(directory).entryInfoList(QDir::AllEntries|QDir::Hidden|QDir::System|QDir::NoDotAndDotDot,QDir::Name)) {
        const QString path=clean(info.absoluteFilePath());
        if(excluded(path,skip))continue;
        const QString rel=relative.isEmpty()?info.fileName():relative+"/"+info.fileName();
        if(!relativeSafe(rel) || names.contains(info.fileName().toCaseFolded()) || !unlinked(path,error))return fail(error,"Ambiguous snapshot path: "+path);
        names.insert(info.fileName().toCaseFolded());
        if(SqliteConnectionUtils::hasOtherOpenConnectionsForPath(path))return fail(error,"Snapshot file has an open database connection: "+path);
        QJsonObject metadata;if(!NativeSnapshotMetadata::capture(path,&metadata,error))return false;
        QJsonObject entry{{"permissions",int(info.permissions())},{"metadata",metadata}};
        if(info.isDir()) {
            entry.insert("kind","directory");entries->insert(rel,entry);
            if(!visit(root,rel,skip,hashFiles,entries,error))return false;
        } else if(info.isFile()) {
            entry.insert("kind","file");entry.insert("size",QString::number(info.size()));
            if(hashFiles){QString hash;if(!digest(path,&hash,error))return false;entry.insert("sha256",hash);}
            entries->insert(rel,entry);
        } else return fail(error,"Unsupported snapshot artifact: "+path);
    }
    return true;
}
bool inventory(const QStringList& roots,const QStringList& skip,bool hashes,QJsonArray* trees,QString* error,bool allowMissingRoots=false)
{
    *trees={};
    for(const auto& root:roots) {
        QJsonObject entries;
        if(allowMissingRoots && !QFileInfo::exists(root)) {
            // A deleted storage root is an empty current generation, not a lost backup.
            // Check the existing ancestor before any generation file is replaced.
            QString ancestor=QFileInfo(root).absolutePath();
            while(!QFileInfo::exists(ancestor)) {
                const QString parent=QFileInfo(ancestor).absolutePath();
                if(parent==ancestor)return fail(error,"No existing ancestor for restore root");
                ancestor=parent;
            }
            if(!unlinked(root,error) || !QFileInfo(ancestor).isDir())return fail(error,"Invalid ancestor for missing restore root");
        } else if(!visit(root,QString(),skip,hashes,&entries,error))return false;
        QJsonObject metadata;
        if(QFileInfo::exists(root) && !NativeSnapshotMetadata::capture(root,&metadata,error))return false;
        trees->append(QJsonObject{{"root",root},{"entries",entries},{"metadata",metadata}});
    }
    return true;
}
QString backupPath(const QString& snapshot,int rootIndex,const QString& relative)
{
    return QDir(snapshot).filePath(QStringLiteral("profile_tree/%1/%2").arg(rootIndex).arg(relative));
}
bool verifyStorageRootAgreement(const QString& settingsPath,QString* error)
{
    QMap<QString,QString> roots;
    if(!MigrationProfileSettings::storageRoots(settingsPath,&roots,error))return false;
    const QString path=AppRuntime::activeDbFilePath();
    if(!QFileInfo::exists(path))return true;
    const QString name="snapshot-root-read-"+QUuid::createUuid().toString(QUuid::WithoutBraces);
    auto database=QSqlDatabase::addDatabase("QSQLITE",name);
    const auto cleanup=qScopeGuard([&]{database.close();database={};SqliteConnectionUtils::safeCloseAndRemoveDatabase(name);});
    database.setDatabaseName(path);database.setConnectOptions("QSQLITE_OPEN_READONLY");
    if(!database.open())return fail(error,"Cannot verify snapshot storage roots: "+database.lastError().text());
    QSqlQuery query(database);
    if(!query.exec("SELECT 1 FROM sqlite_master WHERE type='table' AND name='settings'"))return fail(error,query.lastError().text());
    if(!query.next())return !query.lastError().isValid() || fail(error,query.lastError().text());
    query.finish();
    for(auto it=roots.begin();it!=roots.end();++it) {
        if(!query.prepare("SELECT value FROM settings WHERE key=?"))return fail(error,query.lastError().text());
        query.addBindValue(it.key());
        if(!query.exec())return fail(error,query.lastError().text());
        if(query.next() && !query.value(0).isNull()) {
            const QString value=query.value(0).toString().trimmed();
            if(!SafeFileOps::isSamePath(value,it.value()))return fail(error,QStringLiteral("Database and protected settings disagree on storage root: %1").arg(it.key()));
        }
        if(query.lastError().isValid())return fail(error,query.lastError().text());
        query.finish();
    }
    return true;
}
bool capture(const QString& snapshot,const QList<ArtifactPair>& pairs,QJsonArray* trees,QString* error)
{
    QStringList roots;
    if(!verifyStorageRootAgreement(QDir(snapshot).filePath("profile_settings.json"),error))return false;
    if(!rootsFromSettings(QDir(snapshot).filePath("profile_settings.json"),&roots,error))return false;
    const auto skip=exclusions(pairs);
    if(!inventory(roots,skip,true,trees,error))return false;
    for(int i=0;i<trees->size();++i) {
        const auto tree=trees->at(i).toObject();const auto entries=tree.value("entries").toObject();
        for(auto it=entries.begin();it!=entries.end();++it) {
            const auto entry=it.value().toObject();
            if(entry.value("kind").toString()!="file")continue;
            const auto target=backupPath(snapshot,i,it.key());
            if(!QDir().mkpath(QFileInfo(target).absolutePath())
                || !copyVerified(QDir(roots[i]).filePath(it.key()),target,entry.value("sha256").toString(),error))return false;
        }
    }
    QJsonArray after;
    if(!inventory(roots,skip,true,&after,error))return false;
    if(after!=*trees)return fail(error,"Profile changed while the snapshot was copied");
    return true;
}
bool preflight(const QString& snapshot,const QJsonArray& trees,const QList<ArtifactPair>& pairs,QJsonArray* current,QString* error,bool requireMetadata)
{
    QStringList roots;
    if(!rootsFromSettings(QDir(snapshot).filePath("profile_settings.json"),&roots,error))return false;
    if(trees.size()!=roots.size())return fail(error,"Incomplete profile root inventory");
    const auto skip=exclusions(pairs);
    for(int i=0;i<trees.size();++i) {
        const auto tree=trees[i].toObject();
        if(!tree.value("root").isString() || !SafeFileOps::isSamePath(tree.value("root").toString(),roots[i]) || !tree.value("entries").isObject())return fail(error,"Snapshot root does not match protected settings");
        if(requireMetadata && !NativeSnapshotMetadata::validate(tree.value("metadata").toObject(),true,error))return false;
        const auto entries=tree.value("entries").toObject();QSet<QString> paths;
        for(auto it=entries.begin();it!=entries.end();++it) {
            const auto entry=it.value().toObject();const auto kind=entry.value("kind").toString();
            const QString target=QDir(roots[i]).filePath(it.key());
            if(!relativeSafe(it.key()) || paths.contains(it.key().toCaseFolded()) || excluded(target,skip)
                || !entry.value("permissions").isDouble() || entry.value("permissions").toInt(-1)<0
                || (kind!="file" && kind!="directory") || !unlinked(target,error))return fail(error,"Invalid profile inventory entry: "+it.key());
            if(requireMetadata && !NativeSnapshotMetadata::validate(entry.value("metadata").toObject(),kind=="directory",error))return false;
            paths.insert(it.key().toCaseFolded());
            const QFileInfo live(target);
            if(live.exists() && ((kind=="file" && !live.isFile()) || (kind=="directory" && !live.isDir())))return fail(error,"Restore destination type conflict: "+target);
            QString parent=QFileInfo(it.key()).path();
            while(parent!="." && !parent.isEmpty()) {
                if(entries.value(parent).toObject().value("kind").toString()!="directory")return fail(error,"Missing parent in profile inventory");
                parent=QFileInfo(parent).path();
            }
            if(kind=="file") {
                QString hash;const QString source=backupPath(snapshot,i,it.key());
                if(!unlinked(source,error) || !digest(source,&hash,error) || hash!=entry.value("sha256").toString()
                    || QString::number(QFileInfo(source).size())!=entry.value("size").toString())return fail(error,"Profile snapshot file is missing or damaged: "+it.key());
            }
        }
    }
    return inventory(roots,skip,false,current,error,true);
}
bool restore(const QString& snapshot,const QJsonArray& trees,const QJsonArray& current,QString* error,bool nativeMetadata)
{
    for(int i=0;i<trees.size();++i) {
        const auto tree=trees[i].toObject();const QString root=tree.value("root").toString();const auto entries=tree.value("entries").toObject();
        if(!unlinked(root,error) || !QDir().mkpath(root))return fail(error,"Cannot recreate profile storage root: "+root);
        for(auto it=entries.begin();it!=entries.end();++it)if(it.value().toObject().value("kind").toString()=="directory") {
            const QString path=QDir(root).filePath(it.key());
            if(!unlinked(path,error) || !QDir().mkpath(path))return fail(error,"Cannot recreate profile directory: "+path);
        }
        for(auto it=entries.begin();it!=entries.end();++it) {
            const auto entry=it.value().toObject();if(entry.value("kind").toString()!="file")continue;
            const QString path=QDir(root).filePath(it.key());
            if(!unlinked(path,error))return false;
            const QString source=backupPath(snapshot,i,it.key()),hash=entry.value("sha256").toString();
            if(nativeMetadata) {
                if(!SecureSnapshotRestore::restoreFile(source,path,hash,entry.value("metadata").toObject(),copyVerified,error))return false;
            } else if(!copyVerified(source,path,hash,error)
                || !QFile::setPermissions(path,QFileDevice::Permissions(entry.value("permissions").toInt())))return fail(error,"Cannot restore profile file: "+path);
        }
        const auto previous=current[i].toObject().value("entries").toObject();QStringList removeDirectories;
        for(auto it=previous.begin();it!=previous.end();++it)if(!entries.contains(it.key())) {
            const QString path=QDir(root).filePath(it.key());
            if(!unlinked(path,error))return false;
            if(it.value().toObject().value("kind").toString()=="directory")removeDirectories<<path;
            else if(!SafeFileOps::safeRemoveFile(path,{root},{},{},error))return false;
        }
        std::sort(removeDirectories.begin(),removeDirectories.end(),[](const QString& a,const QString& b){return a.size()>b.size();});
        for(const auto& path:removeDirectories)if(QFileInfo::exists(path) && !QDir().rmdir(path))return fail(error,"Cannot remove new profile directory: "+path);
    }
    return true;
}
bool restoreMetadata(const QJsonArray& trees,QString* error)
{
    QMap<QString,QJsonObject> files,directories;
    for(const auto& item:trees) {
        const auto tree=item.toObject();const QString root=tree.value("root").toString();
        directories.insert(root,tree.value("metadata").toObject());
        const auto entries=tree.value("entries").toObject();
        for(auto it=entries.begin();it!=entries.end();++it) {
            const auto entry=it.value().toObject();const QString path=QDir(root).filePath(it.key());
            (entry.value("kind").toString()=="directory"?directories:files).insert(path,entry.value("metadata").toObject());
        }
    }
    for(auto it=files.begin();it!=files.end();++it)if(!unlinked(it.key(),error) || !NativeSnapshotMetadata::apply(it.key(),it.value(),error))return false;
    auto paths=directories.keys();std::sort(paths.begin(),paths.end(),[](const QString& a,const QString& b){return a.size()>b.size();});
    for(const auto& path:paths)if(!unlinked(path,error) || !NativeSnapshotMetadata::apply(path,directories.value(path),error))return false;
    return true;
}
} // namespace ProfileSnapshotInventory
