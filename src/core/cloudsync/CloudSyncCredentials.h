#pragma once

#include <QString>

namespace CloudSyncCredentials {

// The password is never put into the database or source code. On Windows the
// saved value is protected by DPAPI and can only be read by this user account
// on this PC.
bool hasSavedOwnerPassword();
QString loadOwnerPassword(QString* errorText = nullptr);
bool saveOwnerPassword(const QString& password, QString* errorText = nullptr);
void clearOwnerPassword();
QString protectToken(const QString& token, QString* errorText = nullptr);
QString unprotectToken(const QString& stored, QString* errorText = nullptr);

} // namespace CloudSyncCredentials
