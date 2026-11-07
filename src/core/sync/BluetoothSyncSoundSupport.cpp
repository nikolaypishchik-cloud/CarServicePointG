#include "core/sync/BluetoothSyncSoundSupport.h"

#include "core/util/SoundSupport.h"

#if CS_HAS_MULTIMEDIA
#include <QSoundEffect>
#endif

namespace BluetoothSyncSoundSupport {
namespace {
bool bluetoothSoundsEnabled()
{
    return SoundSupport::soundsEnabled(true);
}

int bluetoothSoundsVolumePct()
{
    return SoundSupport::soundsVolumePct(85);
}

QByteArray loadBluetoothSoundBytes(const QString& resourceName, QString* loadedFrom = nullptr)
{
    return SoundSupport::loadAssetBytes(resourceName, loadedFrom, QStringLiteral("Bluetooth sound"));
}

QByteArray loadBluetoothSearchSoundBytes(QString* loadedFrom = nullptr)
{
    return loadBluetoothSoundBytes(QStringLiteral("bluetooth_search_scan.wav"), loadedFrom);
}

QByteArray loadBluetoothConnectSyncSoundBytes(QString* loadedFrom = nullptr)
{
    return loadBluetoothSoundBytes(QStringLiteral("bluetooth_connect_sync.wav"), loadedFrom);
}

QByteArray loadBluetoothDisconnectLossSoundBytes(QString* loadedFrom = nullptr)
{
    return loadBluetoothSoundBytes(QStringLiteral("bluetooth_disconnect_loss.wav"), loadedFrom);
}

#if CS_HAS_MULTIMEDIA
void setupBluetoothSoundEffect(QSoundEffect* effect, const QString& resourceName, int loopCount)
{
    SoundSupport::setupEffect(effect, resourceName, loopCount, 0.85f, QStringLiteral("Bluetooth sound"));
}

QSoundEffect* bluetoothSearchEffectInstance()
{
    static QSoundEffect* effect = []() {
        auto* e = new QSoundEffect();
        setupBluetoothSoundEffect(e, QStringLiteral("bluetooth_search_scan.wav"), QSoundEffect::Infinite);
        return e;
    }();
    return effect;
}

QSoundEffect* bluetoothConnectSyncEffectInstance()
{
    static QSoundEffect* effect = []() {
        auto* e = new QSoundEffect();
        setupBluetoothSoundEffect(e, QStringLiteral("bluetooth_connect_sync.wav"), 1);
        return e;
    }();
    return effect;
}

QSoundEffect* bluetoothDisconnectLossEffectInstance()
{
    static QSoundEffect* effect = []() {
        auto* e = new QSoundEffect();
        setupBluetoothSoundEffect(e, QStringLiteral("bluetooth_disconnect_loss.wav"), 1);
        return e;
    }();
    return effect;
}
#endif

#ifdef Q_OS_WIN
QString bluetoothSoundWavFilePath(const QByteArray& bytes,
                                  const QString& filePrefix,
                                  const char* logLabel,
                                  const QString& loadedFrom)
{
    return SoundSupport::ensureTempWavFile(bytes, filePrefix, loadedFrom, QString::fromLatin1(logLabel));
}

QString bluetoothSearchWavFilePath()
{
    static QString cachedPath;
    if (!cachedPath.isEmpty()) return cachedPath;
    QString loadedFrom;
    const QByteArray bytes = loadBluetoothSearchSoundBytes(&loadedFrom);
    cachedPath = bluetoothSoundWavFilePath(bytes, QStringLiteral("bluetooth_search_scan"), "Bluetooth search sound loaded from:", loadedFrom);
    return cachedPath;
}

QString bluetoothConnectSyncWavFilePath()
{
    static QString cachedPath;
    if (!cachedPath.isEmpty()) return cachedPath;
    QString loadedFrom;
    const QByteArray bytes = loadBluetoothConnectSyncSoundBytes(&loadedFrom);
    cachedPath = bluetoothSoundWavFilePath(bytes, QStringLiteral("bluetooth_connect_sync"), "Bluetooth connect/sync sound loaded from:", loadedFrom);
    return cachedPath;
}

QString bluetoothDisconnectLossWavFilePath()
{
    static QString cachedPath;
    if (!cachedPath.isEmpty()) return cachedPath;
    QString loadedFrom;
    const QByteArray bytes = loadBluetoothDisconnectLossSoundBytes(&loadedFrom);
    cachedPath = bluetoothSoundWavFilePath(bytes, QStringLiteral("bluetooth_disconnect_loss"), "Bluetooth disconnect/loss sound loaded from:", loadedFrom);
    return cachedPath;
}

bool playNativeWindowsBluetoothSound(const QString& path, bool loop)
{
    return SoundSupport::playNativeFile(path, true, loop);
}

bool playNativeWindowsBluetoothSearchLoop()
{
    return playNativeWindowsBluetoothSound(bluetoothSearchWavFilePath(), true);
}

bool playNativeWindowsBluetoothConnectSyncOnce()
{
    return playNativeWindowsBluetoothSound(bluetoothConnectSyncWavFilePath(), false);
}

bool playNativeWindowsBluetoothDisconnectLossOnce()
{
    return playNativeWindowsBluetoothSound(bluetoothDisconnectLossWavFilePath(), false);
}

void stopNativeWindowsBluetoothSearchLoop()
{
    SoundSupport::stopNativePlayback();
}
#endif

} // namespace

void playSearchLoop()
{
    if (!bluetoothSoundsEnabled()) return;
#ifdef Q_OS_WIN
    playNativeWindowsBluetoothSearchLoop();
#else
#if CS_HAS_MULTIMEDIA
    if (auto* effect = bluetoothSearchEffectInstance()) {
        effect->setVolume(static_cast<float>(bluetoothSoundsVolumePct()) / 100.0f);
        if (!effect->isPlaying()) effect->play();
    }
#endif
#endif
}

void stopSearchLoop()
{
#ifdef Q_OS_WIN
    stopNativeWindowsBluetoothSearchLoop();
#else
#if CS_HAS_MULTIMEDIA
    if (auto* effect = bluetoothSearchEffectInstance()) {
        if (effect->isPlaying()) effect->stop();
    }
#endif
#endif
}

void playConnectSync()
{
    if (!bluetoothSoundsEnabled()) return;
#ifdef Q_OS_WIN
    playNativeWindowsBluetoothConnectSyncOnce();
#else
#if CS_HAS_MULTIMEDIA
    if (auto* effect = bluetoothConnectSyncEffectInstance()) {
        effect->setVolume(static_cast<float>(bluetoothSoundsVolumePct()) / 100.0f);
        if (effect->isPlaying()) effect->stop();
        effect->play();
    }
#endif
#endif
}

void playDisconnectLoss()
{
    if (!bluetoothSoundsEnabled()) return;
#ifdef Q_OS_WIN
    playNativeWindowsBluetoothDisconnectLossOnce();
#else
#if CS_HAS_MULTIMEDIA
    if (auto* effect = bluetoothDisconnectLossEffectInstance()) {
        effect->setVolume(static_cast<float>(bluetoothSoundsVolumePct()) / 100.0f);
        if (effect->isPlaying()) effect->stop();
        effect->play();
    }
#endif
#endif
}

} // namespace BluetoothSyncSoundSupport
