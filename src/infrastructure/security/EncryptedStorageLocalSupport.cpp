// P2 architecture migration: implementation module converted from legacy .ipp implementation-fragment decomposition.
// Compiled as a separate P2 implementation module; include-order dependencies are isolated in the module prelude.

#include "infrastructure/security/EncryptedStorageModulePrelude.h"

namespace EncryptedStorage {

static const char kMagic[8] = {'C','S','D','B','E','N','C','1'};
static constexpr quint32 kFormatVersion = 1;
static constexpr int kNonceSize = 12;
static constexpr int kTagSize = 16;
static constexpr int kMasterKeySize = 32; // AES-256

bool writeAllBytesAtomic(const QString& targetPath, const QByteArray& data, QString* errorText);
bool decryptSealedFileToPlain(const QString& sealedPath, const QString& plainPath, QString* errorText);

QList<DbMapping> dbMappings()
{
    const QDir dataDir(AppRuntime::dataDir());
    return {
        {AppRuntime::activeDbFilePath(),   AppRuntime::sealedActiveDbFilePath(),   dataDir.filePath(QStringLiteral("CarService_workspace.db"))},
        {AppRuntime::legacyDbFilePath(),   AppRuntime::sealedLegacyDbFilePath(),   dataDir.filePath(QStringLiteral("CarServicePointG.db"))},
        {AppRuntime::archiveDbFilePath(),  AppRuntime::sealedArchiveDbFilePath(),  dataDir.filePath(QStringLiteral("CarService_archive_legacy.db"))},
        {AppRuntime::registryDbFilePath(), AppRuntime::sealedRegistryDbFilePath(), dataDir.filePath(QStringLiteral("CarService_registry.db"))}
    };
}

void removePlaintextArtifacts(const QString& basePath)
{
    safeRemoveEncryptedStorageFile(basePath, QFileInfo(basePath).absolutePath());
    safeRemoveEncryptedStorageFile(basePath + QStringLiteral("-wal"), QFileInfo(basePath).absolutePath());
    safeRemoveEncryptedStorageFile(basePath + QStringLiteral("-shm"), QFileInfo(basePath).absolutePath());
    safeRemoveEncryptedStorageFile(basePath + QStringLiteral(".rollback"), QFileInfo(basePath).absolutePath());
    safeRemoveEncryptedStorageFile(basePath + QStringLiteral(".staging"), QFileInfo(basePath).absolutePath());
}

QString runtimeSessionMarkerPath()
{
    QDir d(AppRuntime::sessionDir());
    return d.filePath(QStringLiteral("runtime_session_incomplete.flag"));
}

QString recoveryQuarantineDirPath()
{
    QDir d(AppRuntime::dataDir());
    d.mkpath(QStringLiteral("recovery/runtime_quarantine"));
    d.cd(QStringLiteral("recovery"));
    d.cd(QStringLiteral("runtime_quarantine"));
    const QString dirPath = d.absolutePath();
    AppRuntime::protectPathAtRest(dirPath);
    return dirPath;
}

QString runtimeArtifactSuffixTag(const QString& suffix)
{
    if (suffix == QStringLiteral("-wal")) return QStringLiteral("wal");
    if (suffix == QStringLiteral("-shm")) return QStringLiteral("shm");
    if (suffix == QStringLiteral(".rollback")) return QStringLiteral("rollback");
    if (suffix == QStringLiteral(".staging")) return QStringLiteral("staging");
    return QStringLiteral("db");
}

QStringList runtimeArtifactSuffixes()
{
    return {
        QString(),
        QStringLiteral("-wal"),
        QStringLiteral("-shm"),
        QStringLiteral(".rollback"),
        QStringLiteral(".staging")
    };
}

bool hasRuntimeArtifacts(const DbMapping& mapping)
{
    for (const QString& suffix : runtimeArtifactSuffixes()) {
        if (QFileInfo::exists(mapping.runtimePath + suffix)) return true;
    }
    return false;
}

bool hasAnyRuntimeArtifacts()
{
    for (const DbMapping& mapping : dbMappings()) {
        if (hasRuntimeArtifacts(mapping)) return true;
    }
    return QFileInfo::exists(runtimeSessionMarkerPath());
}

bool writeRuntimeSessionMarker(QString* errorText)
{
    const QByteArray payload = QStringLiteral("opened_at=%1\n")
        .arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs))
        .toUtf8();
    return writeAllBytesAtomic(runtimeSessionMarkerPath(), payload, errorText);
}

