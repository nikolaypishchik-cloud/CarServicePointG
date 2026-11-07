#include "core/backup/DataProtectionCoordinator.h"

#include "core/backup/BackupLogging.h"
#include "infrastructure/audit/BackupRecoveryAuditTrail.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QMutex>
#include <QMutexLocker>
#include <QThread>
#include <QWaitCondition>
#include <QUuid>
#include <utility>

namespace {
QMutex& coordinatorMutex()
{
    static QMutex mutex;
    return mutex;
}

QString& activeOperationId()
{
    static QString value;
    return value;
}

QString& activeOperationName()
{
    static QString value;
    return value;
}

QString& activeState()
{
    static QString value = QStringLiteral("Idle");
    return value;
}

QString& degradedReason()
{
    static QString value;
    return value;
}

Qt::HANDLE& activeThreadId()
{
    static Qt::HANDLE value = nullptr;
    return value;
}

void boundedCoordinatorPause(int milliseconds)
{
    QMutex pauseMutex;
    QWaitCondition pauseCondition;
    pauseMutex.lock();
    pauseCondition.wait(&pauseMutex, qMax(1, milliseconds));
    pauseMutex.unlock();
}
}

DataProtectionCoordinator::Scope::Scope(Operation operation, const QString& operationId, bool active)
    : m_operation(operation)
    , m_operationId(operationId)
    , m_active(active)
{
}

DataProtectionCoordinator::Scope::Scope(Scope&& other) noexcept
    : m_operation(other.m_operation)
    , m_operationId(std::move(other.m_operationId))
    , m_active(other.m_active)
{
    other.m_active = false;
}

DataProtectionCoordinator::Scope& DataProtectionCoordinator::Scope::operator=(Scope&& other) noexcept
{
    if (this != &other) {
        release();
        m_operation = other.m_operation;
        m_operationId = std::move(other.m_operationId);
        m_active = other.m_active;
        other.m_active = false;
    }
    return *this;
}

DataProtectionCoordinator::Scope::~Scope()
{
    release();
}

void DataProtectionCoordinator::Scope::setState(const QString& state)
{
    if (m_active) DataProtectionCoordinator::setStateForCurrentOperation(m_operationId, state);
}

void DataProtectionCoordinator::Scope::markDegraded(const QString& reason)
{
    if (m_active) DataProtectionCoordinator::markDegradedForCurrentOperation(m_operationId, reason);
}

void DataProtectionCoordinator::Scope::clearDegraded()
{
    if (m_active) DataProtectionCoordinator::clearDegradedForCurrentOperation(m_operationId);
}

void DataProtectionCoordinator::Scope::release()
{
    if (!m_active) return;
    {
        QMutexLocker locker(&coordinatorMutex());
        if (activeOperationId() == m_operationId) {
            BackupRecoveryAuditTrail::appendRecoveryEvent(QStringLiteral("completed"),
                                                          QStringLiteral("data_protection_operation"),
                                                          m_operationId,
                                                          QStringLiteral("DataProtectionCoordinator"),
                                                          operationName(m_operation),
                                                          m_operationId,
                                                          QStringLiteral("data protection gate released state=%1").arg(activeState()));
            activeOperationId().clear();
            activeOperationName().clear();
            activeState() = QStringLiteral("Idle");
            degradedReason().clear();
            activeThreadId() = nullptr;
        }
    }
    m_active = false;
}

