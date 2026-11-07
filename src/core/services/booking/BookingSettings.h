#pragma once

#include <QString>

struct BookingSyncSettings
{
    bool enabled = true;
    QString baseUrl;
    QString token;
    int pollIntervalMs = 10000;
    int workStartHour = 9;
    int workEndHour = 18;
    int slotMinutes = 30;
    int capacityPerSlot = 1;
    bool longTermBlocksDay = true;
    QString workDaysCsv = QStringLiteral("1,2,3,4,5,6");
};

class BookingSettings
{
public:
    static BookingSyncSettings load();
    static bool save(const BookingSyncSettings& settings);
};
