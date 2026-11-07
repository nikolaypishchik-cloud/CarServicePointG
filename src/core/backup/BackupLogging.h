#pragma once

#include "core/backup/BackupPolicy.h"

#include <QCoreApplication>
#include <QDebug>
#include <QString>

namespace BackupLogging {

inline bool detailedLoggingEnabled()
{
    if (!QCoreApplication::instance()) return true;
    return BackupPolicy::load().detailedLogging;
}

inline void info(const QString& message)
{
    if (!detailedLoggingEnabled()) return;
    qInfo().noquote() << QStringLiteral("BACKUP ") + message;
}

inline void warn(const QString& message)
{
    qWarning().noquote() << QStringLiteral("BACKUP ") + message;
}

inline void recoveryInfo(const QString& message)
{
    if (!detailedLoggingEnabled()) return;
    qInfo().noquote() << QStringLiteral("RECOVERY ") + message;
}

inline void recoveryWarn(const QString& message)
{
    qWarning().noquote() << QStringLiteral("RECOVERY ") + message;
}

} // namespace BackupLogging
