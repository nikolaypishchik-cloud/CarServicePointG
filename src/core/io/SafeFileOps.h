#pragma once

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QString>
#include <QStringList>

namespace SafeFileOps {

inline QString cleanPath(const QString& path)
{
    const QString trimmed = path.trimmed();
    if (trimmed.isEmpty()) return QString();
    return QDir::cleanPath(QDir::fromNativeSeparators(trimmed));
}

inline QStringList protectedDeletionRoots()
{
    QStringList roots;
    roots << QDir::rootPath();
    roots << QDir::homePath();
    roots << QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    roots << QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    roots << QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    roots << QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    roots << QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QStringList cleaned;
    for (const QString& root : roots) {
        const QString c = cleanPath(root);
        if (!c.isEmpty()) cleaned << c;
    }
    cleaned.removeDuplicates();
    return cleaned;
}

inline bool isSamePath(const QString& a, const QString& b)
{
#ifdef Q_OS_WIN
    return cleanPath(a).compare(cleanPath(b), Qt::CaseInsensitive) == 0;
#else
    return cleanPath(a) == cleanPath(b);
#endif
}

inline bool isInsideRoot(const QString& path, const QString& root)
{
    const QString p = cleanPath(path);
    const QString r = cleanPath(root);
    if (p.isEmpty() || r.isEmpty()) return false;
#ifdef Q_OS_WIN
    return p.compare(r, Qt::CaseInsensitive) == 0 || p.startsWith(r + QLatin1Char('/'), Qt::CaseInsensitive);
#else
    return p == r || p.startsWith(r + QLatin1Char('/'));
#endif
}

inline bool isInsideAnyRoot(const QString& path, const QStringList& roots)
{
    for (const QString& root : roots) {
        if (isInsideRoot(path, root)) return true;
    }
    return false;
}

inline bool isProtectedDeletionTarget(const QString& path, QString* reason = nullptr)
{
    const QString p = cleanPath(path);
    if (p.isEmpty()) {
        if (reason) *reason = QStringLiteral("empty path");
        return true;
    }
    const QFileInfo fi(p);
    const QString abs = cleanPath(fi.absoluteFilePath());
    for (const QString& root : protectedDeletionRoots()) {
        if (isSamePath(abs, root)) {
            if (reason) *reason = QStringLiteral("protected root: %1").arg(QDir::toNativeSeparators(root));
            return true;
        }
    }
    if (QFileInfo(abs).absolutePath().isEmpty()) {
        if (reason) *reason = QStringLiteral("invalid absolute parent");
        return true;
    }
    return false;
}

inline bool nameAllowed(const QString& path,
                        const QStringList& expectedPrefixes = {},
                        const QStringList& expectedSuffixes = {})
{
    const QString name = QFileInfo(path).fileName();
    if (name.isEmpty()) return false;
    if (!expectedPrefixes.isEmpty()) {
        bool ok = false;
        for (const QString& prefix : expectedPrefixes) {
            if (!prefix.isEmpty() && name.startsWith(prefix, Qt::CaseInsensitive)) {
                ok = true;
                break;
            }
        }
        if (!ok) return false;
    }
    if (!expectedSuffixes.isEmpty()) {
        bool ok = false;
        for (const QString& suffix : expectedSuffixes) {
            if (!suffix.isEmpty() && name.endsWith(suffix, Qt::CaseInsensitive)) {
                ok = true;
                break;
            }
        }
        if (!ok) return false;
    }
    return true;
}

inline bool safeRemoveFile(const QString& path,
                           const QStringList& allowedRoots,
                           const QStringList& expectedPrefixes = {},
                           const QStringList& expectedSuffixes = {},
                           QString* errorText = nullptr)
{
    QString reason;
    if (isProtectedDeletionTarget(path, &reason)) {
        if (errorText) *errorText = QStringLiteral("Blocked unsafe file deletion: %1 (%2)").arg(QDir::toNativeSeparators(path), reason);
        return false;
    }
    if (!allowedRoots.isEmpty() && !isInsideAnyRoot(path, allowedRoots)) {
        if (errorText) *errorText = QStringLiteral("Blocked file deletion outside allowed roots: %1").arg(QDir::toNativeSeparators(path));
        return false;
    }
    if (!nameAllowed(path, expectedPrefixes, expectedSuffixes)) {
        if (errorText) *errorText = QStringLiteral("Blocked file deletion with unexpected name: %1").arg(QDir::toNativeSeparators(path));
        return false;
    }
    if (!QFileInfo::exists(path)) return true;
    if (!QFile::remove(path)) {
        if (errorText) *errorText = QStringLiteral("Failed to delete file: %1").arg(QDir::toNativeSeparators(path));
        return false;
    }
    return true;
}

inline bool safeRemoveRecursively(const QString& path,
                                  const QStringList& allowedRoots,
                                  const QStringList& expectedPrefixes = {},
                                  QString* errorText = nullptr)
{
    QString reason;
    if (isProtectedDeletionTarget(path, &reason)) {
        if (errorText) *errorText = QStringLiteral("Blocked unsafe directory deletion: %1 (%2)").arg(QDir::toNativeSeparators(path), reason);
        return false;
    }
    if (!allowedRoots.isEmpty() && !isInsideAnyRoot(path, allowedRoots)) {
        if (errorText) *errorText = QStringLiteral("Blocked directory deletion outside allowed roots: %1").arg(QDir::toNativeSeparators(path));
        return false;
    }
    if (!expectedPrefixes.isEmpty() && !nameAllowed(path, expectedPrefixes, {})) {
        if (errorText) *errorText = QStringLiteral("Blocked directory deletion with unexpected name: %1").arg(QDir::toNativeSeparators(path));
        return false;
    }
    QDir dir(path);
    if (!dir.exists()) return true;
    if (!dir.removeRecursively()) {
        if (errorText) *errorText = QStringLiteral("Failed to delete directory: %1").arg(QDir::toNativeSeparators(path));
        return false;
    }
    return true;
}

} // namespace SafeFileOps
