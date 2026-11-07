#pragma once

#include <QString>
#include <QStringList>

namespace StorageDirectoriesGuard {

enum class StorageKind {
    OrderPdf,
    Invoice,
    Report,
    OrderPhoto
};

struct MissingState {
    bool orderPdfMissing = false;
    bool invoiceMissing = false;
    bool reportMissing = false;
    bool orderPhotoMissing = false;

    bool anyMissing() const { return orderPdfMissing || invoiceMissing || reportMissing || orderPhotoMissing; }
    QStringList labels() const;
};

QString labelForKind(StorageKind kind);
QString configuredRoot(StorageKind kind);
MissingState missingState();
QString missingDirectoriesMessage();
bool hasAllRequiredDirectories();

bool ensureWritableRoot(StorageKind kind, QString* rootOut = nullptr, QString* errorText = nullptr);
bool validateCandidateDirectory(const QString& path, const QString& label, QString* errorText = nullptr);
QString managedSubdir(StorageKind kind, int orderNumber, const QString& bucket, QString* errorText = nullptr);
QString managedFilePath(StorageKind kind, int orderNumber, const QString& bucket, const QString& fileName, QString* errorText = nullptr);
QString copyFileToManagedStorage(StorageKind kind, int orderNumber, const QString& bucket, int ownerId, const QString& sourcePath, QString* errorText = nullptr);
QString copyFileToManagedStorageAs(StorageKind kind, int orderNumber, const QString& bucket, const QString& targetFileName, const QString& sourcePath, QString* errorText = nullptr);
// Background jobs obtain the configured root from their own database connection.
QString copyFileToManagedStorageAsAtRoot(const QString& configuredRoot, StorageKind kind, int orderNumber, const QString& bucket, const QString& targetFileName, const QString& sourcePath, QString* errorText = nullptr);
QString managedReportFilePath(const QString& reportFolder, const QString& fileName, QString* errorText = nullptr);
bool isPathInsideManagedRoots(const QString& path);
QStringList managedRoots();

} // namespace StorageDirectoriesGuard
