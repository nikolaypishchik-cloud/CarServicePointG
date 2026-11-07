#include "ui/tabs/settings/PasswordManager.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QSettings>

namespace PasswordManager {

QString keyEnabled()         { return QStringLiteral("security/password_enabled"); }
QString keyHash()            { return QStringLiteral("security/password_hash"); }
QString keyAutoLockEnabled() { return QStringLiteral("security/autolock_enabled"); }
QString keyAutoLockMinutes() { return QStringLiteral("security/autolock_minutes"); }
QString keyLockScreenStyle() { return QStringLiteral("security/lock_screen_style"); }

static QString hashPassword(const QString& raw)
{
    const QByteArray salted = QByteArrayLiteral("cs_app_salt_v1:") + raw.toUtf8();
    return QString::fromLatin1(QCryptographicHash::hash(salted, QCryptographicHash::Sha256).toHex());
}

bool isPasswordEnabled()
{
    QSettings s;
    return s.value(keyEnabled(), false).toBool();
}

void setPasswordEnabled(bool enabled)
{
    QSettings s;
    s.setValue(keyEnabled(), enabled);
    s.sync();
}

bool hasPassword()
{
    QSettings s;
    return !s.value(keyHash()).toString().isEmpty();
}

bool verifyPassword(const QString& input)
{
    QSettings s;
    const QString stored = s.value(keyHash()).toString();
    if (stored.isEmpty()) return false;
    return stored == hashPassword(input);
}

void setPassword(const QString& newPassword)
{
    QSettings s;
    s.setValue(keyHash(), hashPassword(newPassword));
    s.sync();
}

void resetPassword()
{
    QSettings s;
    s.remove(keyHash());
    s.setValue(keyEnabled(), false);
    s.sync();
}

bool isAutoLockEnabled()
{
    QSettings s;
    return s.value(keyAutoLockEnabled(), false).toBool();
}

void setAutoLockEnabled(bool enabled)
{
    QSettings s;
    s.setValue(keyAutoLockEnabled(), enabled);
    s.sync();
}

int autoLockMinutes()
{
    QSettings s;
    const int v = s.value(keyAutoLockMinutes(), 5).toInt();
    return (v > 0) ? v : 5;
}

void setAutoLockMinutes(int minutes)
{
    QSettings s;
    s.setValue(keyAutoLockMinutes(), (minutes > 0) ? minutes : 5);
    s.sync();
}

QString lockScreenStyle()
{
    QSettings s;
    return s.value(keyLockScreenStyle(), QString()).toString();
}

void setLockScreenStyle(const QString& id)
{
    QSettings s;
    s.setValue(keyLockScreenStyle(), id);
    s.sync();
}

namespace {

QStringList screensaverSearchDirs()
{
    QString winDir = QString::fromLocal8Bit(qgetenv("SystemRoot"));
    if (winDir.isEmpty()) winDir = QStringLiteral("C:/Windows");
    winDir.replace(QLatin1Char('\\'), QLatin1Char('/'));
    return {
        winDir + QStringLiteral("/System32/"),
        winDir + QStringLiteral("/SysWOW64/"),
    };
}

struct KnownSaver {
    const char* file;     // .scr filename in System32
    const char* display;  // Ukrainian label, matching Windows naming
};

} // namespace

QList<LockScreenOption> availableLockScreens()
{
    QList<LockScreenOption> result;
    result.append({QString(), QString::fromUtf8("(немає)")});

    static const KnownSaver known[] = {
        {"Bubbles.scr",          "Бульбашки"},
        {"Mystify.scr",          "Геометричний вальс"},
        {"ssText3d.scr",         "Об'ємний текст"},
        {"scrnsave.scr",         "Пусто"},
        {"Ribbons.scr",          "Стрічки"},
        {"PhotoScreensaver.scr", "Фотографії"},
    };

    const QStringList dirs = screensaverSearchDirs();
    for (const auto& s : known) {
        for (const QString& dir : dirs) {
            const QString full = dir + QString::fromUtf8(s.file);
            if (QFile::exists(full)) {
                const QString id = QFileInfo(QString::fromUtf8(s.file)).baseName();
                result.append({id, QString::fromUtf8(s.display)});
                break;
            }
        }
    }
    return result;
}

QString screensaverPath(const QString& id)
{
    if (id.isEmpty()) return {};
    const QStringList dirs = screensaverSearchDirs();
    for (const QString& dir : dirs) {
        const QString full = dir + id + QStringLiteral(".scr");
        if (QFile::exists(full)) return full;
    }
    return {};
}

} // namespace PasswordManager
