#pragma once

#include "core/services/booking/BookingRecord.h"

#include <QDateTime>
#include <QString>
#include <optional>

class BookingRepository
{
public:
    bool ensureSchema(QString* errorText = nullptr) const;
    bool upsertBooking(const BookingRecord& booking, bool planningSynced, const QString& rejectionReason, QString* errorText = nullptr) const;
    bool isPlanningSynced(const QString& bookingUid) const;
    bool markPlanningSynced(const QString& bookingUid, const QDate& day, int hour, QString* errorText = nullptr) const;
    bool updateLocalStatus(const QString& bookingUid, const QString& status, const QString& reason = QString(), QString* errorText = nullptr) const;
    std::optional<BookingRecord> findByDateTimePlate(const QDate& date, const QTime& timeFrom, const QString& plateDisplay, QString* errorText = nullptr) const;
    QDateTime lastSyncTime() const;
    void setLastSyncTime(const QDateTime& value) const;
};
