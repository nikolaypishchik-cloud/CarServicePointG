#include "AppVersion.h"
#include "core/backup/CrashRecoveryManager.h"

#include "runtime/AppRuntime.h"
#include "core/backup/BackupLogging.h"
#include "core/backup/BackupPaths.h"
#include "core/backup/BackupPolicy.h"
#include "core/backup/RecoveryDraftStore.h"
#include "core/backup/DataProtectionCoordinator.h"
#include "infrastructure/db/sqlite/Db.h"
#include "infrastructure/audit/BackupRecoveryAuditTrail.h"

#include <QCryptographicHash>
#include <QDate>
#include <QSet>
#include <QRegularExpression>
#include <QDateTime>
#include <QFileInfo>
#include <QJsonDocument>
#include <QDir>
#include <QUuid>
#include <utility>

namespace {

static RecoveryDraftParticipant* participantForObject(QObject* object)
{
    return object ? dynamic_cast<RecoveryDraftParticipant*>(object) : nullptr;
}

}

CrashRecoveryManager::CrashRecoveryManager(QObject* parent)
    : QObject(parent)
{
    connect(&m_heartbeatTimer, &QTimer::timeout, this, &CrashRecoveryManager::updateHeartbeat);
    connect(&m_autosaveTimer, &QTimer::timeout, this, [this]() { autosaveDirtyDrafts(); });
}

void CrashRecoveryManager::start()
{
    BackupPolicy::cleanupRecovery(nullptr);
    detectPreviousRecoverableSession();

    m_currentSession.sessionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_currentSession.appStartTime = QDateTime::currentDateTime();
    m_currentSession.lastHeartbeat = m_currentSession.appStartTime;
    m_currentSession.cleanShutdown = false;
    m_currentSession.dirtyState = false;
    m_currentSession.appVersion = CarService::AppVersion::versionString();
    m_currentSession.schemaVersion = Database::getSetting(QStringLiteral("app.schema.version"), QStringLiteral("runtime")).toString();
    m_currentSession.sessionDir = BackupPaths::recoverySessionDir(m_currentSession.sessionId);
    persistSessionState();

    m_heartbeatTimer.start(5000);
    reloadPolicy();
    BackupLogging::recoveryInfo(QStringLiteral("session started %1").arg(m_currentSession.sessionId));
    BackupRecoveryAuditTrail::appendRecoveryEvent(QStringLiteral("started"),
                                                  QStringLiteral("recovery_session"),
                                                  m_currentSession.sessionId,
                                                  QStringLiteral("CrashRecoveryManager"),
                                                  QStringLiteral("recovery_session"),
                                                  m_currentSession.sessionId,
                                                  QStringLiteral("crash recovery session started"));
}

void CrashRecoveryManager::reloadPolicy()
{
    const BackupPolicy::Config cfg = BackupPolicy::load();
    if (!cfg.recoveryEnabled || !cfg.recoveryAutosaveEnabled) {
        m_autosaveTimer.stop();
        return;
    }
    m_autosaveTimer.setInterval(cfg.recoveryAutosaveIntervalMs);
    if (!m_autosaveTimer.isActive()) {
        m_autosaveTimer.start();
    }
}

bool CrashRecoveryManager::flushDirtyDraftsBeforeCriticalOperation(QString* errorText)
{
    const bool ok = autosaveDirtyDrafts(false, errorText);
    BackupRecoveryAuditTrail::appendRecoveryEvent(ok ? QStringLiteral("completed") : QStringLiteral("failed"),
                                                  QStringLiteral("draft_autosave_before_critical_operation"),
                                                  m_currentSession.sessionId,
                                                  QStringLiteral("CrashRecoveryManager"),
                                                  QStringLiteral("recovery_session"),
                                                  m_currentSession.sessionId,
                                                  QStringLiteral("recovery drafts flushed before DB close/restore"),
                                                  ok || !errorText ? QString() : *errorText);
    return ok;
}

