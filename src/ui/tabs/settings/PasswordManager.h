#pragma once

#include <QList>
#include <QString>

// Manages startup password: storage (SHA-256 hash in QSettings),
// verification, and reset. All methods are thread-safe for read access.
namespace PasswordManager {

struct LockScreenOption {
    QString id;          // empty = "(none)", otherwise screensaver basename like "Bubbles"
    QString displayName; // localized label for the combobox
};

bool isPasswordEnabled();
void setPasswordEnabled(bool enabled);

bool hasPassword();
bool verifyPassword(const QString& input);
void setPassword(const QString& newPassword);
void resetPassword(); // clears the hash and disables password protection

// Auto-lock settings
bool isAutoLockEnabled();
void setAutoLockEnabled(bool enabled);
int  autoLockMinutes();          // default: 5
void setAutoLockMinutes(int minutes);

// Lock-screen animation (Windows screensavers)
QString lockScreenStyle();                   // saved id (e.g. "Bubbles") or empty for none
void    setLockScreenStyle(const QString& id);
QList<LockScreenOption> availableLockScreens();   // option list for the settings UI
QString screensaverPath(const QString& id);  // resolves a full .scr path or empty

// QSettings key constants (used internally; exposed for tests)
QString keyEnabled();             // "security/password_enabled"
QString keyHash();                // "security/password_hash"
QString keyAutoLockEnabled();     // "security/autolock_enabled"
QString keyAutoLockMinutes();     // "security/autolock_minutes"
QString keyLockScreenStyle();     // "security/lock_screen_style"

} // namespace PasswordManager
