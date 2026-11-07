#include "core/util/SoundSupport.h"

#include "infrastructure/db/sqlite/Db.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QVariant>
#include <QDebug>
#include <QUrl>
#include <QMutex>
#include <QMutexLocker>

#if CS_HAS_MULTIMEDIA
#include <QSoundEffect>
#endif

#ifdef Q_OS_WIN
#include <windows.h>
#include <mmsystem.h>
#include <string>
#endif

namespace SoundSupport {
namespace {

struct SoundSettingsCache {
    bool loaded = false;
    bool enabled = true;
    int volumePct = 85;
};

QMutex& soundSettingsMutex()
{
    static QMutex mutex;
    return mutex;
}

SoundSettingsCache& soundSettingsCache()
{
    static SoundSettingsCache cache;
    return cache;
}

void loadSoundSettingsCacheLocked(bool defaultEnabled, int defaultVolume)
{
    SoundSettingsCache& cache = soundSettingsCache();
    if (cache.loaded) return;

    cache.enabled = defaultEnabled;
    cache.volumePct = qBound(0, defaultVolume, 100);

    const QVariant modern = Database::getSetting(QStringLiteral("settings.sounds.enabled"), QVariant());
    if (modern.isValid() && !modern.isNull()) {
        cache.enabled = modern.toBool();
    } else {
        const QVariant legacy = Database::getSetting(QStringLiteral("ui/sounds_enabled"), QVariant());
        if (legacy.isValid() && !legacy.isNull()) {
            cache.enabled = legacy.toBool();
            Database::setSetting(QStringLiteral("settings.sounds.enabled"), legacy);
        } else {
            Database::setSetting(QStringLiteral("settings.sounds.enabled"), defaultEnabled);
        }
    }

    const QVariant volume = Database::getSetting(QStringLiteral("settings.sounds.volume"), QVariant());
    if (volume.isValid() && !volume.isNull()) {
        cache.volumePct = qBound(0, volume.toInt(), 100);
    }
    cache.loaded = true;
}

QStringList assetCandidates(const QString& fileName)
{
    const QString appDir = QCoreApplication::applicationDirPath();
    return {
        QStringLiteral(":/assets/") + fileName,
        appDir + QStringLiteral("/../resources/assets/") + fileName,
        appDir + QStringLiteral("/resources/assets/") + fileName,
        appDir + QStringLiteral("/assets/") + fileName
    };
}
}

bool soundsEnabled(bool defaultValue)
{
    QMutexLocker locker(&soundSettingsMutex());
    loadSoundSettingsCacheLocked(defaultValue, 85);
    return soundSettingsCache().enabled;
}

int soundsVolumePct(int defaultValue)
{
    QMutexLocker locker(&soundSettingsMutex());
    loadSoundSettingsCacheLocked(true, defaultValue);
    return qBound(0, soundSettingsCache().volumePct, 100);
}

void invalidateSettingsCache()
{
    QMutexLocker locker(&soundSettingsMutex());
    soundSettingsCache().loaded = false;
}

QByteArray loadAssetBytes(const QString& fileName, QString* loadedFrom, const QString& categoryLabel)
{
    const QString label = categoryLabel.trimmed().isEmpty() ? QStringLiteral("Sound") : categoryLabel.trimmed();
    for (const QString& candidate : assetCandidates(fileName)) {
        QFile src(candidate);
        if (!src.open(QIODevice::ReadOnly)) continue;
        const QByteArray bytes = src.readAll();
        src.close();
        if (bytes.isEmpty()) {
            qWarning() << label << "resource is empty:" << candidate;
            continue;
        }
        if (loadedFrom) *loadedFrom = candidate;
        return bytes;
    }

    qWarning() << label << "resource open failed:" << (QStringLiteral(":/assets/") + fileName)
               << "and filesystem fallbacks near" << QCoreApplication::applicationDirPath();
    return QByteArray();
}

QString ensureTempWavFile(const QByteArray& bytes,
                          const QString& filePrefix,
                          const QString& loadedFrom,
                          const QString& logLabel)
{
    if (bytes.isEmpty()) return QString();

    const QString dir = QDir::tempPath() + QStringLiteral("/CarServicePointG");
    if (!QDir().mkpath(dir)) {
        qWarning() << "Temp sound dir create failed:" << dir;
        return QString();
    }

    const QString hash = QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha1).toHex().left(12));
    const QString path = dir + QLatin1Char('/') + filePrefix + QLatin1Char('_') + hash + QStringLiteral(".wav");

    bool needWrite = true;
    QFileInfo fi(path);
    if (fi.exists() && fi.size() == bytes.size()) {
        QFile existing(path);
        if (existing.open(QIODevice::ReadOnly)) {
            needWrite = (existing.readAll() != bytes);
            existing.close();
        }
    }

    if (needWrite) {
        QFile dst(path);
        if (!dst.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            qWarning() << "Temp sound write failed:" << path;
            return QString();
        }
        const qint64 written = dst.write(bytes);
        dst.close();
        if (written != bytes.size()) {
            qWarning() << "Temp sound write incomplete:" << path << written << "/" << bytes.size();
            return QString();
        }
    }

    if (!logLabel.trimmed().isEmpty()) {
        qInfo() << logLabel << loadedFrom;
    }
    return path;
}

