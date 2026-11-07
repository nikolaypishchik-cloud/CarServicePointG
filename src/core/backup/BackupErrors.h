#pragma once

#include <QString>

namespace BackupErrors {

inline QString invalidSnapshot(const QString& details)
{
    return QStringLiteral("Невалідний snapshot: %1").arg(details);
}

inline QString snapshotCreateFailed(const QString& details)
{
    return QStringLiteral("Не вдалося створити snapshot: %1").arg(details);
}

inline QString restoreFailed(const QString& details)
{
    return QStringLiteral("Не вдалося виконати restore: %1").arg(details);
}

inline QString draftFailed(const QString& details)
{
    return QStringLiteral("Не вдалося обробити recovery draft: %1").arg(details);
}

} // namespace BackupErrors
