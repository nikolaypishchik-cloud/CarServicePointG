#pragma once

#include "core/services/booking/BookingRecord.h"

#include <QDate>
#include <QString>
#include <QStringList>

class PlanningAvailabilityService
{
public:
    bool canAcceptOnlineBooking(const BookingRecord& booking, QString* reason = nullptr) const;
    bool addOnlineBookingToPlanning(const BookingRecord& booking, QString* reason = nullptr) const;
    bool refreshOnlineBookingPlanningText(const BookingRecord& booking, QString* reason = nullptr) const;
    QStringList freeSlotsForDate(const QDate& day) const;
};