#if CS_HAS_MULTIMEDIA
void setupEffect(QSoundEffect* effect, const QString& fileName, int loopCount, float defaultVolume, const QString& categoryLabel)
{
    if (!effect) return;

    QString selectedSource;
    for (const QString& candidate : assetCandidates(fileName)) {
        if (candidate.startsWith(QStringLiteral(":/"))) {
            QFile src(candidate);
            if (!src.open(QIODevice::ReadOnly)) continue;
            const bool ok = src.size() > 0;
            src.close();
            if (!ok) continue;
            effect->setSource(QUrl(QStringLiteral("qrc") + candidate));
            selectedSource = candidate;
            break;
        }
        if (QFile::exists(candidate)) {
            effect->setSource(QUrl::fromLocalFile(QFileInfo(candidate).absoluteFilePath()));
            selectedSource = candidate;
            break;
        }
    }

    effect->setLoopCount(loopCount);
    effect->setVolume(defaultVolume);

    if (selectedSource.isEmpty()) {
        const QString label = categoryLabel.trimmed().isEmpty() ? QStringLiteral("Sound effect") : categoryLabel.trimmed();
        qWarning() << label << "source not found for:" << fileName
                   << "near" << QCoreApplication::applicationDirPath();
    }
}
#endif

bool playNativeFile(const QString& path, bool asyncPlayback, bool loop)
{
#ifdef Q_OS_WIN
    if (path.isEmpty()) return false;
    const QString nativePathQt = QDir::toNativeSeparators(path);
    const std::wstring nativePath(reinterpret_cast<const wchar_t*>(nativePathQt.utf16()),
                                  static_cast<std::size_t>(nativePathQt.size()));
    DWORD flags = SND_FILENAME | SND_NODEFAULT;
    if (loop) flags |= SND_LOOP;
    flags |= asyncPlayback ? SND_ASYNC : SND_SYNC;
    const bool ok = PlaySoundW(nativePath.c_str(), nullptr, flags);
    if (!ok) {
        qWarning() << "PlaySoundW failed for sound:" << QString::fromWCharArray(nativePath.c_str());
    }
    return ok;
#else
    Q_UNUSED(path);
    Q_UNUSED(asyncPlayback);
    Q_UNUSED(loop);
    return false;
#endif
}

void stopNativePlayback()
{
#ifdef Q_OS_WIN
    PlaySoundW(nullptr, nullptr, 0);
#endif
}

} // namespace SoundSupport
