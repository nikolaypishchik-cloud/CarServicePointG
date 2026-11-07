// Extracted from src/app/update/UpdateServiceSupport.cpp for P1/P2 monolith reduction.
// Included exactly once by the original translation unit/header; do not compile separately.

namespace UpdateServiceSupport {
QString escapeWinPath(QString s)
{
    return s.replace('/', '\\');
}

QString psSingleQuoted(QString s)
{
    s.replace(QStringLiteral("'"), QStringLiteral("''"));
    return QStringLiteral("'") + s + QStringLiteral("'");
}

QString firstNonEmptyVersionText(const QString& a, const QString& b)
{
    const QString at = a.trimmed();
    if (!at.isEmpty()) return at;
    return b.trimmed();
}

QString readLastLogLines(const QString& path, int maxLines)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return QString();
    const QString text = QString::fromUtf8(f.readAll());
    const QStringList lines = text.split(QRegularExpression(QStringLiteral("[\r\n]+")), Qt::SkipEmptyParts);
    const int start = qMax(0, lines.size() - qMax(1, maxLines));
    return lines.mid(start).join(QChar('\n'));
}

QString updateRuntimeDirPath()
{
    const QString dirPath = QDir(App::dataDir()).filePath(QStringLiteral("updates/runtime"));
    QDir().mkpath(dirPath);
    return dirPath;
}

QString updateRuntimeLogPath()
{
    return QDir(updateRuntimeDirPath()).filePath(QStringLiteral("apply_update.log"));
}

QString updateLatestStatusPath()
{
    return QDir(updateRuntimeDirPath()).filePath(QStringLiteral("update_status.txt"));
}

QString updateLockStatePath()
{
    return QDir(updateRuntimeDirPath()).filePath(QStringLiteral("update_lock_state.json"));
}

QString programDataUpdateRootPath()
{
    const QString programData = qEnvironmentVariable("ProgramData").trimmed();
    if (programData.isEmpty()) return QString();
    return QDir(programData).filePath(QStringLiteral("CarServicePointG"));
}

QString programDataOrRuntimePath(const QString& fileName)
{
    const QString dirPath = programDataUpdateRootPath();
    if (!dirPath.isEmpty() && QDir().mkpath(dirPath)) return QDir(dirPath).filePath(fileName);
    return QDir(updateRuntimeDirPath()).filePath(fileName);
}

QString updateTransactionPath()
{
    return programDataOrRuntimePath(QStringLiteral("update_transaction.json"));
}

QString failedUpdatePackagesPath()
{
    return programDataOrRuntimePath(QStringLiteral("failed_update_packages.json"));
}

QString updateFileLockPath()
{
    return QDir(updateRuntimeDirPath()).filePath(QStringLiteral("update_once_global.lock"));
}

bool atomicWriteJsonFile(const QString& path, const QJsonObject& obj)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    f.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    return f.commit();
}

QJsonObject readJsonObjectFile(const QString& path)
{
    QFile f(path);
    if (!f.exists() || !f.open(QIODevice::ReadOnly)) return {};
    QJsonParseError pe{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) return {};
    return doc.object();
}

bool isTerminalUpdateState(const QString& state)
{
    return state.compare(QStringLiteral("success"), Qt::CaseInsensitive) == 0
        || state.compare(QStringLiteral("failed"), Qt::CaseInsensitive) == 0
        || state.compare(QStringLiteral("cancelled"), Qt::CaseInsensitive) == 0;
}

QString makeUpdateRunId()
{
    return QStringLiteral("%1_pid%2")
        .arg(QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd_HHmmss_zzz")))
        .arg(QCoreApplication::applicationPid());
}

QString updateRunDirPath(const QString& runId)
{
    const QString safeRunId = QString(runId).replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_.-]")), QStringLiteral("_"));
    const QString dirPath = QDir(updateRuntimeDirPath()).filePath(QStringLiteral("runs/%1").arg(safeRunId.isEmpty() ? makeUpdateRunId() : safeRunId));
    QDir().mkpath(dirPath);
    return dirPath;
}

QAtomicInteger<int> s_updateFlowActive {0};

void appendUpdateRuntimeLog(const QString& stage, const QString& message);

