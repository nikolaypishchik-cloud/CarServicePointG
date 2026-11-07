#pragma once

#include <QString>
#include <QStringList>

struct DatabaseImportClassification {
    QString sourcePath;
    bool fileExists = false;
    bool sqliteReadable = false;
    QString detectedKind; // native_backup | legacy_import | unknown
    QStringList currentSignatureTables;
    QStringList legacySignatureTables;
    int workOrdersCount = 0;
    QString explanation;

    bool isNativeBackup() const { return detectedKind == QStringLiteral("native_backup"); }
    bool isLegacyImport() const { return detectedKind == QStringLiteral("legacy_import"); }
};

class DatabaseImportClassifier
{
public:
    static bool classify(const QString& sourcePath,
                         DatabaseImportClassification* classification,
                         QString* errorText = nullptr);
};
