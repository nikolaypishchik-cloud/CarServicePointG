#pragma once

#include <QString>
#include <QStringList>

namespace DbThreadConnectionSupport {

QString currentConnectionName(const QString& baseName);
QStringList trackedConnectionNames(const QString& baseName);
void registerConnectionName(const QString& name);
void registerConnectionName(const QString& name, const QString& databasePath);
void registerRuntimeConnection(const QString& name, const QString& databasePath);
bool isRuntimeConnection(const QString& name);
void updateConnectionPath(const QString& name, const QString& databasePath);
void unregisterConnectionName(const QString& name);
void clear();
bool isConnectionOwnedByCurrentThread(const QString& name);
QString trackedConnectionPath(const QString& name);

} // namespace DbThreadConnectionSupport