bool isMainQtThread()
{
    return QCoreApplication::instance()
        && QThread::currentThread() == QCoreApplication::instance()->thread();
}

QStringList updateAllowedDeleteRoots()
{
    QStringList roots;
    const QString runtimeRoot = SafeFileOps::cleanPath(updateRuntimeDirPath());
    const QString downloadRoot = SafeFileOps::cleanPath(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));
    const QString programDataRoot = SafeFileOps::cleanPath(programDataUpdateRootPath());
    if (!runtimeRoot.trimmed().isEmpty()) roots << runtimeRoot;
    if (!downloadRoot.trimmed().isEmpty()) roots << downloadRoot;
    if (!programDataRoot.trimmed().isEmpty()) roots << programDataRoot;
    return roots;
}

bool safeRemoveUpdateFile(const QString& path,
                          const QStringList& prefixes = {},
                          const QStringList& suffixes = {},
                          QString* errorText = nullptr)
{
    QStringList effectiveSuffixes = suffixes;
    if (effectiveSuffixes.isEmpty()) {
        effectiveSuffixes << QStringLiteral(".log") << QStringLiteral(".json") << QStringLiteral(".tmp")
                          << QStringLiteral(".lock") << QStringLiteral(".part") << QStringLiteral(".exe")
                          << QStringLiteral(".zip") << QStringLiteral(".sha256") << QStringLiteral(".txt");
    }
    return SafeFileOps::safeRemoveFile(path, updateAllowedDeleteRoots(), prefixes, effectiveSuffixes, errorText);
}

bool processExistsByPid(qint64 pid)
{
    if (pid <= 0) return false;
#ifdef Q_OS_WIN
    // Non-blocking WinAPI PID probe. Do not use tasklist/QProcess here: it can
    // block the UI and previously forced stale update transactions to look active.
    const DWORD processId = static_cast<DWORD>(pid);
    HANDLE handle = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (handle == nullptr) {
        const DWORD err = GetLastError();
        const bool accessDeniedMeansAlive = (err == ERROR_ACCESS_DENIED);
        appendUpdateRuntimeLog(QStringLiteral("PID_PROBE_WINAPI"),
                               QStringLiteral("OpenProcess pid=%1 result=%2 error=%3")
                                   .arg(pid)
                                   .arg(accessDeniedMeansAlive ? QStringLiteral("alive_access_denied") : QStringLiteral("not_found"))
                                   .arg(static_cast<unsigned long>(err)));
        return accessDeniedMeansAlive;
    }

    DWORD exitCode = 0;
    const BOOL ok = GetExitCodeProcess(handle, &exitCode);
    CloseHandle(handle);
    if (!ok) {
        appendUpdateRuntimeLog(QStringLiteral("PID_PROBE_WINAPI"),
                               QStringLiteral("GetExitCodeProcess failed for pid=%1; keeping fail-safe alive").arg(pid));
        return true;
    }
    return exitCode == STILL_ACTIVE;
#else
    errno = 0;
    return (::kill(static_cast<pid_t>(pid), 0) == 0) || errno == EPERM;
#endif
}

void pruneArchivedUpdateLogs(int keepCount = 20)
{
    const QString dirPath = QDir(updateRuntimeDirPath()).filePath(QStringLiteral("logs"));
    QDir dir(dirPath);
    const QFileInfoList logs = dir.entryInfoList({QStringLiteral("apply_update_*.log")}, QDir::Files, QDir::Time);
    for (int i = qMax(0, keepCount); i < logs.size(); ++i) {
        safeRemoveUpdateFile(logs.at(i).absoluteFilePath(), {QStringLiteral("apply_update_")}, {QStringLiteral(".log")});
    }
}

QString normalizeSha256Digest(QString s);
int compareVersions(const QString& a, const QString& b);

bool hasFreshHeartbeat(const QJsonObject& obj, int maxAgeSeconds)
{
    const QString heartbeatText = obj.value(QStringLiteral("heartbeat_utc")).toString(
        obj.value(QStringLiteral("updated_at_utc")).toString());
    const QDateTime heartbeat = QDateTime::fromString(heartbeatText, Qt::ISODateWithMs);
    if (!heartbeat.isValid()) return false;
    return heartbeat.toUTC().secsTo(QDateTime::currentDateTimeUtc()) <= maxAgeSeconds;
}