void clearRuntimeSessionMarker()
{
    safeRemoveEncryptedStorageFile(runtimeSessionMarkerPath(), AppRuntime::sessionDir());
}

bool quarantineRuntimeArtifacts(const DbMapping& mapping, const QString& reason, QString* errorText)
{
    const QString stamp = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd_HHmmss_zzz"));
    const QFileInfo fi(mapping.runtimePath);
    const QString baseName = fi.completeBaseName().isEmpty() ? fi.fileName() : fi.completeBaseName();
    const QDir recoveryDir(recoveryQuarantineDirPath());

    for (const QString& suffix : runtimeArtifactSuffixes()) {
        const QString sourcePath = mapping.runtimePath + suffix;
        if (!QFileInfo::exists(sourcePath)) continue;
        const QString targetName = QStringLiteral("%1_%2_%3.%4")
            .arg(stamp, baseName, reason, runtimeArtifactSuffixTag(suffix));
        const QString targetPath = recoveryDir.filePath(targetName);
        safeRemoveEncryptedStorageFile(targetPath, recoveryQuarantineDirPath());
        if (!QFile::rename(sourcePath, targetPath)) {
            if (!QFile::copy(sourcePath, targetPath)) {
                if (errorText) {
                    *errorText = QStringLiteral("Не вдалося перемістити runtime-артефакт у quarantine: %1")
                        .arg(sourcePath);
                }
                return false;
            }
            safeRemoveEncryptedStorageFile(sourcePath, QFileInfo(sourcePath).absolutePath());
        }
        AppRuntime::protectPathAtRest(targetPath);
    }
    return true;
}

RuntimeDbPreflightValidator::DbRole runtimeRoleForMapping(const DbMapping& mapping)
{
    if (QFileInfo(mapping.runtimePath).absoluteFilePath() == QFileInfo(AppRuntime::registryDbFilePath()).absoluteFilePath()) {
        return RuntimeDbPreflightValidator::DbRole::Registry;
    }
    if (QFileInfo(mapping.runtimePath).absoluteFilePath() == QFileInfo(AppRuntime::archiveDbFilePath()).absoluteFilePath()
        || QFileInfo(mapping.runtimePath).absoluteFilePath() == QFileInfo(AppRuntime::legacyDbFilePath()).absoluteFilePath()) {
        return RuntimeDbPreflightValidator::DbRole::Archive;
    }
    return RuntimeDbPreflightValidator::DbRole::Workspace;
}

bool validateRuntimeSqlite(const DbMapping& mapping, QString* errorText)
{
    const bool hasBase = QFileInfo::exists(mapping.runtimePath);
    const bool hasSidecars = QFileInfo::exists(mapping.runtimePath + QStringLiteral("-wal"))
        || QFileInfo::exists(mapping.runtimePath + QStringLiteral("-shm"));

    if (!hasBase) {
        if (hasSidecars) {
            if (errorText) {
                *errorText = QStringLiteral("Виявлено WAL/SHM без основного runtime-файла БД: %1")
                    .arg(mapping.runtimePath);
            }
            return false;
        }
        return true;
    }

    QString localError;
    if (!RuntimeDbPreflightValidator::validateFile(mapping.runtimePath, runtimeRoleForMapping(mapping), &localError)) {
        if (errorText) {
            *errorText = localError.trimmed().isEmpty()
                ? QStringLiteral("Не вдалося пройти runtime DB preflight")
                : localError;
        }
        return false;
    }
    return true;
}

bool restoreRuntimeFromSealed(const DbMapping& mapping, QString* errorText)
{
    removePlaintextArtifacts(mapping.runtimePath);
    if (!QFileInfo::exists(mapping.sealedPath)) return true;
    return decryptSealedFileToPlain(mapping.sealedPath, mapping.runtimePath, errorText);
}

bool writeAllBytesAtomic(const QString& targetPath, const QByteArray& data, QString* errorText)
{
    QSaveFile out(targetPath);
    if (!out.open(QIODevice::WriteOnly)) {
        if (errorText) *errorText = out.errorString();
        return false;
    }
    if (out.write(data) != data.size()) {
        if (errorText) *errorText = out.errorString();
        return false;
    }
    if (!out.commit()) {
        if (errorText) *errorText = out.errorString();
        return false;
    }
    AppRuntime::protectPathAtRest(targetPath);
    return true;
}

