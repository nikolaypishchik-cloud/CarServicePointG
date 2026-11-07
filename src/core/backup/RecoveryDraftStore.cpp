#include "runtime/AppRuntime.h"
#include "core/backup/RecoveryDraftStore.h"
#include "core/io/SafeFileOps.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QHash>
#include <algorithm>

namespace {
QString statePathForDir(const QString& sessionDir)
{
    return QDir(sessionDir).filePath(QStringLiteral("session_state.json"));
}

QString manifestPathForDir(const QString& sessionDir)
{
    return QDir(sessionDir).filePath(QStringLiteral("recovery_manifest.json"));
}

bool writeJson(const QString& path, const QJsonObject& o, QString* errorText)
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

QJsonObject readJson(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}

QString resolveSessionDirForDraft(const DraftPayload& payload)
{
    if (!payload.filePath.trimmed().isEmpty()) {
        return QFileInfo(payload.filePath).absolutePath();
    }

    QDir root(AppRuntime::recoveryDir());
    const QFileInfoList dayDirs = root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time);
    for (const QFileInfo& dayFi : dayDirs) {
        QDir day(dayFi.absoluteFilePath());
        const QFileInfoList sessionDirs = day.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time);
        for (const QFileInfo& sessionFi : sessionDirs) {
            if (sessionFi.fileName() == QStringLiteral("session_%1").arg(payload.recoverySessionId)) {
                return sessionFi.absoluteFilePath();
            }
        }
    }
    return QString();
}

QVector<DraftPayload> readDraftFilesFromDirectory(const QString& sessionDir)
{
    QVector<DraftPayload> items;
    QDir dir(sessionDir);
    const QFileInfoList files = dir.entryInfoList(QStringList() << QStringLiteral("*.json"), QDir::Files, QDir::Time);
    for (const QFileInfo& fi : files) {
        if (fi.fileName() == QStringLiteral("session_state.json") || fi.fileName() == QStringLiteral("recovery_manifest.json")) {
            continue;
        }
        const QJsonObject object = readJson(fi.absoluteFilePath());
        DraftPayload payload;
        if (object.isEmpty()) {
            payload.draftId = fi.completeBaseName();
            payload.validationState = QStringLiteral("invalid");
            payload.conflictState = QStringLiteral("unknown");
        } else {
            payload = DraftPayload::fromJson(object);
        }
        payload.filePath = fi.absoluteFilePath();
        payload.fileSizeBytes = fi.size();
        if (payload.draftId.isEmpty()) {
            payload.draftId = fi.completeBaseName();
        }
        items.push_back(payload);
    }
    return items;
}
}

bool RecoveryDraftStore::saveSessionState(const RecoverySessionState& state, QString* errorText)
{
    QDir().mkpath(state.sessionDir);
    return writeJson(statePathForDir(state.sessionDir), state.toJson(), errorText);
}

bool RecoveryDraftStore::loadSessionState(const QString& sessionDir, RecoverySessionState* state, QString* errorText)
{
    const QJsonObject o = readJson(statePathForDir(sessionDir));
    if (o.isEmpty()) {
        if (errorText) *errorText = QStringLiteral("session_state.json не знайдено або порожній");
        return false;
    }
    if (state) *state = RecoverySessionState::fromJson(o);
    return true;
}

QVector<RecoverySessionState> RecoveryDraftStore::listSessions()
{
    QVector<RecoverySessionState> sessions;
    QDir root(AppRuntime::recoveryDir());
    const QFileInfoList dayDirs = root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time);
    for (const QFileInfo& dayFi : dayDirs) {
        QDir day(dayFi.absoluteFilePath());
        const QFileInfoList sessionDirs = day.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time);
        for (const QFileInfo& sessionFi : sessionDirs) {
            RecoverySessionState state;
            const QString sessionDir = sessionFi.absoluteFilePath();
            if (loadSessionState(sessionDir, &state, nullptr)) {
                sessions.push_back(state);
                continue;
            }

            // Defensive recovery: do not lose orphan draft files when session_state.json
            // or the manifest was not written before a crash/shutdown.
            const QVector<DraftPayload> drafts = listDrafts(sessionDir);
            if (!drafts.isEmpty()) {
                state.sessionDir = sessionDir;
                state.sessionId = sessionFi.fileName().startsWith(QStringLiteral("session_"))
                    ? sessionFi.fileName().mid(QStringLiteral("session_").size())
                    : sessionFi.fileName();
                state.cleanShutdown = false;
                state.dirtyState = true;
                state.draftCount = drafts.size();
                state.lastHeartbeat = sessionFi.lastModified();
                state.lastDraftWrite = drafts.first().updatedAt.isValid() ? drafts.first().updatedAt : sessionFi.lastModified();
                sessions.push_back(state);
            }
        }
    }
    std::sort(sessions.begin(), sessions.end(), [](const RecoverySessionState& a, const RecoverySessionState& b) {
        return a.lastHeartbeat > b.lastHeartbeat;
    });
    return sessions;
}