qint64 jsonPid(const QJsonObject& obj, const QString& key)
{
    return static_cast<qint64>(obj.value(key).toDouble());
}

bool hasLivePidForKeys(const QJsonObject& obj, const QList<QString>& pidKeys)
{
    for (const QString& key : pidKeys) {
        const qint64 pid = jsonPid(obj, key);
        if (pid > 0 && processExistsByPid(pid)) return true;
    }
    return false;
}

bool hasLiveUpdateWorker(const QJsonObject& obj)
{
    return hasLivePidForKeys(obj, {
        QStringLiteral("updater_pid"),
        QStringLiteral("installer_pid")
    });
}

bool hasLiveUpdateOwnerApp(const QJsonObject& obj)
{
    return hasLivePidForKeys(obj, {
        QStringLiteral("app_pid"),
        QStringLiteral("pid")
    });
}

bool isHandoffOwnedUpdateStage(const QString& stageOrState)
{
    const QString s = stageOrState.trimmed().toLower();
    return s.contains(QStringLiteral("handoff"))
        || s.contains(QStringLiteral("elevat"))
        || s.contains(QStringLiteral("install"))
        || s.contains(QStringLiteral("app_exited"))
        || s.contains(QStringLiteral("resources_unlocked"))
        || s.contains(QStringLiteral("post_install"))
        || s.contains(QStringLiteral("shortcut_refresh"))
        || s.contains(QStringLiteral("starting_updated_app"));
}

bool removeStaleUpdateTransaction(const QString& reason, QString* reasonOut)
{
    const QString path = updateTransactionPath();
    QString removeError;
    const bool removed = safeRemoveUpdateFile(path, {QStringLiteral("update_")}, {QStringLiteral(".json")}, &removeError);
    const QString finalReason = removed || !QFileInfo::exists(path)
        ? reason
        : QStringLiteral("%1; stale update transaction cleanup failed: %2").arg(reason, removeError);
    appendUpdateRuntimeLog(QStringLiteral("CHECK_UPDATE"), finalReason);
    if (reasonOut) *reasonOut = finalReason;
    return false;
}

