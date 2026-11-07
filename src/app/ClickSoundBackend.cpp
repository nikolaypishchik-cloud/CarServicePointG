#include "app/ClickSoundBackend.h"

#include "core/util/SoundSupport.h"

#include <QDateTime>
#include <QTableView>
#include <QWidget>

#if CS_HAS_MULTIMEDIA
#include <QSoundEffect>
#endif

namespace {

QByteArray loadClickSoundBytes(QString* loadedFrom = nullptr)
{
    return SoundSupport::loadAssetBytes(QStringLiteral("windows_mouse_click.wav"),
                                        loadedFrom,
                                        QStringLiteral("Click sound"));
}

QByteArray loadTableClickSoundBytes(QString* loadedFrom = nullptr)
{
    return SoundSupport::loadAssetBytes(QStringLiteral("tables_click_global.wav"),
                                        loadedFrom,
                                        QStringLiteral("Table click sound"));
}

QByteArray loadMinimizeSoundBytes(QString* loadedFrom = nullptr)
{
    return SoundSupport::loadAssetBytes(QStringLiteral("window_minimize_custom.wav"),
                                        loadedFrom,
                                        QStringLiteral("Minimize sound"));
}

QByteArray loadRestoreSoundBytes(QString* loadedFrom = nullptr)
{
    return SoundSupport::loadAssetBytes(QStringLiteral("window_restore_custom.wav"),
                                        loadedFrom,
                                        QStringLiteral("Restore sound"));
}

QByteArray loadStartupSoundBytes(QString* loadedFrom = nullptr)
{
    return SoundSupport::loadAssetBytes(QStringLiteral("window_startup_custom.wav"),
                                        loadedFrom,
                                        QStringLiteral("Startup sound"));
}

QByteArray loadCloseSoundBytes(QString* loadedFrom = nullptr)
{
    return SoundSupport::loadAssetBytes(QStringLiteral("window_close_custom.wav"),
                                        loadedFrom,
                                        QStringLiteral("Close sound"));
}

#if CS_HAS_MULTIMEDIA
QSoundEffect* clickEffectInstance()
{
    static QSoundEffect* effect = []() {
        auto* e = new QSoundEffect();
        SoundSupport::setupEffect(e,
                                  QStringLiteral("windows_mouse_click.wav"),
                                  1,
                                  0.85f,
                                  QStringLiteral("Click sound"));
        return e;
    }();
    return effect;
}

QSoundEffect* tableClickEffectInstance()
{
    static QSoundEffect* effect = []() {
        auto* e = new QSoundEffect();
        SoundSupport::setupEffect(e,
                                  QStringLiteral("tables_click_global.wav"),
                                  1,
                                  0.85f,
                                  QStringLiteral("Table click sound"));
        return e;
    }();
    return effect;
}

QSoundEffect* minimizeEffectInstance()
{
    static QSoundEffect* effect = []() {
        auto* e = new QSoundEffect();
        SoundSupport::setupEffect(e,
                                  QStringLiteral("window_minimize_custom.wav"),
                                  1,
                                  0.85f,
                                  QStringLiteral("Minimize sound"));
        return e;
    }();
    return effect;
}

QSoundEffect* restoreEffectInstance()
{
    static QSoundEffect* effect = []() {
        auto* e = new QSoundEffect();
        SoundSupport::setupEffect(e,
                                  QStringLiteral("window_restore_custom.wav"),
                                  1,
                                  0.85f,
                                  QStringLiteral("Restore sound"));
        return e;
    }();
    return effect;
}

QSoundEffect* startupEffectInstance()
{
    static QSoundEffect* effect = []() {
        auto* e = new QSoundEffect();
        SoundSupport::setupEffect(e,
                                  QStringLiteral("window_startup_custom.wav"),
                                  1,
                                  0.85f,
                                  QStringLiteral("Startup sound"));
        return e;
    }();
    return effect;
}

QSoundEffect* closeEffectInstance()
{
    static QSoundEffect* effect = []() {
        auto* e = new QSoundEffect();
        SoundSupport::setupEffect(e,
                                  QStringLiteral("window_close_custom.wav"),
                                  1,
                                  0.85f,
                                  QStringLiteral("Close sound"));
        return e;
    }();
    return effect;
}

bool playEffect(QSoundEffect* effect, int volumePct)
{
    effect->setMuted(false);
    effect->setVolume(qBound(0, volumePct, 100) / 100.0f);
    effect->stop();
    effect->play();
    return true;
}
#endif

#ifdef Q_OS_WIN
QString ensureTempSoundFile(const QByteArray& bytes,
                            const QString& baseName,
                            const QString& loadedFrom,
                            const QString& logPrefix)
{
    if (bytes.isEmpty()) {
        return QString();
    }

    return SoundSupport::ensureTempWavFile(bytes, baseName, loadedFrom, logPrefix);
}

QString clickWavFilePath()
{
    static QString cachedPath;
    if (!cachedPath.isEmpty()) return cachedPath;
    QString loadedFrom;
    cachedPath = ensureTempSoundFile(loadClickSoundBytes(&loadedFrom),
                                     QStringLiteral("windows_mouse_click"),
                                     loadedFrom,
                                     QStringLiteral("Click sound loaded from:"));
    return cachedPath;
}

QString tableClickWavFilePath()
{
    static QString cachedPath;
    if (!cachedPath.isEmpty()) return cachedPath;
    QString loadedFrom;
    cachedPath = ensureTempSoundFile(loadTableClickSoundBytes(&loadedFrom),
                                     QStringLiteral("tables_click_global"),
                                     loadedFrom,
                                     QStringLiteral("Table click sound loaded from:"));
    return cachedPath;
}

QString minimizeWavFilePath()
{
    static QString cachedPath;
    if (!cachedPath.isEmpty()) return cachedPath;
    QString loadedFrom;
    cachedPath = ensureTempSoundFile(loadMinimizeSoundBytes(&loadedFrom),
                                     QStringLiteral("window_minimize_custom"),
                                     loadedFrom,
                                     QStringLiteral("Minimize sound loaded from:"));
    return cachedPath;
}

QString restoreWavFilePath()
{
    static QString cachedPath;
    if (!cachedPath.isEmpty()) return cachedPath;
    QString loadedFrom;
    cachedPath = ensureTempSoundFile(loadRestoreSoundBytes(&loadedFrom),
                                     QStringLiteral("window_restore_custom"),
                                     loadedFrom,
                                     QStringLiteral("Restore sound loaded from:"));
    return cachedPath;
}

QString startupWavFilePath()
{
    static QString cachedPath;
    if (!cachedPath.isEmpty()) return cachedPath;
    QString loadedFrom;
    cachedPath = ensureTempSoundFile(loadStartupSoundBytes(&loadedFrom),
                                     QStringLiteral("window_startup_custom"),
                                     loadedFrom,
                                     QStringLiteral("Startup sound loaded from:"));
    return cachedPath;
}

QString closeWavFilePath()
{
    static QString cachedPath;
    if (!cachedPath.isEmpty()) return cachedPath;
    QString loadedFrom;
    cachedPath = ensureTempSoundFile(loadCloseSoundBytes(&loadedFrom),
                                     QStringLiteral("window_close_custom"),
                                     loadedFrom,
                                     QStringLiteral("Close sound loaded from:"));
    return cachedPath;
}

bool playNativeFileIfAvailable(const QString& path)
{
    return SoundSupport::playNativeFile(path, true, false);
}
#endif

} // namespace

