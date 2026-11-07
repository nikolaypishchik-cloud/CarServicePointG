#pragma once
#include "core/io/NativeSnapshotMetadata.h"
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QMap>
#include <QRegularExpression>

// Custody of prior sealed generations. This does not merge or route archived rows.
namespace MigrationRetainedStores {
inline bool digest(const QString& path,QString* hash,QString* error) {
    const QFileInfo info(path);QFile file(path);QCryptographicHash h(QCryptographicHash::Sha256);
    if(info.isSymLink() || !info.isFile() || !file.open(QIODevice::ReadOnly)
        || !h.addData(&file) || file.error()!=QFileDevice::NoError)
        return NativeSnapshotMetadata::fail(error,"Cannot verify retained store: "+path);
    *hash=QString::fromLatin1(h.result().toHex());return true;
}
inline bool retain(const QString& root,const QMap<QString,QString>& stores,QString* generation,QString* error) {
    QJsonArray entries;
    for(auto it=stores.cbegin();it!=stores.cend();++it) {
        if(!QRegularExpression("^[a-z][a-z0-9_-]{0,63}$").match(it.key()).hasMatch())
            return NativeSnapshotMetadata::fail(error,"Unknown retained store role");
        const QFileInfo info(it.value());
        if(info.isSymLink())return NativeSnapshotMetadata::fail(error,"Symlink retained store");
        QString hash;
        if(info.exists() && !digest(it.value(),&hash,error))return false;
        entries.append(QJsonObject{{"role",it.key()},{"present",info.exists()},{"sha256",hash}});
    }
    if(stores.isEmpty())return NativeSnapshotMetadata::fail(error,"Incomplete retained store inventory");
    const auto manifest=QJsonDocument(QJsonObject{{"version",1},{"stores",entries}}).toJson(QJsonDocument::Compact);
    const auto id=QString::fromLatin1(QCryptographicHash::hash(manifest,QCryptographicHash::Sha256).toHex());
    const QFileInfo rootInfo(root);
    if(rootInfo.isSymLink() || (rootInfo.exists() && !rootInfo.isDir()) || !QDir().mkpath(root))
        return NativeSnapshotMetadata::fail(error,"Invalid retained generation root");
    if(!NativeSnapshotMetadata::protectBackupDirectory(root,error))return false;
    const auto destination=QDir(root).filePath(id);
    const auto verify=[&](const QString& directory)->bool {
        if(QFileInfo(directory).isSymLink())return NativeSnapshotMetadata::fail(error,"Symlink retained generation");
        QFile saved(QDir(directory).filePath("manifest.json"));
        if(QFileInfo(saved.fileName()).isSymLink() || !saved.open(QIODevice::ReadOnly) || saved.readAll()!=manifest)
            return NativeSnapshotMetadata::fail(error,"Retained manifest differs");
        for(const auto& entry:entries) {
            const auto obj=entry.toObject();const auto path=QDir(directory).filePath(obj["role"].toString()+".enc");
            if(!obj["present"].toBool()) {
                if(QFileInfo::exists(path) || QFileInfo(path).isSymLink())return NativeSnapshotMetadata::fail(error,"Unexpected retained store");
                continue;
            }
            QString actual;
            if(!digest(path,&actual,error) || actual!=obj["sha256"].toString())
                return NativeSnapshotMetadata::fail(error,"Retained store digest differs");
        }
        return true;
    };
    if(QFileInfo::exists(destination)) {
        if(!verify(destination))return false;
        if(generation)*generation=destination;return true;
    }
    QTemporaryDir staging(QDir(root).filePath(".pending-XXXXXX"));
    if(!staging.isValid() || !NativeSnapshotMetadata::protectBackupDirectory(staging.path(),error))return false;
    for(const auto& entry:entries) {
        const auto obj=entry.toObject();if(!obj["present"].toBool())continue;
        const auto role=obj["role"].toString();
        if(!QFile::copy(stores.value(role),staging.filePath(role+".enc")))
            return NativeSnapshotMetadata::fail(error,"Cannot retain prior sealed store");
    }
    QSaveFile file(staging.filePath("manifest.json"));
    if(!file.open(QIODevice::WriteOnly) || file.write(manifest)!=manifest.size() || !file.commit() || !verify(staging.path()))
        return NativeSnapshotMetadata::fail(error,"Cannot commit verified retained generation");
    if(!QDir().rename(staging.path(),destination))return NativeSnapshotMetadata::fail(error,"Cannot publish retained generation");
    staging.setAutoRemove(false);
    if(!verify(destination))return false;
    if(generation)*generation=destination;return true;
}
}