bool isFreshUpdateLockState(QString* reasonOut)
{
    const QString path = updateLockStatePath();
    QFileInfo info(path);
    if (!info.exists()) {
        const QJsonObject tx = readJsonObjectFile(updateTransactionPath());
        const QString txState = tx.value(QStringLiteral("state")).toString();
        if (!txState.isEmpty() && !isTerminalUpdateState(txState)) {
            const bool workerAlive = hasLiveUpdateWorker(tx);
            const bool ownerAppAlive = hasLiveUpdateOwnerApp(tx);
            const bool heartbeatFresh = hasFreshHeartbeat(tx, 20 * 60);
            const bool handoffOwned = isHandoffOwnedUpdateStage(txState);
            if (workerAlive || (ownerAppAlive && heartbeatFresh) || (handoffOwned && heartbeatFresh)) {
                if (reasonOut) *reasonOut = QStringLiteral("active update transaction state=%1 app_pid=%2 updater_pid=%3 installer_pid=%4 heartbeat=%5 path=%6")
                    .arg(txState)
                    .arg(jsonPid(tx, QStringLiteral("app_pid")))
                    .arg(jsonPid(tx, QStringLiteral("updater_pid")))
                    .arg(jsonPid(tx, QStringLiteral("installer_pid")))
                    .arg(tx.value(QStringLiteral("heartbeat_utc")).toString())
                    .arg(updateTransactionPath());
                return true;
            }

            return removeStaleUpdateTransaction(
                QStringLiteral("stale update transaction was removed: state=%1 app_pid=%2 updater_pid=%3 installer_pid=%4 heartbeat_fresh=%5 path=%6")
                    .arg(txState)
                    .arg(jsonPid(tx, QStringLiteral("app_pid")))
                    .arg(jsonPid(tx, QStringLiteral("updater_pid")))
                    .arg(jsonPid(tx, QStringLiteral("installer_pid")))
                    .arg(heartbeatFresh ? QStringLiteral("true") : QStringLiteral("false"))
                    .arg(updateTransactionPath()),
                reasonOut);
        }
        return false;
    }

    const qint64 ageSeconds = info.lastModified().secsTo(QDateTime::currentDateTime());
    const QJsonObject obj = readJsonObjectFile(path);
    const QString runId = obj.value(QStringLiteral("run_id")).toString();
    const QString stage = obj.value(QStringLiteral("stage")).toString();
    const QString owner = obj.value(QStringLiteral("owner")).toString();
    const qint64 appPid = static_cast<qint64>(obj.value(QStringLiteral("app_pid")).toDouble(obj.value(QStringLiteral("pid")).toDouble()));
    const qint64 updaterPid = static_cast<qint64>(obj.value(QStringLiteral("updater_pid")).toDouble());
    const qint64 installerPid = static_cast<qint64>(obj.value(QStringLiteral("installer_pid")).toDouble());
    const bool workerAlive = hasLiveUpdateWorker(obj);
    const bool ownerAppAlive = hasLiveUpdateOwnerApp(obj);
    const bool heartbeatFresh = hasFreshHeartbeat(obj, 20 * 60);
    const bool handoffOwned = owner.compare(QStringLiteral("updater"), Qt::CaseInsensitive) == 0
        || owner.compare(QStringLiteral("installer"), Qt::CaseInsensitive) == 0
        || isHandoffOwnedUpdateStage(stage);

    // IMPORTANT: after handoff the old app PID is expected to die. The lock must
    // not be treated as stale while the updater/installer is alive or the handoff
    // heartbeat is still fresh. App-owned checking/downloading locks must not block
    // forever after a crash.
    if (handoffOwned && (workerAlive || heartbeatFresh)) {
        if (reasonOut) *reasonOut = QStringLiteral("active handoff/update lock run_id=%1 owner=%2 stage=%3 app_pid=%4 updater_pid=%5 installer_pid=%6 heartbeat_fresh=%7 age=%8s path=%9")
            .arg(runId.isEmpty() ? QStringLiteral("<unknown>") : runId,
                 owner.isEmpty() ? QStringLiteral("<unknown>") : owner,
                 stage.isEmpty() ? QStringLiteral("<unknown>") : stage)
            .arg(appPid)
            .arg(updaterPid)
            .arg(installerPid)
            .arg(heartbeatFresh ? QStringLiteral("true") : QStringLiteral("false"))
            .arg(ageSeconds)
            .arg(path);
        return true;
    }

    if (!handoffOwned && !workerAlive && !ownerAppAlive) {
        safeRemoveUpdateFile(path);
        if (reasonOut) {
            *reasonOut = QStringLiteral("stale update lock was removed because app pid is not active and app-owned update process is not active: run_id=%1 stage=%2 app_pid=%3 age=%4s heartbeat_fresh=%5 path=%6")
                .arg(runId.isEmpty() ? QStringLiteral("<unknown>") : runId,
                     stage.isEmpty() ? QStringLiteral("<unknown>") : stage)
                .arg(appPid)
                .arg(ageSeconds)
                .arg(heartbeatFresh ? QStringLiteral("true") : QStringLiteral("false"))
                .arg(path);
        }
        return false;
    }

    if (ageSeconds > 30 * 60 && !workerAlive && !heartbeatFresh) {
        safeRemoveUpdateFile(path);
        if (reasonOut) *reasonOut = QStringLiteral("stale update lock was removed: run_id=%1 stage=%2 owner=%3 age=%4s no live updater/installer heartbeat path=%5")
            .arg(runId.isEmpty() ? QStringLiteral("<unknown>") : runId,
                 stage.isEmpty() ? QStringLiteral("<unknown>") : stage,
                 owner.isEmpty() ? QStringLiteral("<unknown>") : owner)
            .arg(ageSeconds)
            .arg(path);
        return false;
    }

    if (reasonOut) {
        *reasonOut = QStringLiteral("active update lock run_id=%1 owner=%2 stage=%3 app_pid=%4 updater_pid=%5 installer_pid=%6 heartbeat=%7 age=%8s path=%9")
            .arg(runId.isEmpty() ? QStringLiteral("<unknown>") : runId,
                 owner.isEmpty() ? QStringLiteral("app") : owner,
                 stage.isEmpty() ? QStringLiteral("<unknown>") : stage)
            .arg(appPid)
            .arg(updaterPid)
            .arg(installerPid)
            .arg(obj.value(QStringLiteral("heartbeat_utc")).toString())
            .arg(ageSeconds)
            .arg(path);
    }
    return true;
}

