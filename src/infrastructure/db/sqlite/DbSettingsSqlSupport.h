#pragma once

#include <QSqlDatabase>
#include <QString>
#include <QVariant>

QString dbGetSettingValue(QSqlDatabase database, const QString& key, const QString& def = QString());
bool dbSetSettingValue(QSqlDatabase database, const QString& key, const QVariant& value);

// Runtime handles retain native settings and cache behavior; explicit caller-owned
// connections read/write only their own settings table.
QVariant dbGetSettingForConnection(QSqlDatabase database, const QString& key, const QVariant& def = QVariant());
bool dbSetSettingForConnection(QSqlDatabase database, const QString& key, const QVariant& value);
