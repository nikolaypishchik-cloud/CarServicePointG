#include "core/services/settings/OutsourceDirectorySettingsService.h"

#include "infrastructure/db/sqlite/Db.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace OutsourceDirectorySettingsService {
namespace {
QJsonObject toJson(const OutsourceEntry& e)
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
}


QList<OutsourceEntry> loadEntries()
{
    QList<OutsourceEntry> out;
    const QString raw = Database::getSetting(QStringLiteral("settings.out.items"), QString()).toString().trimmed();
    if (raw.isEmpty()) return out;
    const auto doc = QJsonDocument::fromJson(raw.toUtf8());
    if (!doc.isArray()) return out;
    for (const auto& v : doc.array()) {
        if (!v.isObject()) continue;
        const auto o = v.toObject();
        OutsourceEntry e;
        e.id = o.value(QStringLiteral("id")).toString();
        e.company = o.value(QStringLiteral("company")).toString();
        e.address = o.value(QStringLiteral("address")).toString();
        e.contact = o.value(QStringLiteral("contact")).toString();
        e.branch = o.value(QStringLiteral("branch")).toString();
        e.note = o.value(QStringLiteral("note")).toString();
        e.markup = o.value(QStringLiteral("markup")).toDouble(20.0);
        out.push_back(e);
    }
    return out;
}

bool saveEntries(const QList<OutsourceEntry>& entries, bool* changed)
{
    QJsonArray arr;
    for (const auto& e : entries) arr.push_back(toJson(e));
    const QString newJson = QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
    const QString current = Database::getSetting(QStringLiteral("settings.out.items"), QString()).toString();
    const bool hasChanges = current.trimmed() != newJson.trimmed();
    if (changed) *changed = hasChanges;
    Database::setSetting(QStringLiteral("settings.out.items"), newJson);
    return true;
}

} // namespace OutsourceDirectorySettingsService