#ifdef Q_OS_WIN
struct BlobGuard {
    DATA_BLOB blob{};
    ~BlobGuard() {
        if (blob.pbData) {
            LocalFree(blob.pbData);
            blob.pbData = nullptr;
        }
    }
};

bool randomBytes(QByteArray* out, int size, QString* errorText)
{
    if (!out || size <= 0) {
        if (errorText) *errorText = QStringLiteral("Invalid random buffer request");
        return false;
    }
    out->resize(size);
    NTSTATUS status = BCryptGenRandom(nullptr,
                                      reinterpret_cast<PUCHAR>(out->data()),
                                      static_cast<ULONG>(out->size()),
                                      BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if (status < 0) {
        if (errorText) *errorText = QStringLiteral("BCryptGenRandom failed: 0x%1").arg(QString::number(static_cast<quint32>(status), 16));
        return false;
    }
    return true;
}

bool protectMasterKeyDpapi(const QByteArray& key, QByteArray* protectedBlob, QString* errorText)
{
    if (key.isEmpty() || !protectedBlob) {
        if (errorText) *errorText = QStringLiteral("Invalid master key input");
        return false;
    }
    DATA_BLOB in{};
    in.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(key.constData()));
    in.cbData = static_cast<DWORD>(key.size());
    BlobGuard out;
    if (!CryptProtectData(&in, L"CarServicePointG master key", nullptr, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &out.blob)) {
        if (errorText) *errorText = QStringLiteral("CryptProtectData failed: %1").arg(GetLastError());
        return false;
    }
    *protectedBlob = QByteArray(reinterpret_cast<const char*>(out.blob.pbData), static_cast<int>(out.blob.cbData));
    return true;
}

bool unprotectMasterKeyDpapi(const QByteArray& protectedBlob, QByteArray* key, QString* errorText)
{
    if (protectedBlob.isEmpty() || !key) {
        if (errorText) *errorText = QStringLiteral("Invalid protected master key blob");
        return false;
    }
    DATA_BLOB in{};
    in.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(protectedBlob.constData()));
    in.cbData = static_cast<DWORD>(protectedBlob.size());
    BlobGuard out;
    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr,
                            CRYPTPROTECT_UI_FORBIDDEN, &out.blob)) {
        if (errorText) *errorText = QStringLiteral("CryptUnprotectData failed: %1").arg(GetLastError());
        return false;
    }
    *key = QByteArray(reinterpret_cast<const char*>(out.blob.pbData), static_cast<int>(out.blob.cbData));
    return true;
}

bool loadOrCreateMasterKey(QByteArray* keyOut, QString* errorText)
{
    if (!keyOut) {
        if (errorText) *errorText = QStringLiteral("Master key output is null");
        return false;
    }

    const QString keyPath = AppRuntime::masterKeyFilePath();
    QFileInfo fi(keyPath);
    QDir().mkpath(fi.absolutePath());

    if (fi.exists()) {
        QFile in(keyPath);
        if (!in.open(QIODevice::ReadOnly)) {
            if (errorText) *errorText = in.errorString();
            return false;
        }
        const QByteArray protectedBlob = in.readAll();
        if (!unprotectMasterKeyDpapi(protectedBlob, keyOut, errorText)) return false;
        if (keyOut->size() != kMasterKeySize) {
            if (errorText) *errorText = QStringLiteral("Invalid master key size in DPAPI blob");
            return false;
        }
        AppRuntime::protectPathAtRest(keyPath);
        return true;
    }

    QByteArray freshKey;
    if (!randomBytes(&freshKey, kMasterKeySize, errorText)) return false;
    QByteArray protectedBlob;
    if (!protectMasterKeyDpapi(freshKey, &protectedBlob, errorText)) return false;
    if (!writeAllBytesAtomic(keyPath, protectedBlob, errorText)) return false;
    *keyOut = freshKey;
    return true;
}

