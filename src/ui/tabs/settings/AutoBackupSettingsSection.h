#pragma once

#include <QWidget>
#include <QVector>

class BackupManager;
class CrashRecoveryManager;
class RestoreCoordinator;
class RecoveryCoordinator;
class QLabel;
class LabeledToggle;
class QLineEdit;
class QSpinBox;
class QGridLayout;
class QComboBox;
class QPushButton;
class QResizeEvent;

class AutoBackupSettingsSection : public QWidget
{
    Q_OBJECT
public:
    AutoBackupSettingsSection(BackupManager* backupManager,
                              CrashRecoveryManager* crashRecoveryManager,
                              RestoreCoordinator* restoreCoordinator,
                              RecoveryCoordinator* recoveryCoordinator,
                              QWidget* parent = nullptr);

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void reloadFromSettings();
    void refreshStatus();
    void saveSettings();
    void rebuildActionButtonsLayout();
    QString effectiveBackupDir() const;

    BackupManager* m_backupManager = nullptr;
    CrashRecoveryManager* m_crashRecoveryManager = nullptr;
    RestoreCoordinator* m_restoreCoordinator = nullptr;
    RecoveryCoordinator* m_recoveryCoordinator = nullptr;

    QComboBox* m_protectionMode = nullptr;
    LabeledToggle* m_enabled = nullptr;
    LabeledToggle* m_emergencyBeforeRestore = nullptr;
    LabeledToggle* m_keepPrev = nullptr;
    LabeledToggle* m_cleanupOnStart = nullptr;
    LabeledToggle* m_cleanupAfterBackup = nullptr;
    LabeledToggle* m_showNotifications = nullptr;
    LabeledToggle* m_detailedLogging = nullptr;
    LabeledToggle* m_recoveryEnabled = nullptr;
    LabeledToggle* m_recoveryAutosaveEnabled = nullptr;
    LabeledToggle* m_recoveryShowDialog = nullptr;
    QLineEdit* m_backupDir = nullptr;
    QSpinBox* m_maxPerDay = nullptr;
    QSpinBox* m_maxTotalMb = nullptr;
    QSpinBox* m_autosaveInterval = nullptr;
    QSpinBox* m_recoveryRetention = nullptr;
    QLabel* m_status = nullptr;
    QGridLayout* m_actionsGrid = nullptr;
    QVector<QPushButton*> m_actionButtons;
    int m_lastActionColumns = -1;
};
