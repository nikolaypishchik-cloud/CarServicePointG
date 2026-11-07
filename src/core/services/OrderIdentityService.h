#pragma once

#include <QDate>
#include <QDateTime>
#include <QSqlDatabase>
#include <QString>
#include <functional>

namespace OrderIdentityService {

struct Allocation {
    QString orderUid;
    int internalNumber = 0;
    int visibleNumber = 0;
    int year = 0;
    bool replayed = false;
};

// Owns a savepoint, so the caller can commit the allocation and order together.
// Reservations remain after order deletion. A replay must never resurrect a deleted order.
bool allocateAnnualIdentity(QSqlDatabase database, const QString& requestUid,
    const QDateTime& createdAt, int preferredInternalNumber, Allocation* allocation,
    QString* errorText, const std::function<QSqlDatabase()>& registryResolver = {});

int orderYearForDate(const QDate& date);
QString createOrderUid();
int nextVisibleOrderNumber(QSqlDatabase activeDb,
                           const std::function<QSqlDatabase()>& registryResolver,
                           const QDate& openedDate);
bool ensureCanonicalIdentityIndexes(QSqlDatabase database, QString* errorText = nullptr);
bool reserveIdentityBeforeDeletion(QSqlDatabase database, int internalNumber, QString* errorText);

} // namespace OrderIdentityService
