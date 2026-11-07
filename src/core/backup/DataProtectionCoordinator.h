#pragma once

#include <QString>

class DataProtectionCoordinator
{
public:
    enum class Operation {
        AutomaticBackup,
        ManualBackup,
        EmergencyBackup,
        Cleanup,
        RecoveryDraftAutosave,
        Restore,
        ShutdownFlush,
        DbMigration,
        ImportCommit
    };

    class Scope
    {
    public:
        Scope() = default;
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;
        Scope(Scope&& other) noexcept;
        Scope& operator=(Scope&& other) noexcept;
        ~Scope();

        bool isActive() const { return m_active; }
        QString operationId() const { return m_operationId; }
        void setState(const QString& state);
        void markDegraded(const QString& reason);
        void clearDegraded();
        void release();

    private:
        friend class DataProtectionCoordinator;
        Scope(Operation operation, const QString& operationId, bool active);
        Operation m_operation = Operation::AutomaticBackup;
        QString m_operationId;
        bool m_active = false;
    };

    static Scope tryEnter(Operation operation,
                          const QString& reason,
                          QString* errorText = nullptr,
                          int timeoutMs = 0);
    static bool isBusy();
    static bool isRestoreRunning();
    static QString currentState();
    static QString currentOperationId();
    static void setStateForCurrentOperation(const QString& operationId, const QString& state);
    static void markDegradedForCurrentOperation(const QString& operationId, const QString& reason);
    static void clearDegradedForCurrentOperation(const QString& operationId);

    static QString operationName(Operation operation);
};
