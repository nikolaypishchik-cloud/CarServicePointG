#include "ui/common/dialogs/MessageBoxSoundSupport.h"

#include "core/util/SoundSupport.h"

#include <QApplication>
#include <QDateTime>
#include <QDebug>

#if CS_HAS_MULTIMEDIA
#include <QSoundEffect>
#endif

namespace MessageBoxSoundSupport {
namespace {
QByteArray loadPopupAlertSoundBytes(QString* loadedFrom = nullptr)
{
    return SoundSupport::loadAssetBytes(QStringLiteral("warning_popup_global.wav"), loadedFrom, QStringLiteral("Popup alert sound"));
}

QByteArray loadPopupCloseSoundBytes(QString* loadedFrom = nullptr)
{
    return SoundSupport::loadAssetBytes(QStringLiteral("warning_popup_close_global.wav"), loadedFrom, QStringLiteral("Popup close sound"));
}

QByteArray loadPopupConfirmationShowSoundBytes(QString* loadedFrom = nullptr)
{
    return SoundSupport::loadAssetBytes(QStringLiteral("confirmation_popup_show.wav"), loadedFrom, QStringLiteral("Popup confirmation show sound"));
}

QByteArray loadPopupConfirmationYesSoundBytes(QString* loadedFrom = nullptr)
{
    return SoundSupport::loadAssetBytes(QStringLiteral("confirmation_popup_yes.wav"), loadedFrom, QStringLiteral("Popup confirmation yes sound"));
}

QByteArray loadPopupConfirmationNoSoundBytes(QString* loadedFrom = nullptr)
{
    return SoundSupport::loadAssetBytes(QStringLiteral("confirmation_popup_no.wav"), loadedFrom, QStringLiteral("Popup confirmation no sound"));
}

QByteArray loadPopupSettingsActionSoundBytes(QString* loadedFrom = nullptr)
{
    return SoundSupport::loadAssetBytes(QStringLiteral("settings_message_action.wav"), loadedFrom, QStringLiteral("Popup settings action sound"));
}

#if CS_HAS_MULTIMEDIA
QSoundEffect* popupAlertEffectInstance()
{
    static QSoundEffect* effect = []() {
        auto* e = new QSoundEffect();
        SoundSupport::setupEffect(e, QStringLiteral("warning_popup_global.wav"), 1, 0.90f);
        return e;
    }();
    return effect;
}

QSoundEffect* popupCloseEffectInstance()
{
    static QSoundEffect* effect = []() {
        auto* e = new QSoundEffect();
        SoundSupport::setupEffect(e, QStringLiteral("warning_popup_close_global.wav"), 1, 0.90f);
        return e;
    }();
    return effect;
}

QSoundEffect* popupConfirmationShowEffectInstance()
{
    static QSoundEffect* effect = []() {
        auto* e = new QSoundEffect();
        SoundSupport::setupEffect(e, QStringLiteral("confirmation_popup_show.wav"), 1, 0.90f);
        return e;
    }();
    return effect;
}

QSoundEffect* popupConfirmationYesEffectInstance()
{
    static QSoundEffect* effect = []() {
        auto* e = new QSoundEffect();
        SoundSupport::setupEffect(e, QStringLiteral("confirmation_popup_yes.wav"), 1, 0.90f);
        return e;
    }();
    return effect;
}

QSoundEffect* popupConfirmationNoEffectInstance()
{
    static QSoundEffect* effect = []() {
        auto* e = new QSoundEffect();
        SoundSupport::setupEffect(e, QStringLiteral("confirmation_popup_no.wav"), 1, 0.90f);
        return e;
    }();
    return effect;
}

QSoundEffect* popupSettingsActionEffectInstance()
{
    static QSoundEffect* effect = []() {
        auto* e = new QSoundEffect();
        SoundSupport::setupEffect(e, QStringLiteral("settings_message_action.wav"), 1, 0.90f);
        return e;
    }();
    return effect;
}
#endif

#ifdef Q_OS_WIN
QString popupAlertWavFilePath()
{
    static QString cachedPath;
    if (!cachedPath.isEmpty()) return cachedPath;
    QString loadedFrom;
    const QByteArray bytes = loadPopupAlertSoundBytes(&loadedFrom);
    cachedPath = SoundSupport::ensureTempWavFile(bytes, QStringLiteral("warning_popup_global"), loadedFrom, QStringLiteral("Popup alert sound loaded from:"));
    return cachedPath;
}

QString popupCloseWavFilePath()
{
    static QString cachedPath;
    if (!cachedPath.isEmpty()) return cachedPath;
    QString loadedFrom;
    const QByteArray bytes = loadPopupCloseSoundBytes(&loadedFrom);
    cachedPath = SoundSupport::ensureTempWavFile(bytes, QStringLiteral("warning_popup_close_global"), loadedFrom, QStringLiteral("Popup close sound loaded from:"));
    return cachedPath;
}

QString popupConfirmationShowWavFilePath()
{
    static QString cachedPath;
    if (!cachedPath.isEmpty()) return cachedPath;
    QString loadedFrom;
    const QByteArray bytes = loadPopupConfirmationShowSoundBytes(&loadedFrom);
    cachedPath = SoundSupport::ensureTempWavFile(bytes, QStringLiteral("confirmation_popup_show"), loadedFrom, QStringLiteral("Popup confirmation show sound loaded from:"));
    return cachedPath;
}

QString popupConfirmationYesWavFilePath()
{
    static QString cachedPath;
    if (!cachedPath.isEmpty()) return cachedPath;
    QString loadedFrom;
    const QByteArray bytes = loadPopupConfirmationYesSoundBytes(&loadedFrom);
    cachedPath = SoundSupport::ensureTempWavFile(bytes, QStringLiteral("confirmation_popup_yes"), loadedFrom, QStringLiteral("Popup confirmation YES sound loaded from:"));
    return cachedPath;
}

QString popupConfirmationNoWavFilePath()
{
    static QString cachedPath;
    if (!cachedPath.isEmpty()) return cachedPath;
    QString loadedFrom;
    const QByteArray bytes = loadPopupConfirmationNoSoundBytes(&loadedFrom);
    cachedPath = SoundSupport::ensureTempWavFile(bytes, QStringLiteral("confirmation_popup_no"), loadedFrom, QStringLiteral("Popup confirmation NO sound loaded from:"));
    return cachedPath;
}

QString popupSettingsActionWavFilePath()
{
    static QString cachedPath;
    if (!cachedPath.isEmpty()) return cachedPath;
    QString loadedFrom;
    const QByteArray bytes = loadPopupSettingsActionSoundBytes(&loadedFrom);
    cachedPath = SoundSupport::ensureTempWavFile(bytes, QStringLiteral("settings_message_action"), loadedFrom, QStringLiteral("Popup settings-action sound loaded from:"));
    return cachedPath;
}
#endif

bool playAlertBackend(int volumePct)
{
#ifdef Q_OS_WIN
    Q_UNUSED(volumePct);
    if (SoundSupport::playNativeFile(popupAlertWavFilePath(), true, false)) return true;
#endif
#if CS_HAS_MULTIMEDIA
    if (auto* effect = popupAlertEffectInstance()) {
        effect->setMuted(false);
        effect->setVolume(qBound(0, volumePct, 100) / 100.0f);
        effect->stop();
        effect->play();
        return true;
    }
#endif
    return false;
}

bool playCloseBackend(int volumePct)
{
#ifdef Q_OS_WIN
    Q_UNUSED(volumePct);
    if (SoundSupport::playNativeFile(popupCloseWavFilePath(), true, false)) return true;
#endif
#if CS_HAS_MULTIMEDIA
    if (auto* effect = popupCloseEffectInstance()) {
        effect->setMuted(false);
        effect->setVolume(qBound(0, volumePct, 100) / 100.0f);
        effect->stop();
        effect->play();
        return true;
    }
#endif
    return false;
}

bool playConfirmationShowBackend(int volumePct)
{
#ifdef Q_OS_WIN
    Q_UNUSED(volumePct);
    if (SoundSupport::playNativeFile(popupConfirmationShowWavFilePath(), true, false)) return true;
#endif
#if CS_HAS_MULTIMEDIA
    if (auto* effect = popupConfirmationShowEffectInstance()) {
        effect->setMuted(false);
        effect->setVolume(qBound(0, volumePct, 100) / 100.0f);
        effect->stop();
        effect->play();
        return true;
    }
#endif
    return false;
}

bool playConfirmationYesBackend(int volumePct)
{
#ifdef Q_OS_WIN
    Q_UNUSED(volumePct);
    if (SoundSupport::playNativeFile(popupConfirmationYesWavFilePath(), true, false)) return true;
#endif
#if CS_HAS_MULTIMEDIA
    if (auto* effect = popupConfirmationYesEffectInstance()) {
        effect->setMuted(false);
        effect->setVolume(qBound(0, volumePct, 100) / 100.0f);
        effect->stop();
        effect->play();
        return true;
    }
#endif
    return false;
}

bool playConfirmationNoBackend(int volumePct)
{
#ifdef Q_OS_WIN
    Q_UNUSED(volumePct);
    if (SoundSupport::playNativeFile(popupConfirmationNoWavFilePath(), true, false)) return true;
#endif
#if CS_HAS_MULTIMEDIA
    if (auto* effect = popupConfirmationNoEffectInstance()) {
        effect->setMuted(false);
        effect->setVolume(qBound(0, volumePct, 100) / 100.0f);
        effect->stop();
        effect->play();
        return true;
    }
#endif
    return false;
}

bool playSettingsActionBackend(int volumePct)
{
#ifdef Q_OS_WIN
    Q_UNUSED(volumePct);
    if (SoundSupport::playNativeFile(popupSettingsActionWavFilePath(), true, false)) return true;
#endif
#if CS_HAS_MULTIMEDIA
    if (auto* effect = popupSettingsActionEffectInstance()) {
        effect->setMuted(false);
        effect->setVolume(qBound(0, volumePct, 100) / 100.0f);
        effect->stop();
        effect->play();
        return true;
    }
#endif
    return false;
}

bool playOrBeep(bool (*backend)(int), int volumePct, const char* warningText)
{
    if (backend(volumePct)) return true;
    QApplication::beep();
    qWarning() << warningText;
    return false;
}
} // namespace

qint64 nowMs()
{
    return QDateTime::currentMSecsSinceEpoch();
}

bool soundsEnabled()
{
    return SoundSupport::soundsEnabled(true);
}

int soundsVolumePct()
{
    return SoundSupport::soundsVolumePct(90);
}

bool playAlert(int volumePct)
{
    return playOrBeep(&playAlertBackend, volumePct, "All popup alert sound backends failed; fallback beep used");
}

bool playClose(int volumePct)
{
    return playOrBeep(&playCloseBackend, volumePct, "All popup close sound backends failed; fallback beep used");
}

bool playConfirmationShow(int volumePct)
{
    return playOrBeep(&playConfirmationShowBackend, volumePct, "All popup confirmation show sound backends failed; fallback beep used");
}

bool playConfirmationYes(int volumePct)
{
    return playOrBeep(&playConfirmationYesBackend, volumePct, "All popup confirmation YES sound backends failed; fallback beep used");
}

bool playConfirmationNo(int volumePct)
{
    return playOrBeep(&playConfirmationNoBackend, volumePct, "All popup confirmation NO sound backends failed; fallback beep used");
}

bool playSettingsAction(int volumePct)
{
    return playOrBeep(&playSettingsActionBackend, volumePct, "All popup settings-action sound backends failed; fallback beep used");
}

void primeSettingsAction()
{
#ifdef Q_OS_WIN
    Q_UNUSED(popupSettingsActionWavFilePath());
#endif
#if CS_HAS_MULTIMEDIA
    if (auto* effect = popupSettingsActionEffectInstance()) {
        effect->setMuted(false);
    }
#endif
}

} // namespace MessageBoxSoundSupport
