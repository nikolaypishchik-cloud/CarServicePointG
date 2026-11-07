#pragma once

#include <QByteArray>
#include <QString>

#if CS_HAS_MULTIMEDIA
class QSoundEffect;
#endif

namespace SoundSupport {

bool soundsEnabled(bool defaultValue = true);
int soundsVolumePct(int defaultValue);
void invalidateSettingsCache();
QByteArray loadAssetBytes(const QString& fileName, QString* loadedFrom = nullptr, const QString& categoryLabel = QString());
QString ensureTempWavFile(const QByteArray& bytes,
                          const QString& filePrefix,
                          const QString& loadedFrom = QString(),
                          const QString& logLabel = QString());

#if CS_HAS_MULTIMEDIA
void setupEffect(QSoundEffect* effect, const QString& fileName, int loopCount, float defaultVolume, const QString& categoryLabel = QString());
#endif

bool playNativeFile(const QString& path, bool asyncPlayback = true, bool loop = false);
void stopNativePlayback();

} // namespace SoundSupport