bool aes256GcmEncrypt(const QByteArray& plaintext,
                      const QByteArray& key,
                      QByteArray* nonceOut,
                      QByteArray* ciphertextOut,
                      QByteArray* tagOut,
                      QString* errorText)
{
    if (!nonceOut || !ciphertextOut || !tagOut || key.size() != kMasterKeySize) {
        if (errorText) *errorText = QStringLiteral("Invalid AES-256-GCM encrypt arguments");
        return false;
    }

    if (!randomBytes(nonceOut, kNonceSize, errorText)) return false;
    tagOut->resize(kTagSize);
    ciphertextOut->resize(plaintext.size());

    BCRYPT_ALG_HANDLE hAlg = nullptr;
    BCRYPT_KEY_HANDLE hKey = nullptr;
    PUCHAR keyObject = nullptr;
    DWORD keyObjectLen = 0;
    DWORD dataLen = 0;
    NTSTATUS status = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, nullptr, 0);
    if (status < 0) {
        if (errorText) *errorText = QStringLiteral("BCryptOpenAlgorithmProvider failed: 0x%1").arg(QString::number(static_cast<quint32>(status), 16));
        return false;
    }

    auto cleanup = [&]() {
        if (hKey) BCryptDestroyKey(hKey);
        if (keyObject) delete[] keyObject;
        if (hAlg) BCryptCloseAlgorithmProvider(hAlg, 0);
    };

    status = BCryptSetProperty(hAlg, BCRYPT_CHAINING_MODE,
                               reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),
                               static_cast<ULONG>((wcslen(BCRYPT_CHAIN_MODE_GCM) + 1) * sizeof(wchar_t)), 0);
    if (status < 0) {
        if (errorText) *errorText = QStringLiteral("BCryptSetProperty(GCM) failed: 0x%1").arg(QString::number(static_cast<quint32>(status), 16));
        cleanup();
        return false;
    }

    status = BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&keyObjectLen), sizeof(keyObjectLen), &dataLen, 0);
    if (status < 0 || keyObjectLen == 0) {
        if (errorText) *errorText = QStringLiteral("BCryptGetProperty(OBJECT_LENGTH) failed");
        cleanup();
        return false;
    }
    keyObject = new UCHAR[keyObjectLen];
    status = BCryptGenerateSymmetricKey(hAlg, &hKey, keyObject, keyObjectLen,
                                        reinterpret_cast<PUCHAR>(const_cast<char*>(key.constData())),
                                        static_cast<ULONG>(key.size()), 0);
    if (status < 0) {
        if (errorText) *errorText = QStringLiteral("BCryptGenerateSymmetricKey failed: 0x%1").arg(QString::number(static_cast<quint32>(status), 16));
        cleanup();
        return false;
    }

    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO authInfo;
    BCRYPT_INIT_AUTH_MODE_INFO(authInfo);
    authInfo.pbNonce = reinterpret_cast<PUCHAR>(nonceOut->data());
    authInfo.cbNonce = static_cast<ULONG>(nonceOut->size());
    authInfo.pbTag = reinterpret_cast<PUCHAR>(tagOut->data());
    authInfo.cbTag = static_cast<ULONG>(tagOut->size());

    ULONG resultSize = 0;
    status = BCryptEncrypt(hKey,
                           reinterpret_cast<PUCHAR>(const_cast<char*>(plaintext.constData())),
                           static_cast<ULONG>(plaintext.size()),
                           &authInfo,
                           nullptr, 0,
                           reinterpret_cast<PUCHAR>(ciphertextOut->data()),
                           static_cast<ULONG>(ciphertextOut->size()),
                           &resultSize,
                           0);
    if (status < 0) {
        if (errorText) *errorText = QStringLiteral("BCryptEncrypt failed: 0x%1").arg(QString::number(static_cast<quint32>(status), 16));
        cleanup();
        return false;
    }
    ciphertextOut->resize(static_cast<int>(resultSize));
    cleanup();
    return true;
}