void CrashRecoveryManager::markCleanShutdown()
{
    m_autosaveTimer.stop();
    m_heartbeatTimer.stop();
    QString autosaveError;
    const bool autosaveOk = autosaveDirtyDrafts(true, &autosaveError);
    if (!autosaveOk) {
        m_currentSession.cleanShutdown = false;
        m_currentSession.dirtyState = true;
        m_currentSession.lastHeartbeat = QDateTime::currentDateTime();
        persistSessionState();
        BackupLogging::recoveryWarn(QStringLiteral("clean shutdown refused because draft autosave failed %1").arg(m_currentSession.sessionId));
        BackupRecoveryAuditTrail::appendRecoveryEvent(QStringLiteral("failed"),
                                                      QStringLiteral("recovery_session"),
                                                      m_currentSession.sessionId,
                                                      QStringLiteral("CrashRecoveryManager"),
                                                      QStringLiteral("recovery_session"),
                                                      m_currentSession.sessionId,
                                                      QStringLiteral("clean shutdown refused because recovery draft autosave failed"),
                                                      autosaveError);
        return;
    }
    m_currentSession.cleanShutdown = true;
    m_currentSession.lastHeartbeat = QDateTime::currentDateTime();
    persistSessionState();
    BackupLogging::recoveryInfo(QStringLiteral("clean shutdown %1").arg(m_currentSession.sessionId));
    BackupRecoveryAuditTrail::appendRecoveryEvent(QStringLiteral("completed"),
                                                  QStringLiteral("recovery_session"),
                                                  m_currentSession.sessionId,
                                                  QStringLiteral("CrashRecoveryManager"),
                                                  QStringLiteral("recovery_session"),
                                                  m_currentSession.sessionId,
                                                  QStringLiteral("recovery session finished with clean shutdown"));
}

void CrashRecoveryManager::registerParticipant(RecoveryDraftParticipant* participant)
{
    QObject* object = participant ? participant->draftQObject() : nullptr;
    if (!participant || !object) return;
    for (const Entry& e : std::as_const(m_entries)) {
        if (e.object == object) return;
    }
    Entry e;
    e.object = object;
    m_entries.push_back(e);
    connect(object, &QObject::destroyed, this, [this, object]() {
        for (int i = m_entries.size() - 1; i >= 0; --i) {
            if (m_entries.at(i).object == object) {
                m_entries.removeAt(i);
            }
        }
    });
}

void CrashRecoveryManager::unregisterParticipant(RecoveryDraftParticipant* participant)
{
    QObject* object = participant ? participant->draftQObject() : nullptr;
    for (int i = m_entries.size() - 1; i >= 0; --i) {
        const QObject* entryObject = m_entries.at(i).object.data();
        const RecoveryDraftParticipant* entryParticipant = participantForObject(m_entries.at(i).object.data());
        if ((object && entryObject == object) || (!object && entryParticipant == participant)) {
            m_entries.removeAt(i);
        }
    }
}

void CrashRecoveryManager::notifyParticipantDirty(RecoveryDraftParticipant* participant)
{
    if (!participant) return;
    QObject* object = participant->draftQObject();
    if (!object) return;
    participant = participantForObject(object);
    if (!participant) return;
    const BackupPolicy::Config cfg = BackupPolicy::load();
    if (!cfg.recoveryEnabled) return;

    m_currentSession.activeModule = participant->recoveryModule();
    if (participant->isDraftDirty()) {
        m_currentSession.dirtyState = true;
        BackupLogging::recoveryInfo(QStringLiteral("dirty state set module=%1 key=%2")
                                        .arg(participant->recoveryModule(), participant->draftEntityKey()));
        if (!m_autosaveTimer.isActive() && cfg.recoveryAutosaveEnabled) {
            m_autosaveTimer.start(cfg.recoveryAutosaveIntervalMs);
        }
    } else {
        const QVector<DraftPayload> drafts = RecoveryDraftStore::listDrafts(m_currentSession.sessionDir);
        const QString draftId = buildPayload(participant).draftId;
        for (const DraftPayload& draft : drafts) {
            if (draft.draftId == draftId) {
                RecoveryDraftStore::removeDraft(draft, nullptr);
            }
        }
        BackupLogging::recoveryInfo(QStringLiteral("dirty state cleared key=%1").arg(participant->draftEntityKey()));
        m_currentSession.draftCount = RecoveryDraftStore::listDrafts(m_currentSession.sessionDir).size();
        m_currentSession.dirtyState = m_currentSession.draftCount > 0;
    }
    persistSessionState();
}

