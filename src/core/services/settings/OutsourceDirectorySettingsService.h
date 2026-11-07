#pragma once

#include <QString>
#include <QList>

namespace OutsourceDirectorySettingsService {

struct OutsourceEntry {
    QString id;
    QString company;
    QString address;
    QString contact;
    QString branch;
    QString note;
    double markup = 20.0;
};

QList<OutsourceEntry> loadEntries();
bool saveEntries(const QList<OutsourceEntry>& entries, bool* changed = nullptr);

} // namespace OutsourceDirectorySettingsService