void writeUpdateLockState(const QString& runId,
                          const QString& stage,
                          const QString& assetName = QString(),
                          const QString& expectedVersion = QString(),
                          const QString& assetSha256 = QString(),
                          const QString& owner = QString(),
                          qint64 updaterPid = 0,
                          qint64 installerPid = 0)
{
    const QString path = updateLockStatePath();
    const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    QJsonObject obj = readJsonObjectFile(path);
    const QString previousRunId = obj.value(QStringLiteral("run_id")).toString(obj.value(QStringLiteral("transaction_id")).toString());
    if (!previousRunId.isEmpty() && previousRunId != runId) obj.remove(QStringLiteral("started_at_utc"));
    obj.insert(QStringLiteral("run_id"), runId);
    obj.insert(QStringLiteral("transaction_id"), runId);
    obj.insert(QStringLiteral("pid"), static_cast<double>(QCoreApplication::applicationPid()));
    obj.insert(QStringLiteral("app_pid"), static_cast<double>(QCoreApplication::applicationPid()));
    obj.insert(QStringLiteral("updater_pid"), static_cast<double>(qMax<qint64>(0, updaterPid)));
    obj.insert(QStringLiteral("installer_pid"), static_cast<double>(qMax<qint64>(0, installerPid)));
    obj.insert(QStringLiteral("owner"), owner.trimmed().isEmpty() ? QStringLiteral("app") : owner);
    obj.insert(QStringLiteral("stage"), stage);
    obj.insert(QStringLiteral("asset_name"), assetName);
    obj.insert(QStringLiteral("expected_version"), expectedVersion);
    obj.insert(QStringLiteral("asset_sha256"), assetSha256);
    if (!obj.contains(QStringLiteral("started_at_utc"))) obj.insert(QStringLiteral("started_at_utc"), now);
    obj.insert(QStringLiteral("updated_at_utc"), now);
    obj.insert(QStringLiteral("heartbeat_utc"), now);
    obj.insert(QStringLiteral("lock_path"), path);
    obj.insert(QStringLiteral("transaction_path"), updateTransactionPath());
    atomicWriteJsonFile(path, obj);
}

void writeUpdateTransactionState(const QString& runId,
                                 const QString& state,
                                 const QString& currentVersion = QString(),
                                 const QString& expectedVersion = QString(),
                                 const QString& assetName = QString(),
                                 const QString& assetSha256 = QString(),
                                 const QString& packagePath = QString(),
                                 qint64 updaterPid = 0,
                                 qint64 installerPid = 0,
                                 const QString& lastError = QString())
{
    const QString path = updateTransactionPath();
    const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    QJsonObject obj = readJsonObjectFile(path);
    const QString previousRunId = obj.value(QStringLiteral("transaction_id")).toString(obj.value(QStringLiteral("run_id")).toString());
    if (!previousRunId.isEmpty() && previousRunId != runId) obj.remove(QStringLiteral("started_at_utc"));
    obj.insert(QStringLiteral("transaction_id"), runId);
    obj.insert(QStringLiteral("state"), state);
    obj.insert(QStringLiteral("current_version"), currentVersion);
    obj.insert(QStringLiteral("expected_version"), expectedVersion);
    obj.insert(QStringLiteral("asset_name"), assetName);
    obj.insert(QStringLiteral("asset_sha256"), assetSha256);
    obj.insert(QStringLiteral("package_path"), packagePath);
    obj.insert(QStringLiteral("app_pid"), static_cast<double>(QCoreApplication::applicationPid()));
    obj.insert(QStringLiteral("updater_pid"), static_cast<double>(qMax<qint64>(0, updaterPid)));
    obj.insert(QStringLiteral("installer_pid"), static_cast<double>(qMax<qint64>(0, installerPid)));
    if (!obj.contains(QStringLiteral("started_at_utc"))) obj.insert(QStringLiteral("started_at_utc"), now);
    obj.insert(QStringLiteral("updated_at_utc"), now);
    obj.insert(QStringLiteral("heartbeat_utc"), now);
    if (!lastError.isEmpty()) {
        obj.insert(QStringLiteral("last_error"), lastError);
    } else {
        obj.remove(QStringLiteral("last_error"));
    }
    if (isTerminalUpdateState(state)) {
        obj.insert(QStringLiteral("finished_at_utc"), now);
    } else {
        obj.remove(QStringLiteral("finished_at_utc"));
    }
    atomicWriteJsonFile(path, obj);
}

