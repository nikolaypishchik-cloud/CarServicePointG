#pragma once
#include <QSqlDatabase>
#include <QString>

namespace OrderDeletionPublication
{
bool validatePartLocators(int number, const QString &uid, QString *error);
bool record(QSqlDatabase source, int number, QString *error);
bool replay(QSqlDatabase source, QString *error);
bool alreadyDeleted(QSqlDatabase source, int number, QString *error);
} // namespace OrderDeletionPublication
