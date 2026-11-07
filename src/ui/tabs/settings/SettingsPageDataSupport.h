#pragma once

#include <QList>
#include <QString>
#include <QStringList>

struct OutsourceEntry {
    QString id;
    QString company;
    QString address;
    QString contact;
    QString branch;
    QString note;
    double markup = 20.0;
};

namespace SettingsPageDataSupport {


QList<OutsourceEntry> loadOutsourceEntries();
void saveOutsourceEntries(const QList<OutsourceEntry>& entries);

void publishSettingsBackupEvent(const QString& operationType,
                                const QString& entityType,
                                const QString& entityId,
                                const QString& description,
                                const QStringList& changedTables);

} // namespace SettingsPageDataSupport