bool RecoveryDraftStore::saveDraft(DraftPayload* payload, const QString& sessionDir, QString* errorText)
{
    if (!payload) return false;
    QDir().mkpath(sessionDir);
    payload->filePath = QDir(sessionDir).filePath(payload->draftId + QStringLiteral(".json"));
    payload->fileSizeBytes = 0;
    if (!writeJson(payload->filePath, payload->toJson(), errorText)) {
        return false;
    }
    payload->fileSizeBytes = QFileInfo(payload->filePath).size();

    QJsonArray arr;
    const QVector<DraftPayload> items = listDrafts(sessionDir);
    bool replaced = false;
    for (const DraftPayload& item : items) {
        if (item.draftId == payload->draftId) {
            arr.push_back(payload->toJson());
            replaced = true;
        } else {
            arr.push_back(item.toJson());
        }
    }
    if (!replaced) arr.push_back(payload->toJson());
    return writeJson(manifestPathForDir(sessionDir), QJsonObject{{QStringLiteral("drafts"), arr}}, errorText);
}

QVector<DraftPayload> RecoveryDraftStore::listDrafts(const QString& sessionDir)
{
    QHash<QString, DraftPayload> byId;
    const QJsonObject manifest = readJson(manifestPathForDir(sessionDir));
    const QJsonArray arr = manifest.value(QStringLiteral("drafts")).toArray();
    for (const QJsonValue& v : arr) {
        if (!v.isObject()) continue;
        DraftPayload p = DraftPayload::fromJson(v.toObject());
        if (p.draftId.trimmed().isEmpty()) continue;
        p.filePath = QDir(sessionDir).filePath(p.draftId + QStringLiteral(".json"));
        if (QFileInfo::exists(p.filePath)) {
            p.fileSizeBytes = QFileInfo(p.filePath).size();
            byId.insert(p.draftId, p);
        } else {
            p.validationState = QStringLiteral("invalid");
            p.conflictState = QStringLiteral("missing_file");
            byId.insert(p.draftId, p);
        }
    }

    const QVector<DraftPayload> scanned = readDraftFilesFromDirectory(sessionDir);
    for (DraftPayload p : scanned) {
        if (p.draftId.trimmed().isEmpty()) continue;
        // File scan wins over manifest metadata so orphan and newer draft files are never hidden by stale manifest.
        byId.insert(p.draftId, p);
    }

    QVector<DraftPayload> items;
    items.reserve(byId.size());
    for (auto it = byId.cbegin(); it != byId.cend(); ++it) items.push_back(it.value());
    std::sort(items.begin(), items.end(), [](const DraftPayload& a, const DraftPayload& b) {
        return a.updatedAt > b.updatedAt;
    });
    return items;
}

bool RecoveryDraftStore::removeDraft(const DraftPayload& payload, QString* errorText)
{
    const QString sessionDir = resolveSessionDirForDraft(payload);
    if (sessionDir.trimmed().isEmpty()) {
        if (errorText) *errorText = QStringLiteral("Не вдалося визначити session dir для видалення draft");
        return false;
    }

    const QString resolvedFilePath = !payload.filePath.trimmed().isEmpty()
        ? payload.filePath
        : QDir(sessionDir).filePath(payload.draftId + QStringLiteral(".json"));
    SafeFileOps::safeRemoveFile(resolvedFilePath, {sessionDir}, {}, {QStringLiteral(".json")});

    QVector<DraftPayload> items = listDrafts(sessionDir);
    QVector<DraftPayload> keep;
    for (const DraftPayload& item : items) {
        if (item.draftId != payload.draftId) keep.push_back(item);
    }
    QJsonArray arr;
    for (const DraftPayload& item : keep) arr.push_back(item.toJson());
    return writeJson(manifestPathForDir(sessionDir), QJsonObject{{QStringLiteral("drafts"), arr}}, errorText);
}

bool RecoveryDraftStore::clearSession(const QString& sessionDir, QString* errorText)
{
    if (!SafeFileOps::safeRemoveRecursively(sessionDir, {AppRuntime::recoveryDir()}, {})) {
        if (errorText) {
            *errorText = QStringLiteral("Не вдалося видалити recovery session: %1").arg(QDir::toNativeSeparators(sessionDir));
        }
        return false;
    }
    return true;
}
