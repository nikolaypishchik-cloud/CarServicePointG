#include "ui/tabs/settings/SettingsPageDataSupport.h"

#include "core/backup/BackupCommitPublisher.h"
#include "core/services/settings/BrandingAssetsSettingsService.h"
#include "core/services/settings/OutsourceDirectorySettingsService.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDateTime>
#include <QUuid>

namespace SettingsPageDataSupport {

namespace {
void publishSettingsBackupEventImpl(const QString& operationType,
                                    const QString& entityType,
                                    const QString& entityId,
                                    const QString& description,
                                    const QStringList& changedTables)
{
    BackupCommitPublisher::publish(
        BackupCommitPublisher::makeEvent(QStringLiteral("SettingsPage"),
                                         QStringLiteral("settings"),
                                         entityType,
                                         entityId,
                                         QString(),
                                         0,
                                         operationType,
                                         description,
                                         changedTables),
        nullptr);
}
}

void publishSettingsBackupEvent(const QString& operationType,
                                const QString& entityType,
                                const QString& entityId,
                                const QString& description,
                                const QStringList& changedTables)
{
    publishSettingsBackupEventImpl(operationType, entityType, entityId, description, changedTables);
}


static QJsonObject outsourceToJson(const OutsourceEntry& e)
{
    QJsonObject o;
    o["id"] = e.id;
    o["company"] = e.company;
    o["address"] = e.address;
    o["contact"] = e.contact;
    o["branch"] = e.branch;
    o["note"] = e.note;
    o["markup"] = e.markup;
    return o;
}

static OutsourceEntry outsourceFromJson(const QJsonObject& o)
{
    OutsourceEntry e;
    e.id = o.value("id").toString();
    if (e.id.isEmpty()) e.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    e.company = o.value("company").toString();
    e.address = o.value("address").toString();
    e.contact = o.value("contact").toString();
    e.branch = o.value("branch").toString();
    e.note = o.value("note").toString();
    e.markup = o.value("markup").toDouble(20.0);
    return e;
}

QList<OutsourceEntry> loadOutsourceEntries()
{
    QList<OutsourceEntry> out;
    const auto loaded = OutsourceDirectorySettingsService::loadEntries();
    out.reserve(loaded.size());
    for (const auto& row : loaded) {
        OutsourceEntry e;
        e.id = row.id;
        e.company = row.company;
        e.address = row.address;
        e.contact = row.contact;
        e.branch = row.branch;
        e.note = row.note;
        e.markup = row.markup;
        out.push_back(e);
    }
    if (out.isEmpty()) {
        out.push_back({QUuid::createUuid().toString(QUuid::WithoutBraces), QString::fromUtf8("Ремонт генераторів"), QString(), QString(), QString(), QString(), 20.0});
        out.push_back({QUuid::createUuid().toString(QUuid::WithoutBraces), QString::fromUtf8("Ремонт стартерів"), QString(), QString(), QString(), QString(), 20.0});
        out.push_back({QUuid::createUuid().toString(QUuid::WithoutBraces), QString::fromUtf8("Ремонт форсунок"), QString(), QString(), QString(), QString(), 20.0});
    }
    return out;
}

void saveOutsourceEntries(const QList<OutsourceEntry>& entries)
{
    QList<OutsourceDirectorySettingsService::OutsourceEntry> payload;
    payload.reserve(entries.size());
    for (const auto& e : entries) {
        OutsourceDirectorySettingsService::OutsourceEntry row;
        row.id = e.id;
        row.company = e.company;
        row.address = e.address;
        row.contact = e.contact;
        row.branch = e.branch;
        row.note = e.note;
        row.markup = e.markup;
        payload.push_back(row);
    }
    bool changed = false;
    OutsourceDirectorySettingsService::saveEntries(payload, &changed);
    if (changed) {
        publishSettingsBackupEventImpl(QStringLiteral("outsource_directory_save"),
                                   QStringLiteral("outsource_directory"),
                                   QStringLiteral("settings.out.items"),
                                   QString::fromUtf8("Збережено довідник аутсорсу (%1 записів)").arg(entries.size()),
                                   {QStringLiteral("settings")});
    }
}

} // namespace SettingsPageDataSupport