bool CrashRecoveryManager::hasRecoverableSession() const
{
    return !m_recoverableSession.sessionId.trimmed().isEmpty() && !m_recoverableDrafts.isEmpty();
}

RecoverySessionState CrashRecoveryManager::recoverableSession() const
{
    return m_recoverableSession;
}

QVector<DraftPayload> CrashRecoveryManager::recoverableDrafts() const
{
    return m_recoverableDrafts;
}

RecoverySessionState CrashRecoveryManager::currentSession() const
{
    return m_currentSession;
}

void CrashRecoveryManager::cleanupExpiredData(QString* errorText)
{
    BackupPolicy::cleanupRecovery(errorText);
}


void CrashRecoveryManager::refreshRecoverableSessionState()
{
    BackupPolicy::cleanupRecovery(nullptr);
    detectPreviousRecoverableSession();
}

void CrashRecoveryManager::forgetRecoverableSession()
{
    m_recoverableSession = RecoverySessionState();
    m_recoverableDrafts.clear();
    emit recoverableSessionChanged();
}

void CrashRecoveryManager::updateHeartbeat()
{
    m_currentSession.lastHeartbeat = QDateTime::currentDateTime();
    persistSessionState();
    BackupLogging::recoveryInfo(QStringLiteral("heartbeat updated %1").arg(m_currentSession.lastHeartbeat.toString(Qt::ISODateWithMs)));
}

bool CrashRecoveryManager::autosaveDirtyDrafts(bool acquireGate, QString* errorText)
{
    const BackupPolicy::Config cfg = BackupPolicy::load();
    if (!cfg.recoveryEnabled || !cfg.recoveryAutosaveEnabled) return true;

    QString gateError;
    DataProtectionCoordinator::Scope recoveryGate;
    if (acquireGate) {
        recoveryGate = DataProtectionCoordinator::tryEnter(DataProtectionCoordinator::Operation::RecoveryDraftAutosave,
                                                           QStringLiteral("recovery draft autosave"),
                                                           &gateError,
                                                           0);
        if (!recoveryGate.isActive()) {
            BackupLogging::recoveryWarn(QStringLiteral("draft autosave deferred by data protection gate %1").arg(gateError));
            if (errorText) *errorText = gateError;
            return false;
        }
    }

    bool anyDirty = false;
    bool allWritesOk = true;
    QVector<DraftPayload> existing = RecoveryDraftStore::listDrafts(m_currentSession.sessionDir);
    QSet<QString> activeDraftIds;
    for (const Entry& e : std::as_const(m_entries)) {
        QObject* object = e.object.data();
        RecoveryDraftParticipant* participant = participantForObject(object);
        if (!object || !participant) continue;
        const QString expectedDraftId = buildPayload(participant).draftId;
        activeDraftIds.insert(expectedDraftId);
        if (!participant->isDraftDirty()) {
            for (const DraftPayload& draft : existing) {
                if (draft.draftId == expectedDraftId) {
                    RecoveryDraftStore::removeDraft(draft, nullptr);
                    BackupLogging::recoveryInfo(QStringLiteral("dirty state cleared %1").arg(draft.draftId));
                }
            }
            continue;
        }
        anyDirty = true;
        DraftPayload payload = buildPayload(participant);
        QString draftError;
        if (RecoveryDraftStore::saveDraft(&payload, m_currentSession.sessionDir, &draftError)) {
            BackupLogging::recoveryInfo(QStringLiteral("draft saved %1").arg(payload.draftId));
            BackupRecoveryAuditTrail::appendRecoveryEvent(QStringLiteral("completed"),
                                                          QStringLiteral("draft_autosave"),
                                                          payload.draftId,
                                                          QStringLiteral("CrashRecoveryManager"),
                                                          payload.entityType,
                                                          payload.entityId,
                                                          QStringLiteral("recovery draft autosaved"));
        } else {
            allWritesOk = false;
            BackupLogging::recoveryWarn(QStringLiteral("draft save failed %1").arg(draftError));
            if (errorText && errorText->trimmed().isEmpty()) *errorText = draftError;
            BackupRecoveryAuditTrail::appendRecoveryEvent(QStringLiteral("failed"),
                                                          QStringLiteral("draft_autosave"),
                                                          payload.draftId,
                                                          QStringLiteral("CrashRecoveryManager"),
                                                          payload.entityType,
                                                          payload.entityId,
                                                          QStringLiteral("recovery draft autosave failed"),
                                                          draftError);
        }
    }

    // Remove abandoned drafts for objects that no longer exist in the current session.
    existing = RecoveryDraftStore::listDrafts(m_currentSession.sessionDir);
    for (const DraftPayload& draft : existing) {
        if (!activeDraftIds.contains(draft.draftId) && !draft.draftId.trimmed().isEmpty()) {
            RecoveryDraftStore::removeDraft(draft, nullptr);
        }
    }

    m_currentSession.dirtyState = anyDirty;
    m_currentSession.draftCount = RecoveryDraftStore::listDrafts(m_currentSession.sessionDir).size();
    if (anyDirty) {
        m_currentSession.lastDraftWrite = QDateTime::currentDateTime();
    }
    persistSessionState();
    if (allWritesOk && errorText) errorText->clear();
    return allWritesOk;
}