namespace ClickSoundBackend {

qint64 nowMs()
{
    return QDateTime::currentMSecsSinceEpoch();
}

bool clickSoundsEnabled()
{
    return SoundSupport::soundsEnabled(true);
}

int clickSoundsVolumePct()
{
    return SoundSupport::soundsVolumePct(85);
}

bool isInsideTableView(QWidget* widget)
{
    for (QWidget* current = widget; current; current = current->parentWidget()) {
        if (qobject_cast<QTableView*>(current)) {
            return true;
        }
    }
    return false;
}

void reloadEffects()
{
    const bool enabled = clickSoundsEnabled();
    const int volumePct = clickSoundsVolumePct();
#if CS_HAS_MULTIMEDIA
    for (QSoundEffect* effect : {clickEffectInstance(), tableClickEffectInstance(), minimizeEffectInstance(), restoreEffectInstance(), startupEffectInstance(), closeEffectInstance()}) {
        effect->setMuted(!enabled);
        effect->setVolume(qBound(0, volumePct, 100) / 100.0f);
    }
#else
    Q_UNUSED(enabled);
    Q_UNUSED(volumePct);
#endif
}

void preloadStartupSoundAssets()
{
#ifdef Q_OS_WIN
    startupWavFilePath();
#endif
}

bool playClick(int volumePct)
{
#ifdef Q_OS_WIN
    if (playNativeFileIfAvailable(clickWavFilePath())) {
        return true;
    }
#endif
#if CS_HAS_MULTIMEDIA
    return playEffect(clickEffectInstance(), volumePct);
#else
    Q_UNUSED(volumePct);
    return false;
#endif
}

bool playTableClick(int volumePct)
{
#ifdef Q_OS_WIN
    if (playNativeFileIfAvailable(tableClickWavFilePath())) {
        return true;
    }
#endif
#if CS_HAS_MULTIMEDIA
    return playEffect(tableClickEffectInstance(), volumePct);
#else
    Q_UNUSED(volumePct);
    return false;
#endif
}

bool playMinimize(int volumePct)
{
#ifdef Q_OS_WIN
    if (playNativeFileIfAvailable(minimizeWavFilePath())) {
        return true;
    }
#endif
#if CS_HAS_MULTIMEDIA
    return playEffect(minimizeEffectInstance(), volumePct);
#else
    Q_UNUSED(volumePct);
    return false;
#endif
}

bool playRestore(int volumePct)
{
#ifdef Q_OS_WIN
    if (playNativeFileIfAvailable(restoreWavFilePath())) {
        return true;
    }
#endif
#if CS_HAS_MULTIMEDIA
    return playEffect(restoreEffectInstance(), volumePct);
#else
    Q_UNUSED(volumePct);
    return false;
#endif
}

bool playStartup(int volumePct)
{
#ifdef Q_OS_WIN
    if (playNativeFileIfAvailable(startupWavFilePath())) {
        return true;
    }
#endif
#if CS_HAS_MULTIMEDIA
    return playEffect(startupEffectInstance(), volumePct);
#else
    Q_UNUSED(volumePct);
    return false;
#endif
}

bool playClose(int volumePct)
{
#ifdef Q_OS_WIN
    if (playNativeFileIfAvailable(closeWavFilePath())) {
        return true;
    }
#endif
#if CS_HAS_MULTIMEDIA
    return playEffect(closeEffectInstance(), volumePct);
#else
    Q_UNUSED(volumePct);
    return false;
#endif
}

} // namespace ClickSoundBackend
