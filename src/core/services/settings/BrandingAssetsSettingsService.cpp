#include "core/services/settings/BrandingAssetsSettingsService.h"

#include "infrastructure/db/sqlite/Db.h"
#include "runtime/AppRuntime.h"
#include "core/io/SafeFileOps.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QFile>
#include <QUuid>

namespace BrandingAssetsSettingsService {
namespace {
QStringList sanitize(QStringList logos)
{
    QStringList out;
    QSet<QString> seen;
    for (QString path : logos) {
        path = QDir::fromNativeSeparators(path.trimmed());
        if (path.isEmpty()) continue;
        const QFileInfo fi(path);
        if (!fi.exists() || !fi.isFile()) continue;
        const QString canon = fi.canonicalFilePath().isEmpty() ? fi.absoluteFilePath() : fi.canonicalFilePath();
        if (canon.isEmpty() || seen.contains(canon)) continue;
        seen.insert(canon);
        out << canon;
    }
    return out;
}

QString currentNormalizedJson()
{
    QStringList logos;
    const QString raw = Database::getSetting(QStringLiteral("settings.branding.logos"), QString()).toString().trimmed();
    if (!raw.isEmpty()) {
        const QJsonDocument doc = QJsonDocument::fromJson(raw.toUtf8());
        if (doc.isArray()) {
            for (const QJsonValue& v : doc.array()) {
                if (v.isString()) logos << v.toString();
            }
        }
    }
    const QStringList cleaned = sanitize(logos);
    QJsonArray arr;
    for (const QString& path : cleaned) arr.append(path);
    return QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}
}


QStringList sanitizeLogoList(QStringList logos)
{
    return sanitize(std::move(logos));
}

QStringList loadLogoListWithActive(const QString& activeLogoPath)
{
    QStringList logos = loadLogoList();
    const QString active = activeLogoPath.trimmed();
    if (!active.isEmpty()) logos.prepend(active);
    logos = sanitize(logos);

    if (logos.isEmpty() && !active.isEmpty()) {
        const QFileInfo fi(active);
        if (fi.exists() && fi.isFile()) logos << fi.absoluteFilePath();
    }
    return logos;
}

QString copyLogoToDataDir(const QString& sourcePath)
{
    if (sourcePath.trimmed().isEmpty()) return QString();
    const QFileInfo srcInfo(sourcePath);
    if (!srcInfo.exists() || !srcInfo.isFile()) return QString();

    QString ext = srcInfo.suffix().trimmed().toLower();
    if (ext.isEmpty()) ext = QStringLiteral("png");

    const QString dstDir = QDir(AppRuntime::dataDir()).filePath(QStringLiteral("branding"));
    QDir().mkpath(dstDir);
    const QString dst = QDir(dstDir).filePath(QStringLiteral("logo_%1.%2")
                                              .arg(QUuid::createUuid().toString(QUuid::WithoutBraces), ext));
    if (!QFile::copy(srcInfo.absoluteFilePath(), dst)) return QString();
    return QFileInfo(dst).absoluteFilePath();
}


bool removeManagedLogoFile(const QString& path)
{
    const QFileInfo fi(path.trimmed());
    if (!fi.exists() || !fi.isFile()) return false;

    const QString brandingDir = QDir(AppRuntime::dataDir()).filePath(QStringLiteral("branding"));
    const QString canonDir = QFileInfo(brandingDir).canonicalFilePath().isEmpty()
        ? QFileInfo(brandingDir).absoluteFilePath()
        : QFileInfo(brandingDir).canonicalFilePath();
    const QString canonPath = fi.canonicalFilePath().isEmpty() ? fi.absoluteFilePath() : fi.canonicalFilePath();
    if (canonPath.isEmpty() || canonDir.isEmpty()) return false;
    if (!canonPath.startsWith(canonDir + QDir::separator())) return false;
    return SafeFileOps::safeRemoveFile(canonPath, {brandingDir});
}

QStringList loadLogoList()
{
    QStringList logos;
    const QString raw = Database::getSetting(QStringLiteral("settings.branding.logos"), QString()).toString().trimmed();
    if (!raw.isEmpty()) {
        const QJsonDocument doc = QJsonDocument::fromJson(raw.toUtf8());
        if (doc.isArray()) {
            for (const QJsonValue& v : doc.array()) {
                if (v.isString()) logos << v.toString();
            }
        }
    }
    return sanitize(logos);
}

bool saveLogoList(const QStringList& logos, bool* changed)
{
    const QStringList cleaned = sanitize(logos);
    QJsonArray arr;
    for (const QString& path : cleaned) arr.append(path);
    const QString newJson = QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
    const bool hasChanges = currentNormalizedJson() != newJson;
    if (changed) *changed = hasChanges;
    Database::setSetting(QStringLiteral("settings.branding.logos"), newJson);
    return true;
}

} // namespace BrandingAssetsSettingsService
