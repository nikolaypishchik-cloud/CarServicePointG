#include "core/backup/BackupManifestService.h"

#include "core/backup/BackupLogging.h"
#include "core/backup/BackupPaths.h"

#include <QDate>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QSaveFile>

namespace {
QVector<SnapshotMetadata> readIndex()
{
    QVector<SnapshotMetadata> items;
    QFile f(BackupPaths::backupIndexPath());
    if (!f.exists()) return items;
    if (!f.open(QIODevice::ReadOnly)) return items;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    const QJsonArray arr = doc.object().value(QStringLiteral("entries")).toArray();
    for (const QJsonValue& v : arr) {
        if (v.isObject()) items.push_back(SnapshotMetadata::fromJson(v.toObject()));
    }
    return items;
}

bool writeJsonFile(const QString& path, const QJsonObject& o, QString* errorText)
{
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (errorText) *errorText = f.errorString();
        return false;
    }
    f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    if (!f.commit()) {
        if (errorText) *errorText = f.errorString();
        return false;
    }
    return true;
}

void writeManifest(const QVector<SnapshotMetadata>& items)
{
    QMap<QString, QPair<int, qint64>> perDay;
    for (const SnapshotMetadata& meta : items) {
        const QString day = meta.createdAt.date().toString(Qt::ISODate);
        auto it = perDay.find(day);
        if (it == perDay.end()) {
            perDay.insert(day, qMakePair(1, meta.snapshotSizeBytes));
        } else {
            it->first += 1;
            it->second += meta.snapshotSizeBytes;
        }
    }

    QJsonArray days;
    for (auto it = perDay.cbegin(); it != perDay.cend(); ++it) {
        days.push_back(QJsonObject{
            {QStringLiteral("day"), it.key()},
            {QStringLiteral("count"), it.value().first},
            {QStringLiteral("sizeBytes"), QString::number(it.value().second)}
        });
    }
    writeJsonFile(BackupPaths::backupManifestPath(), QJsonObject{{QStringLiteral("days"), days}}, nullptr);
}
}

QVector<SnapshotMetadata> BackupManifestService::listSnapshots()
{
    return readIndex();
}

bool BackupManifestService::appendSnapshot(const SnapshotMetadata& meta, QString* errorText)
{
    QVector<SnapshotMetadata> items = readIndex();
    items.push_back(meta);
    return replaceSnapshotIndex(items, errorText);
}

bool BackupManifestService::replaceSnapshotIndex(const QVector<SnapshotMetadata>& entries, QString* errorText)
{
    QJsonArray arr;
    for (const SnapshotMetadata& meta : entries) {
        arr.push_back(meta.toJson());
    }
    const bool ok = writeJsonFile(BackupPaths::backupIndexPath(), QJsonObject{{QStringLiteral("entries"), arr}}, errorText);
    if (ok) {
        writeManifest(entries);
    }
    return ok;
}

bool BackupManifestService::reconcile(QString* errorText)
{
    QVector<SnapshotMetadata> items = readIndex();
    QVector<SnapshotMetadata> valid;
    for (SnapshotMetadata meta : items) {
        if (meta.snapshotPath.trimmed().isEmpty()) continue;
        QFileInfo fi(meta.snapshotPath);
        if (!fi.exists() || fi.size() <= 0) continue;
        meta.snapshotSizeBytes = fi.size();
        valid.push_back(meta);
    }
    return replaceSnapshotIndex(valid, errorText);
}
