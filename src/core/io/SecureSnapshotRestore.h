#pragma once

#include "core/io/NativeSnapshotMetadata.h"
#include "core/io/SafeFileOps.h"
#include <QUuid>
#include <QCryptographicHash>
#include <cstdio>

namespace SecureSnapshotRestore {
inline bool unlinked(const QString& path,QString* error)
{
    QString cursor=QFileInfo(path).absoluteFilePath();
    for(;;) {
        const QFileInfo info(cursor);
        if(info.isSymLink() || info.isJunction())return NativeSnapshotMetadata::fail(error,"Linked restore staging path");
        const QString parent=info.absolutePath();if(parent==cursor)break;cursor=parent;
    }
    return true;
}
inline bool verifyContents(const QString& path,const QString& expected,QString* error)
{
    QFile file(path);QCryptographicHash hash(QCryptographicHash::Sha256);
    if(!file.open(QIODevice::ReadOnly) || !hash.addData(&file) || file.error()!=QFileDevice::NoError)
        return NativeSnapshotMetadata::fail(error,"Cannot read back staged restore content");
    return QString::fromLatin1(hash.result().toHex())==expected
        || NativeSnapshotMetadata::fail(error,"Staged restore content differs from expected digest");
}
inline bool promote(const QString& staged,const QString& target,QString* error)
{
    if(!unlinked(staged,error) || !unlinked(target,error))return false;
#ifdef Q_OS_WIN
    const auto from=NativeSnapshotMetadata::nativePath(staged),to=NativeSnapshotMetadata::nativePath(target);
    const DWORD previous=GetFileAttributesW(to.c_str());const DWORD absentError=GetLastError();
    if(previous==INVALID_FILE_ATTRIBUTES && absentError!=ERROR_FILE_NOT_FOUND && absentError!=ERROR_PATH_NOT_FOUND)
        return NativeSnapshotMetadata::fail(error,"Cannot inspect restore destination before promotion");
    if(previous!=INVALID_FILE_ATTRIBUTES && (previous&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)))
        return NativeSnapshotMetadata::fail(error,"Invalid restore promotion destination");
    const bool readonly=previous!=INVALID_FILE_ATTRIBUTES && (previous&FILE_ATTRIBUTE_READONLY);
    if(readonly && !SetFileAttributesW(to.c_str(),previous&~FILE_ATTRIBUTE_READONLY))return NativeSnapshotMetadata::fail(error,"Cannot prepare readonly restore destination");
    if(!MoveFileExW(from.c_str(),to.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) {
        const DWORD failure=GetLastError();
        if(readonly && !SetFileAttributesW(to.c_str(),previous))return NativeSnapshotMetadata::fail(error,"Restore promotion and destination attribute rollback failed");
        return NativeSnapshotMetadata::fail(error,"Cannot promote verified restore file: "+QString::number(failure));
    }
#else
    if(std::rename(QFile::encodeName(staged).constData(),QFile::encodeName(target).constData())!=0)
        return NativeSnapshotMetadata::fail(error,"Cannot promote verified restore file");
#endif
    return true;
}
// Copier must verify the expected content digest before returning success.
// Keeping it explicit lets callers reuse their existing streaming hash writer.
template<typename Copier>
bool restoreFile(const QString& source,const QString& target,const QString& hash,const QJsonObject& metadata,Copier copy,QString* error)
{
    if(!NativeSnapshotMetadata::validate(metadata,false,error) || !unlinked(target,error))return false;
    const QString parent=QFileInfo(target).absolutePath();
    const QString stage=QDir(parent).filePath(".migration_restore_"+QUuid::createUuid().toString(QUuid::WithoutBraces));
    if(!QDir(parent).exists() || !QDir().mkdir(stage))return NativeSnapshotMetadata::fail(error,"Cannot create same-volume restore staging directory");
    const QString payload=QDir(stage).filePath("payload");
    const bool restored=[&]{
        if(!NativeSnapshotMetadata::protectBackupDirectory(stage,error) || !unlinked(stage,error))return false;
        if(!copy(source,payload,hash,error) || !NativeSnapshotMetadata::apply(payload,metadata,error)
            || !verifyContents(payload,hash,error))return false;
        if(!promote(payload,target,error))return false;
        QJsonObject actual;
        return NativeSnapshotMetadata::capture(target,&actual,error)
            && (actual==metadata || NativeSnapshotMetadata::fail(error,"Promoted restore metadata differs from manifest"));
    }();
    // This is the freshly created owned directory and its single known payload,
    // never recursive deletion of a computed external directory.
    bool cleaned=unlinked(stage,nullptr) && unlinked(payload,nullptr);
#ifdef Q_OS_WIN
    if(cleaned && QFileInfo::exists(payload)) {
        const auto native=NativeSnapshotMetadata::nativePath(payload);const DWORD attributes=GetFileAttributesW(native.c_str());
        if(attributes==INVALID_FILE_ATTRIBUTES || ((attributes&FILE_ATTRIBUTE_READONLY)
            && !SetFileAttributesW(native.c_str(),attributes&~FILE_ATTRIBUTE_READONLY)))cleaned=false;
    }
#endif
    if(cleaned)cleaned=SafeFileOps::safeRemoveFile(payload,{stage},{},{},nullptr) && QDir().rmdir(stage);
    if(!cleaned) {
        if(error)*error+=(error->isEmpty()?QString():QStringLiteral("; "))+QStringLiteral("Cannot clean owned restore staging directory");
        return false;
    }
    return restored;
}
} // namespace SecureSnapshotRestore