bool aes256GcmDecrypt(const QByteArray& ciphertext,
                      const QByteArray& key,
                      const QByteArray& nonce,
                      const QByteArray& tag,
                      QByteArray* plaintextOut,
                      QString* errorText)
{
    if (!plaintextOut || key.size() != kMasterKeySize || nonce.size() != kNonceSize || tag.size() != kTagSize) {
        if (errorText) *errorText = QStringLiteral("Invalid AES-256-GCM decrypt arguments");
        return false;
    }

    plaintextOut->resize(ciphertext.size());

    BCRYPT_ALG_HANDLE hAlg = nullptr;
    BCRYPT_KEY_HANDLE hKey = nullptr;
    PUCHAR keyObject = nullptr;
    DWORD keyObjectLen = 0;
    DWORD dataLen = 0;
    NTSTATUS status = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, nullptr, 0);
    if (status < 0) {
        if (errorText) *errorText = QStringLiteral("BCryptOpenAlgorithmProvider failed: 0x%1").arg(QString::number(static_cast<quint32>(status), 16));
        return false;
    }

    auto cleanup = [&]() {
        if (hKey) BCryptDestroyKey(hKey);
        if (keyObject) delete[] keyObject;
        if (hAlg) BCryptCloseAlgorithmProvider(hAlg, 0);
    };

    status = BCryptSetProperty(hAlg, BCRYPT_CHAINING_MODE,
                               reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),
                               static_cast<ULONG>((wcslen(BCRYPT_CHAIN_MODE_GCM) + 1) * sizeof(wchar_t)), 0);
    if (status < 0) {
        if (errorText) *errorText = QStringLiteral("BCryptSetProperty(GCM) failed: 0x%1").arg(QString::number(static_cast<quint32>(status), 16));
        cleanup();
        return false;
    }

    status = BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&keyObjectLen), sizeof(keyObjectLen), &dataLen, 0);
    if (status < 0 || keyObjectLen == 0) {
        if (errorText) *errorText = QStringLiteral("BCryptGetProperty(OBJECT_LENGTH) failed");
        cleanup();
        return false;
    }
    keyObject = new UCHAR[keyObjectLen];
    status = BCryptGenerateSymmetricKey(hAlg, &hKey, keyObject, keyObjectLen,
                                        reinterpret_cast<PUCHAR>(const_cast<char*>(key.constData())),
                                        static_cast<ULONG>(key.size()), 0);
    if (status < 0) {
        if (errorText) *errorText = QStringLiteral("BCryptGenerateSymmetricKey failed: 0x%1").arg(QString::number(static_cast<quint32>(status), 16));
        cleanup();
        return false;
    }

    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO authInfo;
    BCRYPT_INIT_AUTH_MODE_INFO(authInfo);
    authInfo.pbNonce = reinterpret_cast<PUCHAR>(const_cast<char*>(nonce.constData()));
    authInfo.cbNonce = static_cast<ULONG>(nonce.size());
    authInfo.pbTag = reinterpret_cast<PUCHAR>(const_cast<char*>(tag.constData()));
    authInfo.cbTag = static_cast<ULONG>(tag.size());

    ULONG resultSize = 0;
    status = BCryptDecrypt(hKey,
                           reinterpret_cast<PUCHAR>(const_cast<char*>(ciphertext.constData())),
                           static_cast<ULONG>(ciphertext.size()),
                           &authInfo,
                           nullptr, 0,
                           reinterpret_cast<PUCHAR>(plaintextOut->data()),
                           static_cast<ULONG>(plaintextOut->size()),
                           &resultSize,
                           0);
    if (status < 0) {
        if (errorText) *errorText = QStringLiteral("BCryptDecrypt failed: 0x%1").arg(QString::number(static_cast<quint32>(status), 16));
        cleanup();
        return false;
    }
    plaintextOut->resize(static_cast<int>(resultSize));
    cleanup();
    return true;
}
#else
bool loadOrCreateMasterKey(QByteArray* keyOut, QString* errorText)
{
    if (!keyOut) {
        if (errorText) *errorText = QStringLiteral("Master key output is null");
        return false;
    }
    keyOut->fill('\0', kMasterKeySize);
    return true;
}
#endif

