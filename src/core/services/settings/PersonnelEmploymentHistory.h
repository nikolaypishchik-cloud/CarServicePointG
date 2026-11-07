#pragma once
#include "core/model/MasterRow.h"
#include <QSqlDatabase>
#include <QJsonObject>
#include <QDate>

namespace PersonnelEmploymentHistory {
QJsonObject load(const QString& id, QString* error = nullptr);
bool edit(const MasterRow& expected, const QString& name, const QString& role, QString* error);
bool setActive(const QString& id, bool active, const QString& effectiveDate, QString* error, const QString& previousEnd = {});

QJsonObject load(QSqlDatabase db, const QString& id, QString* error = nullptr);
bool overlaps(QSqlDatabase db, const QString& id, const QDate& from, const QDate& to);
bool edit(QSqlDatabase db, const MasterRow& expected, const QString& name, const QString& role, QString* error);
bool setActive(QSqlDatabase db, const QString& id, bool active, const QString& effectiveDate, QString* error, const QString& previousEnd = {});
}
