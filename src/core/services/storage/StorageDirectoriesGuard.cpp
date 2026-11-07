#include "core/services/storage/StorageDirectoriesGuard.h"

#include "core/services/settings/GeneralSettingsService.h"
#include "core/io/SafeFileOps.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTemporaryFile>
#include <QCryptographicHash>
#include <QLockFile>
#include <QSaveFile>

namespace StorageDirectoriesGuard {
namespace {

QString cleanNativePath(const QString& path)
{
    const QString trimmed = path.trimmed();
    if (trimmed.isEmpty()) return QString();
    return QDir::cleanPath(QDir::fromNativeSeparators(trimmed));
}

QString safePathPart(QString value, const QString& defaultValue)
{
    value = value.trimmed();
    if (value.isEmpty()) value = defaultValue;
    value.replace(QRegularExpression(QStringLiteral(R"([\\/:*?"<>|]+)")), QStringLiteral("_"));
    value.replace(QRegularExpression(QStringLiteral(R"(\s+)")), QStringLiteral(" "));
    return value;
}

QString rootFromPayload(StorageKind kind, const GeneralSettingsService::DirectoriesSettingsPayload& dirs)
{
    switch (kind) {
    case StorageKind::OrderPdf:   return dirs.pdfDir.trimmed();
    case StorageKind::Invoice:    return dirs.invoicesDir.trimmed();
    case StorageKind::Report:     return dirs.reportsDir.trimmed();
    case StorageKind::OrderPhoto: return dirs.orderPhotosDir.trimmed();
    }
    return QString();
}

QString kindSlug(StorageKind kind)
{
    switch (kind) {
    case StorageKind::OrderPdf:   return QStringLiteral("orders_pdf");
    case StorageKind::Invoice:    return QStringLiteral("invoices");
    case StorageKind::Report:     return QStringLiteral("reports");
    case StorageKind::OrderPhoto: return QStringLiteral("order_photos");
    }
    return QStringLiteral("storage");
}

} // namespace

QStringList MissingState::labels() const
{
    QStringList out;
    if (orderPdfMissing) out << labelForKind(StorageKind::OrderPdf);
    if (invoiceMissing) out << labelForKind(StorageKind::Invoice);
    if (reportMissing) out << labelForKind(StorageKind::Report);
    if (orderPhotoMissing) out << labelForKind(StorageKind::OrderPhoto);
    return out;
}

QString labelForKind(StorageKind kind)
{
    switch (kind) {
    case StorageKind::OrderPdf:   return QString::fromUtf8("наряди / PDF");
    case StorageKind::Invoice:    return QString::fromUtf8("накладні");
    case StorageKind::Report:     return QString::fromUtf8("звіти");
    case StorageKind::OrderPhoto: return QString::fromUtf8("фото нарядів");
    }
    return QString::fromUtf8("файли");
}

QString configuredRoot(StorageKind kind)
{
    return cleanNativePath(rootFromPayload(kind, GeneralSettingsService::loadDirectoriesSettings()));
}

MissingState missingState()
{
    const auto dirs = GeneralSettingsService::loadDirectoriesSettings();
    MissingState state;
    state.orderPdfMissing = dirs.pdfDir.trimmed().isEmpty();
    state.invoiceMissing = dirs.invoicesDir.trimmed().isEmpty();
    state.reportMissing = dirs.reportsDir.trimmed().isEmpty();
    state.orderPhotoMissing = dirs.orderPhotosDir.trimmed().isEmpty();
    return state;
}

QString missingDirectoriesMessage()
{
    const MissingState state = missingState();
    if (!state.anyMissing()) return QString();
    return QString::fromUtf8(
               "Не вказані директорії збереження: %1.\n\n"
               "З міркувань цілісності даних наряди/PDF, накладні, звіти та фото не будуть зберігатися у випадкові або службові папки. "
               "Вкажіть директорії в Налаштуваннях → Інтеграції → Діректорія.")
        .arg(state.labels().join(QStringLiteral(", ")));
}

bool hasAllRequiredDirectories()
{
    return !missingState().anyMissing();
}

bool validateCandidateDirectory(const QString& path, const QString& label, QString* errorText)
{
    const QString dirPath = cleanNativePath(path);
    if (dirPath.isEmpty()) {
        if (errorText) {
            *errorText = QString::fromUtf8("Не вказано директорію для: %1.").arg(label);
        }
        return false;
    }

    QDir dir(dirPath);
    if (!dir.exists() && !QDir().mkpath(dirPath)) {
        if (errorText) {
            *errorText = QString::fromUtf8("Не вдалося створити директорію для %1:\n%2")
                .arg(label, QDir::toNativeSeparators(dirPath));
        }
        return false;
    }

    QTemporaryFile probe(QDir(dirPath).filePath(QStringLiteral(".__cs_storage_probe_XXXXXX.tmp")));
    probe.setAutoRemove(true);
    if (!probe.open()) {
        if (errorText) {
            *errorText = QString::fromUtf8("Немає прав на запис у директорію для %1:\n%2")
                .arg(label, QDir::toNativeSeparators(dirPath));
        }
        return false;
    }
    probe.close();
    return true;
}

bool ensureWritableRoot(StorageKind kind, QString* rootOut, QString* errorText)
{
    if (rootOut) rootOut->clear();
    const QString root = configuredRoot(kind);
    const QString label = labelForKind(kind);
    if (!validateCandidateDirectory(root, label, errorText)) {
        return false;
    }
    if (rootOut) *rootOut = root;
    return true;
}

static QString managedSubdirAtRoot(const QString& configured, StorageKind kind, int orderNumber, const QString& bucket, QString* errorText)
{
    const QString root = cleanNativePath(configured);
    if (!validateCandidateDirectory(root, labelForKind(kind), errorText)) return {};

    QDir base(root);
    if (kind == StorageKind::OrderPdf || kind == StorageKind::Invoice) return base.absolutePath();
    const QString orderPart = orderNumber > 0
        ? QStringLiteral("order_%1").arg(orderNumber)
        : QStringLiteral("order_unknown");
    const QString bucketPart = safePathPart(bucket, kindSlug(kind));
    const QString relative = orderPart + QLatin1Char('/') + bucketPart;
    const QString dirPath = base.filePath(relative);
    if (!QDir().mkpath(dirPath)) {
        if (errorText) {
            *errorText = QString::fromUtf8("Не вдалося створити папку збереження для %1:\n%2")
                .arg(labelForKind(kind), QDir::toNativeSeparators(dirPath));
        }
        return QString();
    }
    return dirPath;
}

QString managedSubdir(StorageKind kind, int orderNumber, const QString& bucket, QString* errorText)
{
    return managedSubdirAtRoot(configuredRoot(kind), kind, orderNumber, bucket, errorText);
}

QString managedFilePath(StorageKind kind, int orderNumber, const QString& bucket, const QString& fileName, QString* errorText)
{
    const QString dirPath = managedSubdir(kind, orderNumber, bucket, errorText);
    if (dirPath.trimmed().isEmpty()) return QString();
    const QString safeName = safePathPart(fileName, QStringLiteral("file.bin"));
    return QDir(dirPath).filePath(safeName);
}

QString copyFileToManagedStorage(StorageKind kind, int orderNumber, const QString& bucket, int ownerId, const QString& sourcePath, QString* errorText)
{
    if (ownerId <= 0 || sourcePath.trimmed().isEmpty()) return QString();
    const QFileInfo fi(sourcePath);
    if (!fi.exists() || !fi.isFile()) {
        if (errorText) *errorText = QString::fromUtf8("Файл не знайдено: %1").arg(QDir::toNativeSeparators(sourcePath));
        return QString();
    }

    const QString ext = fi.suffix().trimmed().isEmpty() ? QStringLiteral("bin") : fi.suffix().trimmed().toLower();
    const QString base = (kind == StorageKind::Invoice)
        ? safePathPart(fi.completeBaseName(), QStringLiteral("invoice"))
        : safePathPart(bucket, kindSlug(kind));
    const QString targetName = (kind == StorageKind::Invoice)
        ? QStringLiteral("%1.%2").arg(base, ext)
        : QStringLiteral("%1_%2.%3").arg(base).arg(ownerId).arg(ext);
    return copyFileToManagedStorageAs(kind, orderNumber, bucket, targetName, fi.absoluteFilePath(), errorText);
}

QString copyFileToManagedStorageAs(StorageKind kind, int orderNumber, const QString& bucket, const QString& targetFileName, const QString& sourcePath, QString* errorText)
{
    return copyFileToManagedStorageAsAtRoot(configuredRoot(kind), kind, orderNumber, bucket, targetFileName, sourcePath, errorText);
}

QString copyFileToManagedStorageAsAtRoot(const QString& configured, StorageKind kind, int orderNumber, const QString& bucket, const QString& targetFileName, const QString& sourcePath, QString* errorText)
{
    if (sourcePath.trimmed().isEmpty()) return QString();
    const QFileInfo fi(sourcePath);
    if (!fi.exists() || !fi.isFile()) {
        if (errorText) *errorText = QString::fromUtf8("Файл не знайдено: %1").arg(QDir::toNativeSeparators(sourcePath));
        return QString();
    }

    const QString ext = fi.suffix().trimmed().isEmpty() ? QStringLiteral("bin") : fi.suffix().trimmed().toLower();
    QString safeName = safePathPart(targetFileName, QStringLiteral("file.%1").arg(ext));
    const QString dotExt = QStringLiteral(".%1").arg(ext);
    if (!safeName.endsWith(dotExt, Qt::CaseInsensitive) && QFileInfo(safeName).suffix().trimmed().isEmpty()) {
        safeName += dotExt;
    }
    const QString directory = managedSubdirAtRoot(configured, kind, orderNumber, bucket, errorText);
    if (directory.isEmpty()) return {};
    QString targetPath = QDir(directory).filePath(safeName);

    const QString cleanSource = cleanNativePath(fi.absoluteFilePath());
    const QString cleanTarget = cleanNativePath(QFileInfo(targetPath).absoluteFilePath());
    if (cleanSource.compare(cleanTarget, Qt::CaseInsensitive) == 0) {
        return targetPath;
    }

    // Retain the per-file lock protocol for ordinary paths. Windows lock-file
    // removal can fail beyond MAX_PATH, so long paths use a short root lock.
    const QString lockPath = targetPath.size() < 240
        ? targetPath + QStringLiteral(".lock")
        : QDir(cleanNativePath(configured)).filePath(QStringLiteral(".cs-copy.lock"));
    QLockFile lock(lockPath);
    if (!lock.tryLock(5000)) {
        if (errorText) *errorText = QString::fromUtf8("Файл вкладення зайнятий іншою операцією.");
        return {};
    }
    auto digest = [](const QString& path) {
        QFile file(path);
        QCryptographicHash hash(QCryptographicHash::Sha256);
        return file.open(QIODevice::ReadOnly) && hash.addData(&file) ? hash.result() : QByteArray{};
    };
    const QByteArray sourceHash = digest(fi.absoluteFilePath());
    if (sourceHash.isEmpty()) {
        if (errorText) *errorText = QString::fromUtf8("Не вдалося прочитати файл вкладення.");
        return {};
    }
    // Existing documents may be shared by several rows. Never overwrite them.
    if (QFileInfo::exists(targetPath)) {
        if (digest(targetPath) == sourceHash) return targetPath;
        const QFileInfo target(targetPath);
        targetPath = target.dir().filePath(target.completeBaseName() + QLatin1Char('_')
            + QString::fromLatin1(sourceHash.toHex()) + QLatin1Char('.') + target.suffix());
        if (QFileInfo::exists(targetPath)) {
            if (digest(targetPath) == sourceHash) return targetPath;
            if (errorText) *errorText = QString::fromUtf8("Конфлікт вмісту файла вкладення. Попередній файл збережено.");
            return {};
        }
    }
    QFile source(fi.absoluteFilePath());
    QSaveFile target(targetPath);
    target.setDirectWriteFallback(false);
    if (!source.open(QIODevice::ReadOnly) || !target.open(QIODevice::WriteOnly)) {
        if (errorText) *errorText = QString::fromUtf8("Не вдалося підготувати безпечне збереження вкладення.");
        return {};
    }
    QCryptographicHash copiedHash(QCryptographicHash::Sha256);
    while (!source.atEnd()) {
        const QByteArray block = source.read(1024 * 1024);
        if (block.isEmpty() || target.write(block) != block.size()) {
            if (errorText) *errorText = QString::fromUtf8("Не вдалося повністю скопіювати вкладення. Попередній файл збережено.");
            return {};
        }
        copiedHash.addData(block);
    }
    if (source.error() != QFileDevice::NoError || copiedHash.result() != sourceHash || !target.commit()) {
        if (errorText) *errorText = QString::fromUtf8("Не вдалося перевірити та завершити збереження вкладення.");
        return {};
    }
    return targetPath;
}

QString managedReportFilePath(const QString& reportFolder, const QString& fileName, QString* errorText)
{
    QString root;
    if (!ensureWritableRoot(StorageKind::Report, &root, errorText)) return QString();

    QDir reportsRoot(root);
    if (QFileInfo(root).fileName().compare(QString::fromUtf8("Звіти"), Qt::CaseInsensitive) != 0) {
        root = reportsRoot.filePath(QString::fromUtf8("Звіти"));
        reportsRoot.setPath(root);
    }
    if (!reportsRoot.exists() && !QDir().mkpath(root)) {
        if (errorText) {
            *errorText = QString::fromUtf8("Не вдалося створити папку збереження для звітів:\n%1")
                .arg(QDir::toNativeSeparators(root));
        }
        return QString();
    }

    const QString folder = safePathPart(reportFolder, QString::fromUtf8("Звіт"));
    const QString dirPath = reportsRoot.filePath(folder);
    if (!QDir().mkpath(dirPath)) {
        if (errorText) {
            *errorText = QString::fromUtf8("Не вдалося створити папку збереження для звіту:\n%1")
                .arg(QDir::toNativeSeparators(dirPath));
        }
        return QString();
    }

    QString safeName = safePathPart(fileName, QStringLiteral("report.pdf"));
    if (!safeName.endsWith(QStringLiteral(".pdf"), Qt::CaseInsensitive)) safeName += QStringLiteral(".pdf");
    return QDir(dirPath).filePath(safeName);
}

QStringList managedRoots()
{
    QStringList roots;
    for (StorageKind kind : {StorageKind::OrderPdf, StorageKind::Invoice, StorageKind::Report, StorageKind::OrderPhoto}) {
        const QString root = configuredRoot(kind);
        if (!root.trimmed().isEmpty()) roots << cleanNativePath(root);
    }
    roots.removeDuplicates();
    return roots;
}

bool isPathInsideManagedRoots(const QString& path)
{
    const QString cleanPath = cleanNativePath(path);
    if (cleanPath.isEmpty()) return false;
    const QStringList roots = managedRoots();
    for (const QString& root : roots) {
        const QString cleanRoot = cleanNativePath(root);
        if (cleanRoot.isEmpty()) continue;
        if (cleanPath == cleanRoot || cleanPath.startsWith(cleanRoot + QLatin1Char('/'), Qt::CaseInsensitive)) {
            return true;
        }
    }
    return false;
}

} // namespace StorageDirectoriesGuard
