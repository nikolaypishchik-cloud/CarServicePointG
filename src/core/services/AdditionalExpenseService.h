#pragma once

#include <QDate>
#include <QSqlDatabase>
#include <QStringList>

namespace AdditionalExpenseService {
bool ensureSchema(QSqlDatabase db, QString* error = nullptr);
bool canWrite(QSqlDatabase db, bool deleting = false);
bool add(QSqlDatabase db, const QDate& date, const QString& category,
         const QString& description, double amount, qint64* id = nullptr, QString* error = nullptr);
bool remove(QSqlDatabase db, qint64 id, QString* error = nullptr);
QStringList categories(QSqlDatabase db);
}

namespace AdditionalExpenseService {
// UI entry points: connection and transaction ownership remain in the service.
bool canWrite(bool deleting = false);
QStringList categories();
bool add(const QDate& date, const QString& category, const QString& description, double amount, qint64* id = nullptr, QString* error = nullptr);
bool remove(qint64 id, QString* error = nullptr);
}
