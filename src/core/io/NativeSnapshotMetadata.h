#pragma once

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QScopeGuard>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <aclapi.h>
#include <sddl.h>
#endif

namespace NativeSnapshotMetadata {
inline bool fail(QString* error,const QString& message)
{
    if(error)*error=message;
    return false;
}
#ifdef Q_OS_WIN
inline std::wstring nativePath(const QString& path)
{
    QString native=QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
    if(!native.startsWith("\\\\?\\"))native=native.startsWith("\\\\")?"\\\\?\\UNC\\"+native.mid(2):"\\\\?\\"+native;
    return native.toStdWString();
}
inline QString ticks(const FILETIME& time)
{
    return QString::number((quint64(time.dwHighDateTime)<<32)|time.dwLowDateTime);
}
inline bool timeValue(const QJsonValue& value,FILETIME* time)
{
    bool ok=false;const QString text=value.toString();const quint64 number=text.toULongLong(&ok);
    if(!value.isString() || !ok || QString::number(number)!=text)return false;
    *time=FILETIME{DWORD(number),DWORD(number>>32)};SYSTEMTIME converted{};
    return FileTimeToSystemTime(time,&converted);
}
inline constexpr DWORD supportedAttributes=FILE_ATTRIBUTE_READONLY|FILE_ATTRIBUTE_HIDDEN|FILE_ATTRIBUTE_SYSTEM
    |FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_ARCHIVE|FILE_ATTRIBUTE_NORMAL|FILE_ATTRIBUTE_NOT_CONTENT_INDEXED|FILE_ATTRIBUTE_ENCRYPTED;
#endif
inline bool capture(const QString& path,QJsonObject* result,QString* error)
{
#ifdef Q_OS_WIN
    const auto native=nativePath(path);WIN32_FILE_ATTRIBUTE_DATA info{};
    if(!GetFileAttributesExW(native.c_str(),GetFileExInfoStandard,&info) || (info.dwFileAttributes&~supportedAttributes))
        return fail(error,"Cannot capture unsupported or unreadable Windows snapshot attributes: "+path);
    PSECURITY_DESCRIPTOR descriptor=nullptr;
    const DWORD flags=OWNER_SECURITY_INFORMATION|GROUP_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION;
    const DWORD status=GetNamedSecurityInfoW(const_cast<wchar_t*>(native.c_str()),SE_FILE_OBJECT,flags,nullptr,nullptr,nullptr,nullptr,&descriptor);
    if(status!=ERROR_SUCCESS)return fail(error,"Cannot read snapshot security descriptor: "+QString::number(status));
    const auto freeDescriptor=qScopeGuard([&]{LocalFree(descriptor);});LPWSTR text=nullptr;
    if(!ConvertSecurityDescriptorToStringSecurityDescriptorW(descriptor,SDDL_REVISION_1,flags,&text,nullptr))return fail(error,"Cannot serialize snapshot security descriptor");
    const auto freeText=qScopeGuard([&]{LocalFree(text);});
    *result=QJsonObject{{"platform","windows-v1"},{"security",QString::fromWCharArray(text)},
        {"attributes",double(info.dwFileAttributes)},{"creation",ticks(info.ftCreationTime)},{"modified",ticks(info.ftLastWriteTime)}};
#else
    const QFileInfo info(path);
    if(!info.exists() || info.isSymLink())return fail(error,"Cannot capture snapshot permissions");
    *result=QJsonObject{{"platform","qt-permissions-v1"},{"permissions",int(info.permissions())},{"directory",info.isDir()}};
#endif
    return true;
}
inline bool validate(const QJsonObject& value,bool directory,QString* error)
{
#ifdef Q_OS_WIN
    FILETIME creation{},modified{};const auto attributes=value.value("attributes");const int bits=attributes.toInt(-1);
    if(value.size()!=5 || value.value("platform").toString()!="windows-v1" || !attributes.isDouble() || bits<=0 || attributes.toDouble()!=double(bits)
        || ((bits&FILE_ATTRIBUTE_NORMAL) && bits!=FILE_ATTRIBUTE_NORMAL)
        || (DWORD(bits)&~supportedAttributes) || bool(bits&FILE_ATTRIBUTE_DIRECTORY)!=directory
        || !timeValue(value.value("creation"),&creation) || !timeValue(value.value("modified"),&modified)
        || !value.value("security").isString() || value.value("security").toString().contains(QChar(0)))return fail(error,"Invalid Windows snapshot metadata");
    PSECURITY_DESCRIPTOR descriptor=nullptr;const auto text=value.value("security").toString().toStdWString();
    if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(text.c_str(),SDDL_REVISION_1,&descriptor,nullptr))return fail(error,"Invalid snapshot security descriptor");
    const auto cleanup=qScopeGuard([&]{LocalFree(descriptor);});PSID owner=nullptr,group=nullptr;BOOL defaulted=false,present=false;PACL acl=nullptr;
    if(!IsValidSecurityDescriptor(descriptor) || !GetSecurityDescriptorOwner(descriptor,&owner,&defaulted) || !owner
        || !GetSecurityDescriptorGroup(descriptor,&group,&defaulted) || !group
        || !GetSecurityDescriptorDacl(descriptor,&present,&acl,&defaulted) || !present)return fail(error,"Incomplete snapshot security descriptor");
    LPWSTR canonical=nullptr;
    if(!ConvertSecurityDescriptorToStringSecurityDescriptorW(descriptor,SDDL_REVISION_1,
        OWNER_SECURITY_INFORMATION|GROUP_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION,&canonical,nullptr))return fail(error,"Cannot validate snapshot security descriptor");
    const auto freeCanonical=qScopeGuard([&]{LocalFree(canonical);});
    if(QString::fromWCharArray(canonical)!=value.value("security").toString())return fail(error,"Noncanonical snapshot security descriptor");
#else
    if(value.size()!=3 || value.value("platform").toString()!="qt-permissions-v1" || !value.value("permissions").isDouble()
        || value.value("permissions").toInt(-1)<0 || !value.value("directory").isBool() || value.value("directory").toBool()!=directory)return fail(error,"Invalid snapshot permissions");
#endif
    return true;
}
inline bool apply(const QString& path,const QJsonObject& value,QString* error)
{
    const bool directory=QFileInfo(path).isDir();if(!validate(value,directory,error))return false;
#ifdef Q_OS_WIN
    const auto native=nativePath(path);const DWORD desired=DWORD(value.value("attributes").toInt());
    DWORD actual=GetFileAttributesW(native.c_str());if(actual==INVALID_FILE_ATTRIBUTES || (actual&FILE_ATTRIBUTE_REPARSE_POINT))return fail(error,"Unsafe snapshot metadata destination");
    if(bool(actual&FILE_ATTRIBUTE_ENCRYPTED)!=bool(desired&FILE_ATTRIBUTE_ENCRYPTED)) {
        const bool ok=(desired&FILE_ATTRIBUTE_ENCRYPTED)?EncryptFileW(native.c_str()):DecryptFileW(native.c_str(),0);
        if(!ok)return fail(error,"Cannot restore snapshot encryption attribute: "+QString::number(GetLastError()));
    }
    PSECURITY_DESCRIPTOR descriptor=nullptr;const auto text=value.value("security").toString().toStdWString();
    if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(text.c_str(),SDDL_REVISION_1,&descriptor,nullptr))return fail(error,"Cannot parse snapshot security descriptor");
    const auto cleanup=qScopeGuard([&]{LocalFree(descriptor);});PSID owner=nullptr,group=nullptr;PACL acl=nullptr;BOOL flag=false,present=false;
    SECURITY_DESCRIPTOR_CONTROL control{};DWORD revision=0;
    if(!GetSecurityDescriptorOwner(descriptor,&owner,&flag) || !GetSecurityDescriptorGroup(descriptor,&group,&flag)
        || !GetSecurityDescriptorDacl(descriptor,&present,&acl,&flag) || !GetSecurityDescriptorControl(descriptor,&control,&revision))return fail(error,"Cannot read snapshot security metadata");
    // Set times while the restoring principal still has access; ACL is applied last.
    HANDLE handle=CreateFileW(native.c_str(),FILE_WRITE_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(handle==INVALID_HANDLE_VALUE)return fail(error,"Cannot open snapshot metadata destination");
    FILETIME creation{},modified{};timeValue(value.value("creation"),&creation);timeValue(value.value("modified"),&modified);
    const bool timesOk=SetFileTime(handle,&creation,nullptr,&modified);CloseHandle(handle);
    if(!timesOk)return fail(error,"Cannot restore snapshot file times");
    const DWORD mutableBits=desired&~(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_ENCRYPTED);
    if(!SetFileAttributesW(native.c_str(),mutableBits?mutableBits:FILE_ATTRIBUTE_NORMAL))return fail(error,"Cannot restore snapshot file attributes");
    const DWORD security=OWNER_SECURITY_INFORMATION|GROUP_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION
        |((control&SE_DACL_PROTECTED)?PROTECTED_DACL_SECURITY_INFORMATION:UNPROTECTED_DACL_SECURITY_INFORMATION);
    // Replay the serialized descriptor control flags as well as its ACL entries.
    // SetNamedSecurityInfo recalculates auto-inheritance and changes this contract.
    if(!SetFileSecurityW(native.c_str(),security,descriptor))return fail(error,"Cannot restore snapshot security descriptor: "+QString::number(GetLastError()));
#else
    if(!QFile::setPermissions(path,QFileDevice::Permissions(value.value("permissions").toInt())))return fail(error,"Cannot restore snapshot permissions");
#endif
    QJsonObject actualMetadata;if(!capture(path,&actualMetadata,error))return false;
    if(actualMetadata==value)return true;
    QStringList fields;for(auto it=value.begin();it!=value.end();++it)if(actualMetadata.value(it.key())!=it.value())fields<<it.key();
    return fail(error,"Restored snapshot metadata differs ("+fields.join(',')+"): "+path);
}
inline bool protectBackupDirectory(const QString& path,QString* error)
{
#ifdef Q_OS_WIN
    const auto native=nativePath(path);HANDLE token=nullptr;
    if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token))return fail(error,"Cannot identify snapshot owner");
    const auto closeToken=qScopeGuard([&]{CloseHandle(token);});DWORD size=0;GetTokenInformation(token,TokenUser,nullptr,0,&size);
    QByteArray buffer(int(size),Qt::Uninitialized);
    if(!size || !GetTokenInformation(token,TokenUser,buffer.data(),size,&size))return fail(error,"Cannot read snapshot owner");
    LPWSTR sid=nullptr;if(!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buffer.data())->User.Sid,&sid))return fail(error,"Cannot serialize snapshot owner");
    const QString sddl="D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;"+QString::fromWCharArray(sid)+")";LocalFree(sid);
    PSECURITY_DESCRIPTOR descriptor=nullptr;const auto text=sddl.toStdWString();
    if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(text.c_str(),SDDL_REVISION_1,&descriptor,nullptr))return fail(error,"Cannot create private snapshot descriptor");
    const auto cleanup=qScopeGuard([&]{LocalFree(descriptor);});
    if(!SetFileSecurityW(native.c_str(),DACL_SECURITY_INFORMATION|PROTECTED_DACL_SECURITY_INFORMATION,descriptor)
        || !EncryptFileW(native.c_str()))return fail(error,"Cannot protect and encrypt snapshot storage before copying");
#else
    if(!QFile::setPermissions(path,QFileDevice::ReadOwner|QFileDevice::WriteOwner|QFileDevice::ExeOwner))return fail(error,"Cannot protect snapshot storage");
#endif
    return true;
}
} // namespace NativeSnapshotMetadata
