#include "core/cloudsync/CloudSyncCredentials.h"

#include <QByteArray>
#include <QSettings>

#ifdef Q_OS_WIN
#include <windows.h>
#include <wincrypt.h>
#endif

namespace {

const QString kSavedPasswordKey = QStringLiteral("cloudsync/owner_password_dpapi");

#ifdef Q_OS_WIN
class LocalBlob final
{
public:
    DATA_BLOB blob{};

    ~LocalBlob()
    {
        if (blob.pbData) LocalFree(blob.pbData);
    }
};
#endif

} // namespace

namespace CloudSyncCredentials {

QString protectToken(const QString& token, QString* errorText)
{
    if (token.isEmpty()) return {};
#ifdef Q_OS_WIN
    const QByteArray bytes = token.toUtf8();
    DATA_BLOB source{static_cast<DWORD>(bytes.size()),
                     reinterpret_cast<BYTE*>(const_cast<char*>(bytes.constData()))};
    LocalBlob protectedBlob;
    if (CryptProtectData(&source, L"CarServicePointG access token", nullptr, nullptr, nullptr,
                         CRYPTPROTECT_UI_FORBIDDEN, &protectedBlob.blob)) {
        return QStringLiteral("dpapi:v1:") + QString::fromLatin1(
            QByteArray(reinterpret_cast<const char*>(protectedBlob.blob.pbData),
                       static_cast<int>(protectedBlob.blob.cbData)).toBase64());
    }
#endif
    if (errorText) *errorText = QString::fromUtf8("Не вдалося захистити токен Windows. Відкритий токен не збережено.");
    return {};
}

QString unprotectToken(const QString& stored, QString* errorText)
{
    if (stored.isEmpty()) return {};
#ifdef Q_OS_WIN
    if (stored.startsWith(QStringLiteral("dpapi:v1:"))) {
        const QByteArray bytes = QByteArray::fromBase64(stored.mid(9).toLatin1());
        DATA_BLOB source{static_cast<DWORD>(bytes.size()),
                         reinterpret_cast<BYTE*>(const_cast<char*>(bytes.constData()))};
        LocalBlob plain;
        if (CryptUnprotectData(&source, nullptr, nullptr, nullptr, nullptr,
                               CRYPTPROTECT_UI_FORBIDDEN, &plain.blob)) {
            const QString value = QString::fromUtf8(reinterpret_cast<const char*>(plain.blob.pbData),
                                                  static_cast<int>(plain.blob.cbData));
            SecureZeroMemory(plain.blob.pbData, plain.blob.cbData);
            return value;
        }
    }
#endif
    if (errorText) *errorText = QString::fromUtf8("Токен недоступний для цього облікового запису Windows. Увійдіть повторно.");
    return {};
}

bool hasSavedOwnerPassword()
{
    QSettings settings;
    return !settings.value(kSavedPasswordKey).toByteArray().isEmpty();
}

QString loadOwnerPassword(QString* errorText)
{
    QSettings settings;
    const QByteArray protectedBytes = QByteArray::fromBase64(settings.value(kSavedPasswordKey).toByteArray());
    if (protectedBytes.isEmpty()) return {};

#ifdef Q_OS_WIN
    DATA_BLOB source{};
    source.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(protectedBytes.constData()));
    source.cbData = static_cast<DWORD>(protectedBytes.size());
    LocalBlob plain;
    if (!CryptUnprotectData(&source, nullptr, nullptr, nullptr, nullptr,
                            CRYPTPROTECT_UI_FORBIDDEN, &plain.blob)) {
        clearOwnerPassword();
        if (errorText) *errorText = QString::fromUtf8("Не вдалося прочитати збережений пароль Windows: %1").arg(GetLastError());
        return {};
    }
    return QString::fromUtf8(reinterpret_cast<const char*>(plain.blob.pbData), static_cast<int>(plain.blob.cbData));
#else
    Q_UNUSED(errorText);
    return {};
#endif
}

bool saveOwnerPassword(const QString& password, QString* errorText)
{
    if (password.isEmpty()) {
        clearOwnerPassword();
        return true;
    }

#ifdef Q_OS_WIN
    const QByteArray sourceBytes = password.toUtf8();
    DATA_BLOB source{};
    source.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(sourceBytes.constData()));
    source.cbData = static_cast<DWORD>(sourceBytes.size());
    LocalBlob protectedBlob;
    if (!CryptProtectData(&source, L"CarServicePointG cloud-sync owner password", nullptr, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &protectedBlob.blob)) {
        if (errorText) *errorText = QString::fromUtf8("Не вдалося захистити пароль Windows: %1").arg(GetLastError());
        return false;
    }

    const QByteArray protectedBytes(reinterpret_cast<const char*>(protectedBlob.blob.pbData),
                                    static_cast<int>(protectedBlob.blob.cbData));
    QSettings settings;
    settings.setValue(kSavedPasswordKey, protectedBytes.toBase64());
    settings.sync();
    return settings.status() == QSettings::NoError;
#else
    if (errorText) *errorText = QString::fromUtf8("Збереження пароля підтримується лише у Windows-версії застосунку.");
    return false;
#endif
}

void clearOwnerPassword()
{
    QSettings settings;
    settings.remove(kSavedPasswordKey);
    settings.sync();
}

} // namespace CloudSyncCredentials