bool clearUpdateFileLock()
{
    safeRemoveUpdateFile(updateFileLockPath(), {QStringLiteral("update_")}, {QStringLiteral(".lock")});
    return true;
}

bool acquireUpdateFileLock(const QString& runId, QString* reasonOut)
{
    const QString path = updateFileLockPath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    if (QFileInfo::exists(path)) {
        const QJsonObject obj = readJsonObjectFile(path);
        const QString state = obj.value(QStringLiteral("state")).toString();
        const bool heartbeatFresh = hasFreshHeartbeat(obj, 20 * 60);
        const bool workerAlive = hasLiveUpdateWorker(obj);
        const bool ownerAppAlive = hasLiveUpdateOwnerApp(obj);
        const bool handoffOwned = isHandoffOwnedUpdateStage(state);
        const bool active = workerAlive || (ownerAppAlive && heartbeatFresh) || (handoffOwned && heartbeatFresh);
        if (active) {
            if (reasonOut) *reasonOut = QStringLiteral("update once file lock is active: run_id=%1 state=%2 app_pid=%3 updater_pid=%4 installer_pid=%5 heartbeat_fresh=%6 path=%7")
                .arg(obj.value(QStringLiteral("run_id")).toString(), state)
                .arg(jsonPid(obj, QStringLiteral("app_pid")))
                .arg(jsonPid(obj, QStringLiteral("updater_pid")))
                .arg(jsonPid(obj, QStringLiteral("installer_pid")))
                .arg(heartbeatFresh ? QStringLiteral("true") : QStringLiteral("false"))
                .arg(path);
            return false;
        }
        safeRemoveUpdateFile(path);
        appendUpdateRuntimeLog(QStringLiteral("CHECK_UPDATE"),
                               QStringLiteral("stale update once file lock was removed: run_id=%1 state=%2 app_pid=%3 updater_pid=%4 installer_pid=%5 heartbeat_fresh=%6 path=%7")
                                   .arg(obj.value(QStringLiteral("run_id")).toString(), state)
                                   .arg(jsonPid(obj, QStringLiteral("app_pid")))
                                   .arg(jsonPid(obj, QStringLiteral("updater_pid")))
                                   .arg(jsonPid(obj, QStringLiteral("installer_pid")))
                                   .arg(heartbeatFresh ? QStringLiteral("true") : QStringLiteral("false"))
                                   .arg(path));
    }
    QJsonObject obj;
    const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    obj.insert(QStringLiteral("run_id"), runId);
    obj.insert(QStringLiteral("state"), QStringLiteral("flow_created"));
    obj.insert(QStringLiteral("app_pid"), static_cast<double>(QCoreApplication::applicationPid()));
    obj.insert(QStringLiteral("heartbeat_utc"), now);
    obj.insert(QStringLiteral("created_at_utc"), now);
    if (!atomicWriteJsonFile(path, obj)) {
        if (reasonOut) *reasonOut = QStringLiteral("cannot create update once file lock: %1").arg(path);
        return false;
    }
    return true;
}