bool encryptPlainFileToSealed(const QString& plainPath, const QString& sealedPath, QString* errorText)
{
    QFile in(plainPath);
    if (!in.open(QIODevice::ReadOnly)) {
        if (errorText) *errorText = in.errorString();
        return false;
    }
    const QByteArray plaintext = in.readAll();
    in.close();

    QByteArray key;
    if (!loadOrCreateMasterKey(&key, errorText)) return false;

#ifdef Q_OS_WIN
    QByteArray nonce;
    QByteArray ciphertext;
    QByteArray tag;
    if (!aes256GcmEncrypt(plaintext, key, &nonce, &ciphertext, &tag, errorText)) return false;

    QByteArray sealed;
    QDataStream ds(&sealed, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds.writeRawData(kMagic, sizeof(kMagic));
    ds << static_cast<quint32>(kFormatVersion)
       << static_cast<quint32>(nonce.size())
       << static_cast<quint32>(tag.size())
       << static_cast<quint64>(plaintext.size());
    ds.writeRawData(nonce.constData(), nonce.size());
    ds.writeRawData(tag.constData(), tag.size());
    ds.writeRawData(ciphertext.constData(), ciphertext.size());
    return writeAllBytesAtomic(sealedPath, sealed, errorText);
#else
    Q_UNUSED(key);
    return writeAllBytesAtomic(sealedPath, plaintext, errorText);
#endif
}

bool decryptSealedFileToPlain(const QString& sealedPath, const QString& plainPath, QString* errorText)
{
    QFile in(sealedPath);
    if (!in.open(QIODevice::ReadOnly)) {
        if (errorText) *errorText = in.errorString();
        return false;
    }
    const QByteArray sealed = in.readAll();
    in.close();

#ifdef Q_OS_WIN
    if (sealed.size() < static_cast<int>(sizeof(kMagic) + sizeof(quint32) * 3 + sizeof(quint64))) {
        if (errorText) *errorText = QStringLiteral("Encrypted DB container is too small");
        return false;
    }
    QBuffer buffer;
    buffer.setData(sealed);
    buffer.open(QIODevice::ReadOnly);
    QDataStream ds(&buffer);
    ds.setByteOrder(QDataStream::LittleEndian);
    char magic[sizeof(kMagic)]{};
    if (ds.readRawData(magic, sizeof(magic)) != sizeof(magic) || memcmp(magic, kMagic, sizeof(kMagic)) != 0) {
        if (errorText) *errorText = QStringLiteral("Encrypted DB container has invalid magic header");
        return false;
    }

    quint32 version = 0;
    quint32 nonceLen = 0;
    quint32 tagLen = 0;
    quint64 plainSize = 0;
    ds >> version >> nonceLen >> tagLen >> plainSize;
    if (version != kFormatVersion || nonceLen != kNonceSize || tagLen != kTagSize) {
        if (errorText) *errorText = QStringLiteral("Encrypted DB container format is unsupported");
        return false;
    }
    QByteArray nonce(static_cast<int>(nonceLen), Qt::Uninitialized);
    QByteArray tag(static_cast<int>(tagLen), Qt::Uninitialized);
    if (ds.readRawData(nonce.data(), nonce.size()) != nonce.size() || ds.readRawData(tag.data(), tag.size()) != tag.size()) {
        if (errorText) *errorText = QStringLiteral("Encrypted DB container is truncated");
        return false;
    }
    const int headerSize = static_cast<int>(sizeof(kMagic) + sizeof(quint32) * 3 + sizeof(quint64) + nonceLen + tagLen);
    const QByteArray ciphertext = sealed.mid(headerSize);
    QByteArray key;
    if (!loadOrCreateMasterKey(&key, errorText)) return false;
    QByteArray plaintext;
    if (!aes256GcmDecrypt(ciphertext, key, nonce, tag, &plaintext, errorText)) return false;
    if (plainSize != static_cast<quint64>(plaintext.size())) {
        if (errorText) *errorText = QStringLiteral("Encrypted DB container size check failed");
        return false;
    }
    return writeAllBytesAtomic(plainPath, plaintext, errorText);
#else
    return writeAllBytesAtomic(plainPath, sealed, errorText);
#endif
}

bool prepareSqliteForSealing(const QString& dbPath, QString* errorText)
{
    if (!QFileInfo::exists(dbPath)) return true;
    // Refuse before touching WAL. The caller must quiesce all owners; a warning
    // is not permission to seal a database whose committed pages are in WAL.
    if (SqliteConnectionUtils::countOpenConnectionsForPath(dbPath) > 0) {
        if (errorText) *errorText = QStringLiteral("Не можна підготувати БД до шифрування: є відкриті з’єднання (%1)").arg(dbPath);
        return false;
    }
    const QString connName = QStringLiteral("seal_prepare_%1_%2")
        .arg(static_cast<qulonglong>(reinterpret_cast<quintptr>(QThread::currentThread())))
        .arg(QFileInfo(dbPath).fileName());
    bool ok = false;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
        db.setDatabaseName(dbPath);
        if (!db.open()) {
            if (errorText) *errorText = db.lastError().text();
        } else {
            QSqlQuery q(db);
            auto prepare = [&]() -> bool {
                if (!q.exec(QStringLiteral("PRAGMA wal_checkpoint(TRUNCATE)")) || !q.next()
                    || q.value(0).toInt() != 0) {
                    if (errorText) *errorText = QStringLiteral("Не завершено перенесення WAL до БД: %1").arg(q.lastError().text());
                    return false;
                }
                q.finish();
                if (!q.exec(QStringLiteral("PRAGMA journal_mode=DELETE")) || !q.next()
                    || q.value(0).toString().compare(QStringLiteral("delete"), Qt::CaseInsensitive) != 0) {
                    if (errorText) *errorText = QStringLiteral("Не отримано виключний доступ до БД: %1").arg(q.lastError().text());
                    return false;
                }
                q.finish();
                // Our preparation connection is the only permitted owner here.
                if (SqliteConnectionUtils::countOpenConnectionsForPath(dbPath) > 1) {
                    qWarning() << "prepareSqliteForSealing skipped VACUUM due to active SQLite connections" << dbPath;
                    if (errorText) *errorText = QStringLiteral("Під час підготовки БД відкрито інше з’єднання");
                    return false;
                }
                if (!q.exec(QStringLiteral("VACUUM"))) {
                    if (errorText) *errorText = q.lastError().text();
                    return false;
                }
                return true;
            };
            ok = prepare();
            q.finish();
            db.close();
        }
    }
    SqliteConnectionUtils::safeCloseAndRemoveDatabase(connName);
    // SQLite owns WAL/SHM cleanup. Never unlink them ourselves, especially when
    // a checkpoint or an external-process lock prevented preparation.
    return ok;
}

