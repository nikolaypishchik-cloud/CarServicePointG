#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

struct LegacyImportBundle {
    QString sourcePath;
    QString sourceKind;
    QString workingRootPath;
    QString primaryDbPath;
    QString snapshotDbPath;
    QString settingsJsonPath;
    QString uiStateJsonPath;
    QString appLogPath;
    bool ownsWorkingRoot = false;
};

struct LegacyImportEntityAudit {
    QString entityName;
    QString sourceTable;
    bool required = false;
    bool present = false;
    int rowCount = -1;
    QStringList missingColumns;
};

struct LegacyImportFieldMap {
    QString targetField;
    QStringList sourceCandidates;
    QString transformRule;
    bool required = false;
};

struct LegacyImportEntitySchemaMap {
    QString entityName;
    QString sourceTable;
    QString targetTable;
    QString strategy;
    bool requiresNormalization = false;
    QVector<LegacyImportFieldMap> fieldMaps;
    QStringList notes;
};

struct LegacyImportFinding {
    QString severity;
    QString code;
    QString entityName;
    QString message;
    int affectedCount = -1;
    bool blocksExecute = false;
};

struct LegacyImportVerifyEntityResult {
    QString entityName;
    int sourceCount = -1;
    int targetCount = -1;
    int missingCount = 0;
    int invalidLinkCount = 0;
    bool ok = false;
    QStringList messages;
};

struct LegacyImportVerifyReport {
    bool ran = false;
    bool success = false;
    QString sourceDbPath;
    QString reportDirPath;
    QString markdownReportPath;
    QString jsonReportPath;
    QVector<LegacyImportVerifyEntityResult> entities;
    QStringList infoMessages;
    QStringList warningMessages;
    QStringList errorMessages;
};

struct LegacyImportAuditReport {
    bool sourceResolved = false;
    bool sqliteIntegrityOk = false;
    bool canExecute = false;
    QString sourceKind;
    QString sourcePath;
    QString sourceDbPath;
    QString sourceFingerprint;
    QString reportDirPath;
    QString markdownReportPath;
    QString jsonReportPath;
    int orderCount = 0;
    int serviceCount = 0;
    int partCount = 0;
    QStringList detectedSidecars;
    QVector<LegacyImportEntityAudit> entities;
    QVector<LegacyImportEntitySchemaMap> schemaMap;
    QVector<LegacyImportFinding> findings;
    QStringList infoMessages;
    QStringList warningMessages;
    QStringList errorMessages;
};