void clearUpdateLockState(const QString& runId = QString(), bool force = false)
{
    const QString path = updateLockStatePath();
    bool canClearNonHandoffState = true;
    if (QFileInfo::exists(path)) {
        const QJsonObject obj = readJsonObjectFile(path);
        const QString stage = obj.value(QStringLiteral("stage")).toString();
        const QString owner = obj.value(QStringLiteral("owner")).toString();
        const QString lockedRunId = obj.value(QStringLiteral("run_id")).toString();
        const bool handoffOwned = owner.compare(QStringLiteral("updater"), Qt::CaseInsensitive) == 0
            || owner.compare(QStringLiteral("installer"), Qt::CaseInsensitive) == 0
            || isHandoffOwnedUpdateStage(stage);
        if (!force && handoffOwned) return;
        if (!runId.trimmed().isEmpty() && !lockedRunId.isEmpty() && lockedRunId != runId) return;
        safeRemoveUpdateFile(path);
        canClearNonHandoffState = !handoffOwned;
    }
    clearUpdateFileLock();

    const QString txPath = updateTransactionPath();
    if (QFileInfo::exists(txPath)) {
        const QJsonObject tx = readJsonObjectFile(txPath);
        const QString txRunId = tx.value(QStringLiteral("transaction_id")).toString(tx.value(QStringLiteral("run_id")).toString());
        const QString txState = tx.value(QStringLiteral("state")).toString();
        const bool txHandoffOwned = isHandoffOwnedUpdateStage(txState);
        const bool sameRun = runId.trimmed().isEmpty() || txRunId.isEmpty() || txRunId == runId;
        if (force || (sameRun && canClearNonHandoffState && !txHandoffOwned)) {
            safeRemoveUpdateFile(txPath, {QStringLiteral("update_")}, {QStringLiteral(".json")});
        }
    }
}

bool isFailedPackageBlocked(const QString& assetSha256, const QString& expectedVersion, QString* reasonOut)
{
    const QString sha = normalizeSha256Digest(assetSha256);
    if (sha.isEmpty()) return false;
    const QJsonObject root = readJsonObjectFile(failedUpdatePackagesPath());
    const QJsonArray failures = root.value(QStringLiteral("failures")).toArray();
    const QDateTime now = QDateTime::currentDateTimeUtc();
    for (const QJsonValue& v : failures) {
        const QJsonObject item = v.toObject();
        if (normalizeSha256Digest(item.value(QStringLiteral("asset_sha256")).toString()) != sha) continue;
        const QString itemVersion = item.value(QStringLiteral("expected_version")).toString();
        if (!expectedVersion.trimmed().isEmpty() && !itemVersion.trimmed().isEmpty() && compareVersions(itemVersion, expectedVersion) != 0) continue;
        const QDateTime blockUntil = QDateTime::fromString(item.value(QStringLiteral("retry_block_until_utc")).toString(), Qt::ISODateWithMs);
        if (blockUntil.isValid() && now < blockUntil.toUTC()) {
            if (reasonOut) *reasonOut = QStringLiteral("asset_sha256=%1 expected=%2 blocked_until=%3 reason=%4")
                .arg(sha, itemVersion, blockUntil.toUTC().toString(Qt::ISODate), item.value(QStringLiteral("failure_reason")).toString());
            return true;
        }
    }
    return false;
}

void recordFailedUpdatePackage(const QString& assetSha256, const QString& assetName, const QString& expectedVersion, const QString& failureReason)
{
    const QString sha = normalizeSha256Digest(assetSha256);
    if (sha.isEmpty()) return;
    QJsonObject root = readJsonObjectFile(failedUpdatePackagesPath());
    QJsonArray failures = root.value(QStringLiteral("failures")).toArray();
    QJsonArray updated;
    int attemptCount = 0;
    for (const QJsonValue& v : failures) {
        QJsonObject item = v.toObject();
        if (normalizeSha256Digest(item.value(QStringLiteral("asset_sha256")).toString()) == sha) {
            attemptCount = qMax(attemptCount, item.value(QStringLiteral("attempt_count")).toInt());
            continue;
        }
        updated.append(item);
    }
    const QDateTime now = QDateTime::currentDateTimeUtc();
    QJsonObject item;
    item.insert(QStringLiteral("asset_sha256"), sha);
    item.insert(QStringLiteral("asset_name"), assetName);
    item.insert(QStringLiteral("expected_version"), expectedVersion);
    item.insert(QStringLiteral("failure_reason"), failureReason);
    item.insert(QStringLiteral("failed_at_utc"), now.toString(Qt::ISODate));
    item.insert(QStringLiteral("retry_block_until_utc"), now.addSecs(24 * 60 * 60).toString(Qt::ISODate));
    item.insert(QStringLiteral("attempt_count"), attemptCount + 1);
    updated.append(item);
    root.insert(QStringLiteral("failures"), updated);