bool ensureLegacyPlainMigration(const DbMapping& mapping, QString* errorText)
{
    const QFileInfo legacyFi(mapping.legacyPlainPath);
    const QFileInfo sealedFi(mapping.sealedPath);
    if (!legacyFi.exists()) return true;
    if (!sealedFi.exists()) {
        if (!encryptPlainFileToSealed(mapping.legacyPlainPath, mapping.sealedPath, errorText)) {
            return false;
        }
    }
    removePlaintextArtifacts(mapping.legacyPlainPath);
    return true;
}

bool consumePendingLegacyImport(QString* errorText)
{
    const QString pendingPath = AppRuntime::pendingLegacyImportFilePath();
    if (!QFileInfo::exists(pendingPath)) return true;

    // A completed legacy import seeds the active workspace staging target.
    // The canonical rewrite/projection pipeline then rebuilds registry/router
    // state from that workspace. Writing the pending import into the archive
    // slot leaves the active runtime absent and breaks post-import reopen.
    safeRemoveEncryptedStorageFile(AppRuntime::sealedActiveDbFilePath(), AppRuntime::securityDir());
    if (!encryptPlainFileToSealed(pendingPath, AppRuntime::sealedActiveDbFilePath(), errorText)) {
        return false;
    }
    safeRemoveEncryptedStorageFile(AppRuntime::sealedArchiveDbFilePath(), AppRuntime::securityDir());
    safeRemoveEncryptedStorageFile(AppRuntime::sealedRegistryDbFilePath(), AppRuntime::securityDir());
    safeRemoveEncryptedStorageFile(AppRuntime::sealedLegacyDbFilePath(), AppRuntime::securityDir());
    removePlaintextArtifacts(AppRuntime::activeDbFilePath());
    removePlaintextArtifacts(AppRuntime::archiveDbFilePath());
    removePlaintextArtifacts(AppRuntime::registryDbFilePath());
    removePlaintextArtifacts(AppRuntime::legacyDbFilePath());
    safeRemoveEncryptedStorageFile(pendingPath, QFileInfo(pendingPath).absolutePath());
    return true;
}

bool exportPlainFromRuntimeOrSealed(const QString& runtimePath, const QString& sealedPath, const QString& targetPlainPath, QString* errorText)
{
    if (QFileInfo::exists(runtimePath)) {
        QDir().mkpath(QFileInfo(targetPlainPath).absolutePath());
        safeRemoveEncryptedStorageFile(targetPlainPath, QFileInfo(targetPlainPath).absolutePath());
        if (!QFile::copy(runtimePath, targetPlainPath)) {
            if (errorText) *errorText = QStringLiteral("Не вдалося скопіювати runtime БД у бекап");
            return false;
        }
        return true;
    }
    return decryptSealedFileToPlain(sealedPath, targetPlainPath, errorText);
}

} // namespace EncryptedStorage

