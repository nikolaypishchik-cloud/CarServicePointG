#pragma once

#include <QString>

#include "core/services/DatabaseImportClassifier.h"

struct NativeBackupAuditResult {
    DatabaseImportClassification classification;
    int orderCount = 0;
};

struct NativeBackupRestoreResult {
    QString sourcePath;
    QString targetDbPath;
    QString rollbackBackupPath;
    int restoredOrderCount = 0;
    bool restartRequired = false;
};

class NativeBackupRestoreService
{
public:
    static bool auditBackupFile(const QString& sourcePath,
                                NativeBackupAuditResult* result,
                                QString* errorText = nullptr);
    static bool restoreBackupFile(const QString& sourcePath,
                                  NativeBackupRestoreResult* result,
                                  QString* errorText = nullptr);
};