DataProtectionCoordinator::Scope DataProtectionCoordinator::tryEnter(Operation operation,
                                                                      const QString& reason,
                                                                      QString* errorText,
                                                                      int timeoutMs)
{
    QElapsedTimer waitTimer;
    waitTimer.start();

    auto rejectBusy = [&](const QString& activeStateText, const QString& activeOperationText) {
        const QString error = QStringLiteral("Data protection busy: requested=%1 state=%2 activeOperation=%3")
                                  .arg(operationName(operation), activeStateText, activeOperationText);
        if (errorText) *errorText = error;
        BackupLogging::warn(QStringLiteral("data protection gate rejected %1 reason=%2 state=%3 active=%4")
                                .arg(operationName(operation), reason, activeStateText, activeOperationText));
        BackupRecoveryAuditTrail::appendRecoveryEvent(QStringLiteral("deferred"),
                                                      QStringLiteral("data_protection_operation"),
                                                      QString(),
                                                      QStringLiteral("DataProtectionCoordinator"),
                                                      operationName(operation),
                                                      QString(),
                                                      QStringLiteral("data protection gate busy"),
                                                      error);
        return Scope(operation, QString(), false);
    };

    while (true) {
        const int elapsedMs = static_cast<int>(waitTimer.elapsed());
        const int remainingMs = timeoutMs > 0 ? qMax(0, timeoutMs - elapsedMs) : 0;
        const int lockWaitMs = timeoutMs > 0 ? qMin(25, remainingMs) : 0;
        const bool locked = timeoutMs > 0 ? coordinatorMutex().tryLock(lockWaitMs) : coordinatorMutex().tryLock();
        if (!locked) {
            if (timeoutMs <= 0 || elapsedMs >= timeoutMs) {
                return rejectBusy(currentState(), currentOperationId());
            }
            boundedCoordinatorPause(10);
            continue;
        }

        if (activeOperationId().isEmpty()) {
            const QString opId = QUuid::createUuid().toString(QUuid::WithoutBraces);
            activeOperationId() = opId;
            activeOperationName() = operationName(operation);
            activeState() = operationName(operation);
            degradedReason().clear();
            activeThreadId() = QThread::currentThreadId();
            coordinatorMutex().unlock();

            BackupLogging::info(QStringLiteral("data protection gate acquired op=%1 id=%2 reason=%3")
                                    .arg(operationName(operation), opId, reason));
            BackupRecoveryAuditTrail::appendRecoveryEvent(QStringLiteral("started"),
                                                          QStringLiteral("data_protection_operation"),
                                                          opId,
                                                          QStringLiteral("DataProtectionCoordinator"),
                                                          operationName(operation),
                                                          opId,
                                                          reason);
            if (errorText) errorText->clear();
            return Scope(operation, opId, true);
        }

        const QString activeId = activeOperationId();
        const QString state = activeState();
        coordinatorMutex().unlock();

        if (timeoutMs <= 0 || elapsedMs >= timeoutMs) {
            return rejectBusy(state, activeId);
        }
        boundedCoordinatorPause(10);
    }
}

bool DataProtectionCoordinator::isBusy()
{
    QMutexLocker locker(&coordinatorMutex());
    return !activeOperationId().isEmpty();
}

bool DataProtectionCoordinator::isRestoreRunning()
{
    QMutexLocker locker(&coordinatorMutex());
    return activeOperationName() == operationName(Operation::Restore);
}

QString DataProtectionCoordinator::currentState()
{
    QMutexLocker locker(&coordinatorMutex());
    return activeState();
}

QString DataProtectionCoordinator::currentOperationId()
{
    QMutexLocker locker(&coordinatorMutex());
    return activeOperationId();
}

void DataProtectionCoordinator::setStateForCurrentOperation(const QString& operationId, const QString& state)
{
    QMutexLocker locker(&coordinatorMutex());
    if (operationId.isEmpty() || activeOperationId() != operationId) return;
    activeState() = state.trimmed().isEmpty() ? activeOperationName() : state.trimmed();
    BackupLogging::info(QStringLiteral("data protection state id=%1 state=%2").arg(operationId, activeState()));
}

void DataProtectionCoordinator::markDegradedForCurrentOperation(const QString& operationId, const QString& reason)
{
    QMutexLocker locker(&coordinatorMutex());
    if (operationId.isEmpty() || activeOperationId() != operationId) return;
    degradedReason() = reason;
    activeState() = QStringLiteral("Degraded");
    BackupLogging::warn(QStringLiteral("data protection degraded id=%1 reason=%2").arg(operationId, reason));
}

void DataProtectionCoordinator::clearDegradedForCurrentOperation(const QString& operationId)
{
    QMutexLocker locker(&coordinatorMutex());
    if (operationId.isEmpty() || activeOperationId() != operationId) return;
    degradedReason().clear();
}

QString DataProtectionCoordinator::operationName(Operation operation)
{
    switch (operation) {
    case Operation::AutomaticBackup: return QStringLiteral("AutomaticBackup");
    case Operation::ManualBackup: return QStringLiteral("ManualBackup");
    case Operation::EmergencyBackup: return QStringLiteral("EmergencyBackup");
    case Operation::Cleanup: return QStringLiteral("CleanupRunning");
    case Operation::RecoveryDraftAutosave: return QStringLiteral("RecoveryDraftAutosaveRunning");
    case Operation::Restore: return QStringLiteral("RestorePreflight");
    case Operation::ShutdownFlush: return QStringLiteral("ShutdownFlushing");
    case Operation::DbMigration: return QStringLiteral("DbMigration");
    case Operation::ImportCommit: return QStringLiteral("ImportCommit");
    }
    return QStringLiteral("Unknown");
}
