#pragma once

#include <QDialog>

#include "core/backup/BackupDtos.h"

class RecoveryCoordinator;
class CrashRecoveryManager;
class QTableWidget;

class RecoveryManagerDialog : public QDialog
{
    Q_OBJECT
public:
    RecoveryManagerDialog(CrashRecoveryManager* manager, RecoveryCoordinator* coordinator, QWidget* parent = nullptr);

private:
    void rebuild();
    QVector<DraftPayload> selectedDrafts() const;

    CrashRecoveryManager* m_manager = nullptr;
    RecoveryCoordinator* m_coordinator = nullptr;
    QTableWidget* m_table = nullptr;
};
