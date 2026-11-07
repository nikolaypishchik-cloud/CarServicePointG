#pragma once

#include <QDialog>

#include "core/backup/BackupDtos.h"

class BackupManager;
class RestoreCoordinator;
class QComboBox;
class QLineEdit;
class QTableWidget;

class RestoreManagerDialog : public QDialog
{
    Q_OBJECT
public:
    RestoreManagerDialog(BackupManager* backupManager, RestoreCoordinator* coordinator, QWidget* parent = nullptr);

private:
    void rebuild();
    QVector<SnapshotMetadata> filteredSnapshots() const;
    SnapshotMetadata selectedSnapshot() const;

    BackupManager* m_backupManager = nullptr;
    RestoreCoordinator* m_coordinator = nullptr;
    QComboBox* m_scope = nullptr;
    QComboBox* m_operationFilter = nullptr;
    QComboBox* m_sourceFilter = nullptr;
    QLineEdit* m_moduleFilter = nullptr;
    QLineEdit* m_entitySearch = nullptr;
    QTableWidget* m_table = nullptr;
};
