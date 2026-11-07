#include "infrastructure/db/sqlite/DbSettingsSqlSupport.h"

#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>
#include "infrastructure/db/sqlite/Db.h"
#include "infrastructure/db/sqlite/DbThreadConnectionSupport.h"

QString dbGetSettingValue(QSqlDatabase database, const QString& key, const QString& def)
{
    if (!database.isValid()) return def;
    if (!database.isOpen() && !database.open()) return def;
    QSqlQuery q(database);
    q.prepare(QStringLiteral("SELECT value FROM settings WHERE \"key\"=? LIMIT 1"));
    q.addBindValue(key);
    if (!q.exec() || !q.next()) return def;
    return q.value(0).toString();
}

bool dbSetSettingValue(QSqlDatabase database, const QString& key, const QVariant& value)
{
    if (!database.isValid()) return false;
    if (!database.isOpen() && !database.open()) return false;
    QSqlQuery q(database);
    q.prepare(QStringLiteral("INSERT INTO settings(\"key\", value) VALUES(?, ?) ON CONFLICT(\"key\") DO UPDATE SET value=excluded.value"));
    q.addBindValue(key);
    q.addBindValue(value);
    return q.exec();
}

QVariant dbGetSettingForConnection(QSqlDatabase database,const QString& key,const QVariant& def)
{
    if(DbThreadConnectionSupport::isRuntimeConnection(database.connectionName())) return Database::getSetting(key,def);
    if(!database.isValid() || !database.isOpen()) return def;
    QSqlQuery q(database);
    if(!q.prepare(QStringLiteral("SELECT value FROM settings WHERE key=?"))) {qWarning()<<"Connection setting prepare failed"<<key<<q.lastError().text();return def;}
    q.addBindValue(key);
    if(!q.exec()) {qWarning()<<"Connection setting read failed"<<key<<q.lastError().text();return def;}
    if(!q.next()) return def;
    const auto value=q.value(0);return value.isValid() && !value.isNull()?value:def;
}

bool dbSetSettingForConnection(QSqlDatabase database,const QString& key,const QVariant& value)
{
    if(DbThreadConnectionSupport::isRuntimeConnection(database.connectionName())) return Database::setSetting(key,value);
    if(!database.isValid() || !database.isOpen()) return false;
    QSqlQuery q(database);
    if(!q.prepare(QStringLiteral("INSERT INTO settings(key,value) VALUES(?,?) ON CONFLICT(key) DO UPDATE SET value=excluded.value"))) {qWarning()<<"Connection setting write prepare failed"<<key<<q.lastError().text();return false;}
    q.addBindValue(key);q.addBindValue(value);
    if(!q.exec() || q.numRowsAffected()!=1) {qWarning()<<"Connection setting write failed"<<key<<q.lastError().text();return false;}
    return true;
}