DraftPayload CrashRecoveryManager::buildPayload(RecoveryDraftParticipant* participant) const
{
    DraftPayload p;
    p.recoverySessionId = m_currentSession.sessionId;
    p.draftId = participant->recoveryFormType() + QStringLiteral("_") + participant->draftEntityKey();
    p.draftId.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_-]")), QStringLiteral("_"));
    p.module = participant->recoveryModule();
    p.formType = participant->recoveryFormType();
    p.entityType = participant->recoveryEntityType();
    p.entityId = participant->recoveryEntityId();
    p.orderUid = participant->recoveryOrderUid();
    p.createdAt = QFileInfo(QDir(m_currentSession.sessionDir).filePath(p.draftId + QStringLiteral(".json"))).exists()
        ? QFileInfo(QDir(m_currentSession.sessionDir).filePath(p.draftId + QStringLiteral(".json"))).birthTime()
        : QDateTime::currentDateTime();
    p.updatedAt = QDateTime::currentDateTime();
    p.committedBaseVersion = participant->committedBaseVersion();
    p.payloadSchemaVersion = QStringLiteral("1");
    p.appVersion = CarService::AppVersion::versionString();
    p.schemaVersion = m_currentSession.schemaVersion;
    p.baseEntityVersion = p.committedBaseVersion;
    p.baseUpdatedAt = p.createdAt;
    p.payloadData = participant->serializeDraftState();
    p.payloadHash = QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(p.payloadData).toJson(QJsonDocument::Compact),
                                                                 QCryptographicHash::Sha256).toHex());
    p.validationState = QStringLiteral("valid");
    p.conflictState = QStringLiteral("unknown");
    return p;
}

void CrashRecoveryManager::persistSessionState()
{
    RecoveryDraftStore::saveSessionState(m_currentSession, nullptr);
}

void CrashRecoveryManager::detectPreviousRecoverableSession()
{
    m_recoverableSession = RecoverySessionState();
    m_recoverableDrafts.clear();

    const QVector<RecoverySessionState> sessions = RecoveryDraftStore::listSessions();
    for (const RecoverySessionState& session : sessions) {
        if (session.cleanShutdown) continue;
        const QVector<DraftPayload> drafts = RecoveryDraftStore::listDrafts(session.sessionDir);
        if (drafts.isEmpty()) continue;
        m_recoverableSession = session;
        m_recoverableDrafts = drafts;
        BackupLogging::recoveryWarn(QStringLiteral("abnormal shutdown detected session=%1 drafts=%2").arg(session.sessionId).arg(drafts.size()));
        BackupRecoveryAuditTrail::appendRecoveryEvent(QStringLiteral("deferred"),
                                                      QStringLiteral("recoverable_session_detected"),
                                                      session.sessionId,
                                                      QStringLiteral("CrashRecoveryManager"),
                                                      QStringLiteral("recovery_session"),
                                                      session.sessionId,
                                                      QStringLiteral("recoverable session detected and deferred for user decision"));
        emit recoverableSessionChanged();
        return;
    }
}
