#pragma once

#include <QSqlDatabase>
#include <QString>

class ScopedSqliteConnection
{
public:
    ScopedSqliteConnection(const QString& dbPath,
                           const QString& purpose,
                           QString* errorText = nullptr,
                           bool readOnly = false);
    ~ScopedSqliteConnection();

    ScopedSqliteConnection(const ScopedSqliteConnection&) = delete;
    ScopedSqliteConnection& operator=(const ScopedSqliteConnection&) = delete;

    bool isOpen() const;
    QSqlDatabase db() const;
    QString connectionName() const;

private:
    QString m_connectionName;
    bool m_open = false;
};


namespace SqliteConnectionUtils {
void safeCloseAndRemoveDatabase(const QString& connectionName);
bool hasOtherOpenConnectionsForPath(const QString& dbPath, const QString& excludeConnectionName = QString());
int countOpenConnectionsForPath(const QString& dbPath, const QString& excludeConnectionName = QString());
}
