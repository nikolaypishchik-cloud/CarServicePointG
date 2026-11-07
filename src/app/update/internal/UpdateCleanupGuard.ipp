// Extracted from src/app/update/UpdateServiceSupport.cpp for P1/P2 monolith reduction.
// Included exactly once by the original translation unit/header; do not compile separately.

                                   QStringLiteral("sha256_missing: checksum file does not contain SHA256 for asset=%1 checksumAsset=%2")
                                       .arg(m_info.assetName, m_info.checksumAssetName));
            showWarning(QString::fromUtf8("Оновлення"),
                        QString::fromUtf8("SHA256-файл релізу не містить коректного хеша для пакета %1. Оновлення скасовано.").arg(m_info.assetName));
            finish();
            return;
        }

        appendUpdateRuntimeLog(QStringLiteral("VERIFY_SHA256"),
                               QStringLiteral("expected SHA256 resolved from checksum asset=%1 sha=%2")
                                   .arg(m_info.checksumAssetName, m_info.expectedSha256));
        QString failedCooldownReason;
        if (isFailedPackageBlocked(m_info.expectedSha256, m_info.latestVersion, &failedCooldownReason)) {
            appendUpdateRuntimeLog(QStringLiteral("VERIFY_SHA256"), QStringLiteral("UPDATE_SKIP_FAILED_PACKAGE_COOLDOWN: %1").arg(failedCooldownReason));
            if (m_silentIfNoUpdates) {
                finish();
                return;
            }
            appendUpdateRuntimeLog(QStringLiteral("VERIFY_SHA256"), QStringLiteral("Manual update retry override after failed-package cooldown warning: %1").arg(failedCooldownReason));
        }
        if (compareVersions(m_info.currentVersion, m_info.latestVersion) == 0) {
            appendUpdateRuntimeLog(QStringLiteral("VERIFY_SHA256"),
                                   QStringLiteral("UPDATE_SKIP_SAME_VERSION_AFTER_SHA: current=%1 latest=%2 packageSha=%3 marker=%4 state=%5")
                                       .arg(m_info.currentVersion, m_info.latestVersion, m_info.expectedSha256,
                                            appliedUpdateMarkerPath(), updateStatePath()));
            if (!m_silentIfNoUpdates && !m_closingDown) {
                UpdateDialog dlg(m_parent.data());
                dlg.setInfoMode(QString::fromUtf8("Оновлення не потрібне"),
                                QString::fromUtf8("У вас уже встановлена актуальна версія %1.").arg(m_info.currentVersion),
                                QString::fromUtf8("Закрити"),
                                true);
                dlg.exec();
            }
            finish();
            return;
        }
        if (m_promptAfterChecksum) {
            m_promptAfterChecksum = false;
            if (!askToDownload()) {
                finish();
                return;
            }
        }

        startDownload();
    }

    void startDownload()
    {
        m_timedOut = false;
        m_info.expectedSha256 = normalizeSha256Digest(m_info.expectedSha256);
        if (m_info.expectedSha256.isEmpty()) {
            appendUpdateRuntimeLog(QStringLiteral("UPDATE_FAILED"),
                                   QStringLiteral("sha256_missing: expected SHA256 is empty or malformed before installer download asset=%1 checksumAsset=%2")
                                       .arg(m_info.assetName, m_info.checksumAssetName));
            showWarning(QString::fromUtf8("Оновлення"),
                        QString::fromUtf8("Очікуваний SHA-256 для пакета оновлення відсутній або має некоректний формат. Оновлення скасовано."));
            finish();
            return;
        }
        const QString dirPath = m_runDir;
        QDir().mkpath(dirPath);
        m_finalDownloadedFile = QDir(dirPath).filePath(safeUpdatePackageFileName(m_info.assetName.isEmpty() ? QStringLiteral("update_package.exe") : m_info.assetName));
        m_downloadedFile = m_finalDownloadedFile + QStringLiteral(".part");
        writeUpdateLockState(m_runId, QStringLiteral("download_installer"), m_info.assetName, m_info.latestVersion, m_info.expectedSha256);
        writeUpdateTransactionState(m_runId, QStringLiteral("downloading"), m_info.currentVersion, m_info.latestVersion, m_info.assetName, m_info.expectedSha256, m_finalDownloadedFile);
        appendUpdateRuntimeLog(QStringLiteral("DOWNLOAD_INSTALLER"),
                               QStringLiteral("start asset=%1 url=%2 part=%3 final=%4 expectedSize=%5 expectedSha=%6")
                                   .arg(m_info.assetName, m_info.downloadUrl, m_downloadedFile, m_finalDownloadedFile)
                                   .arg(m_info.expectedDownloadSize)
                                   .arg(m_info.expectedSha256));
        safeRemoveUpdateFile(m_downloadedFile, {}, {QStringLiteral(".part")});

        m_output = std::make_unique<QFile>(m_downloadedFile);
        if (!m_output->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            appendUpdateRuntimeLog(QStringLiteral("UPDATE_FAILED"),
                                   QStringLiteral("cannot create temporary update file: %1").arg(m_downloadedFile));
            showWarning(QString::fromUtf8("Оновлення"),
                        QString::fromUtf8("Не вдалося створити тимчасовий файл оновлення.\n%1").arg(m_downloadedFile));
            finish();
            return;
        }

        m_progress = new UpdateDialog(m_parent.data());
        m_progress->setAttribute(Qt::WA_DeleteOnClose, false);
        m_progress->setModal(true);
        m_progress->setProgressMode(QString::fromUtf8("Завантаження оновлення"),
                                    QString::fromUtf8("Підготовка захищеного завантаження пакета релізу з SHA-256 перевіркою..."),
                                    QString(),
                                    QString::fromUtf8("Скасувати"));
        m_progress->setProgressValue(0);
        m_progress->show();

        QNetworkRequest req{QUrl(m_info.downloadUrl)};
        configureDownloadRequest(req);
        m_reply = m_nam.get(req);
        connect(m_progress->secondaryButton(), &QPushButton::clicked, m_reply, &QNetworkReply::abort);
        connect(m_reply, &QIODevice::readyRead, this, [this]() {
            if (m_output) m_output->write(m_reply->readAll());
        });
        connect(m_reply, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64 total) {
            if (!m_progress) return;
            if (total > 0) {
                const int percent = int((received * 100) / total);
                m_progress->setProgressIndeterminate(false);
                m_progress->setProgressValue(percent);
                m_progress->setStatusText(QString::fromUtf8("Завантажено %1% із перевіреного пакета релізу.").arg(percent));
            } else {
                m_progress->setProgressIndeterminate(true);
                m_progress->setStatusText(QString::fromUtf8("Отримання пакета релізу без відомого розміру..."));
            }
        });
        connect(&m_timeout, &QTimer::timeout, m_reply, [this]() {
            m_timedOut = true;
            if (m_reply && m_reply->isRunning()) m_reply->abort();
        });
        connect(m_reply, &QNetworkReply::finished, this, [this]() { handleDownloadFinished(); });
        m_timeout.start(300000);
    }

    void handleDownloadFinished()
    {
        QPointer<QNetworkReply> reply = m_reply;
        cleanupReply();
        if (m_output) {
            if (reply) m_output->write(reply->readAll());
            m_output->flush();
            m_output->close();
        }
        if (m_progress) {
            m_progress->setProgressIndeterminate(false);
            m_progress->setProgressValue(100);
            m_progress->setStatusText(QString::fromUtf8("Завантаження завершено. Перевірка SHA-256, розміру та підготовка handoff..."));
        }

        if (!reply) {
            finish();
            return;
        }

        if (reply->error() != QNetworkReply::NoError) {
            if (m_output) safeRemoveUpdateFile(m_downloadedFile, {}, {QStringLiteral(".part")});
            appendUpdateRuntimeLog(QStringLiteral("UPDATE_FAILED"),
                                   QStringLiteral("DOWNLOAD_INSTALLER failed: %1")
                                       .arg(buildNetworkErrorText(reply, m_timedOut)));
            const bool canceled = reply->error() == QNetworkReply::OperationCanceledError && !m_timedOut;
            if (!canceled) {
                showWarning(QString::fromUtf8("Оновлення"),
                            QString::fromUtf8("Не вдалося завантажити пакет оновлення.\n%1").arg(buildNetworkErrorText(reply, m_timedOut)));
            }
            finish();
            return;
        }

        const int downloadHttpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        appendUpdateRuntimeLog(QStringLiteral("DOWNLOAD_INSTALLER"),
                               QStringLiteral("download reply http=%1 asset=%2 url=%3")
                                   .arg(downloadHttpStatus)
                                   .arg(m_info.assetName, m_info.downloadUrl));
        if (downloadHttpStatus > 0 && (downloadHttpStatus < 200 || downloadHttpStatus >= 300)) {
            if (m_output) safeRemoveUpdateFile(m_downloadedFile, {}, {QStringLiteral(".part")});
            appendUpdateRuntimeLog(QStringLiteral("UPDATE_FAILED"),
                                   QStringLiteral("installer download HTTP status is not successful: %1 asset=%2")
                                       .arg(downloadHttpStatus)
                                       .arg(m_info.assetName));
            showWarning(QString::fromUtf8("Оновлення"),
                        QString::fromUtf8("Завантаження installer повернуло HTTP %1. Оновлення скасовано.").arg(downloadHttpStatus));
            finish();
            return;
        }

        const QFileInfo downloadedInfo(m_downloadedFile);
        const qint64 actualSize = downloadedInfo.exists() ? downloadedInfo.size() : -1;
        appendUpdateRuntimeLog(QStringLiteral("DOWNLOAD_INSTALLER"),
                               QStringLiteral("finished path=%1 actualSize=%2 expectedSize=%3")
                                   .arg(m_downloadedFile)
                                   .arg(actualSize)
                                   .arg(m_info.expectedDownloadSize));
        if (actualSize <= 0) {
            safeRemoveUpdateFile(m_downloadedFile, {}, {QStringLiteral(".part")});
            appendUpdateRuntimeLog(QStringLiteral("UPDATE_FAILED"),
                                   QStringLiteral("downloaded installer is empty or missing path=%1 actualSize=%2")
                                       .arg(m_downloadedFile)
                                       .arg(actualSize));
            showWarning(QString::fromUtf8("Оновлення"),
                        QString::fromUtf8("Завантажений пакет оновлення порожній або відсутній. Оновлення скасовано."));
            finish();
            return;
        }
        if (m_info.expectedDownloadSize > 0 && actualSize != m_info.expectedDownloadSize) {
            safeRemoveUpdateFile(m_downloadedFile, {}, {QStringLiteral(".part")});
            appendUpdateRuntimeLog(QStringLiteral("UPDATE_FAILED"),
                                   QStringLiteral("download size mismatch path=%1 expected=%2 actual=%3")
                                       .arg(m_downloadedFile)
                                       .arg(m_info.expectedDownloadSize)
                                       .arg(actualSize));
            showWarning(QString::fromUtf8("Оновлення"),
                        QString::fromUtf8("Розмір завантаженого пакета не збігається з GitHub Release asset.\nОчікувалося: %1 байт\nОтримано: %2 байт\nОновлення скасовано.")
                            .arg(m_info.expectedDownloadSize)
                            .arg(actualSize));
            finish();
            return;
        }

        QString hashError;
        const QString actualSha256 = computeFileSha256(m_downloadedFile, &hashError);
        qInfo().noquote() << QStringLiteral("UPDATE_DOWNLOAD_VERIFY asset=%1 expectedSha=%2 actualSha=%3 size=%4")
                             .arg(m_info.assetName, m_info.expectedSha256, actualSha256).arg(actualSize);
        appendUpdateRuntimeLog(QStringLiteral("VERIFY_SHA256"),
                               QStringLiteral("asset=%1 path=%2 expectedSha=%3 actualSha=%4 size=%5")
                                   .arg(m_info.assetName, m_downloadedFile, m_info.expectedSha256, actualSha256)
                                   .arg(actualSize));
        if (actualSha256.isEmpty()) {
            safeRemoveUpdateFile(m_downloadedFile, {}, {QStringLiteral(".part")});
            appendUpdateRuntimeLog(QStringLiteral("UPDATE_FAILED"),
                                   QStringLiteral("sha256_missing: calculation_failed path=%1 error=%2")
                                       .arg(m_downloadedFile, hashError));
            showWarning(QString::fromUtf8("Оновлення"),
                        QString::fromUtf8("Не вдалося перевірити SHA-256 завантаженого пакета.\n%1").arg(hashError));
            finish();
            return;
        }
        if (normalizeSha256Digest(m_info.expectedSha256) != actualSha256) {
            safeRemoveUpdateFile(m_downloadedFile, {}, {QStringLiteral(".part")});
            appendUpdateRuntimeLog(QStringLiteral("UPDATE_FAILED"),
                                   QStringLiteral("sha256_mismatch: path=%1 expected=%2 actual=%3")
                                       .arg(m_downloadedFile, m_info.expectedSha256, actualSha256));
            showWarning(QString::fromUtf8("Оновлення"),
                        QString::fromUtf8("SHA-256 завантаженого пакета не збігається з GitHub Release.\nОчікувалося: %1\nОтримано: %2\nОновлення скасовано.")
                            .arg(m_info.expectedSha256, actualSha256));
            finish();
            return;
        }

        safeRemoveUpdateFile(m_finalDownloadedFile);
        if (!QFile::rename(m_downloadedFile, m_finalDownloadedFile)) {
            appendUpdateRuntimeLog(QStringLiteral("UPDATE_FAILED"),
                                   QStringLiteral("rename part to final failed part=%1 final=%2")
                                       .arg(m_downloadedFile, m_finalDownloadedFile));
            showWarning(QString::fromUtf8("Оновлення"),
                        QString::fromUtf8("Не вдалося завершити підготовку перевіреного пакета оновлення.\n%1").arg(m_finalDownloadedFile));
            finish();
            return;
        }

        writeUpdateTransactionState(m_runId, QStringLiteral("downloaded"), m_info.currentVersion, m_info.latestVersion, m_info.assetName, m_info.expectedSha256, m_finalDownloadedFile);
        appendUpdateRuntimeLog(QStringLiteral("PREPARE_HANDOFF"),
                               QStringLiteral("verified package ready path=%1 expectedVersion=%2")
                                   .arg(m_finalDownloadedFile, m_info.latestVersion));
        QString err;
        if (!prepareAndLaunchUpdaterImpl(m_parent.data(), m_finalDownloadedFile, m_info.latestVersion, m_runId, &err)) {
            appendUpdateRuntimeLog(QStringLiteral("UPDATE_FAILED"),
                                   QStringLiteral("PREPARE_HANDOFF failed: %1").arg(err));
            showWarning(QString::fromUtf8("Оновлення"),
                        QString::fromUtf8("Не вдалося підготувати встановлення оновлення.\n%1").arg(err));
            finish();
            return;
        }

        m_handoffStarted = true;
        writeUpdateLockState(m_runId, QStringLiteral("handoff_started"), m_info.assetName, m_info.latestVersion, m_info.expectedSha256, QStringLiteral("updater"));
        writeUpdateTransactionState(m_runId, QStringLiteral("handoff_waiting_app_exit"), m_info.currentVersion, m_info.latestVersion, m_info.assetName, m_info.expectedSha256, m_finalDownloadedFile);
        finish();
    }

    QPointer<QWidget> m_parent;
    bool m_silentIfNoUpdates = false;
    bool m_timedOut = false;
    QNetworkAccessManager m_nam;
    QTimer m_timeout;
    QPointer<QNetworkReply> m_reply;
    QPointer<UpdateDialog> m_progress;
    bool m_closingDown = false;
    bool m_promptAfterChecksum = false;
    bool m_handoffStarted = false;
    RuntimeJob::Ticket m_runtimeTicket;
    bool m_runtimeFinished = false;
    QString m_runId;
    QString m_runDir;
    std::unique_ptr<QFile> m_output;
    UpdateService::ReleaseInfo m_info;
    QString m_downloadedFile;
    QString m_finalDownloadedFile;
};

bool prepareAndLaunchUpdaterImpl(QWidget* parent, const QString& downloadedFile, const QString& expectedVersion, QString* errorOut)
{
    return prepareAndLaunchUpdaterImpl(parent, downloadedFile, expectedVersion, makeUpdateRunId(), errorOut);
}

bool prepareAndLaunchUpdaterImpl(QWidget* parent, const QString& downloadedFile, const QString& expectedVersion, const QString& runId, QString* errorOut)
{
    Q_UNUSED(parent);
    if (CarService::AppVersion::updateSignerThumbprint().size() != 40) {
        if (errorOut) *errorOut = QStringLiteral("Автооновлення заблоковано: у збірці не налаштовано довірений сертифікат видавця.");
        return false;
    }
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString exePath = QCoreApplication::applicationFilePath();
    const QString appLogPath = App::logFilePath();
    QString workDir = QFileInfo(downloadedFile).absolutePath();
    if (workDir.trimmed().isEmpty() || !QDir(workDir).exists()) workDir = updateRunDirPath(runId);
    const bool installedMode = prefersInstallerPackage();
    const bool appDirWritable = isWritableDirectory(appDir);
    const QString activeDbPath = App::activeDbFilePath();
    const QString legacyDbPath = App::legacyDbFilePath();
    const QString archiveDbPath = App::archiveDbFilePath();
    const QString registryDbPath = App::registryDbFilePath();
    const QString sealedDbPath = App::sealedActiveDbFilePath();
    const QString sealedLegacyDbPath = App::sealedLegacyDbFilePath();
    const QString sealedArchiveDbPath = App::sealedArchiveDbFilePath();
    const QString sealedRegistryDbPath = App::sealedRegistryDbFilePath();
    const qint64 appPid = QCoreApplication::applicationPid();
    const QString psPath = QDir(workDir).filePath(QStringLiteral("apply_update.ps1"));
    const QString logPath = QDir(workDir).filePath(QStringLiteral("apply_update.log"));
    const QString statusPath = QDir(workDir).filePath(QStringLiteral("update_status.txt"));
    const QString statusJsonPath = QDir(workDir).filePath(QStringLiteral("update_status.json"));
    const QString latestStatusPath = updateLatestStatusPath();
    const QString latestStatusJsonPath = QDir(updateRuntimeDirPath()).filePath(QStringLiteral("update_status.json"));
    const QString latestLogPath = updateRuntimeLogPath();
    const QString backupDir = QDir(workDir).filePath(QStringLiteral("backup_prev"));
    const QString innoLogDir = QDir(workDir).filePath(QStringLiteral("logs"));
    const QString innoLogPath = QDir(innoLogDir).filePath(QStringLiteral("inno_setup.log"));
    const QString appliedMarkerPath = appliedUpdateMarkerPath();
    const QString updateStateFilePath = updateStatePath();
    const QString transactionPath = updateTransactionPath();
    const QString failedPackagesPath = failedUpdatePackagesPath();
    const QString lockStatePath = updateLockStatePath();
    const QString updateFileLockStatePath = updateFileLockPath();
    const QString userDesktopDir = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    const QString appDataDir = qEnvironmentVariable("APPDATA");
    const QString userProgramsDir = appDataDir.trimmed().isEmpty()
        ? QString()
        : QDir(appDataDir).filePath(QStringLiteral("Microsoft/Windows/Start Menu/Programs"));
    QDir().mkpath(workDir);
    QDir().mkpath(innoLogDir);

    const QString installerDirArg = QStringLiteral("/DIR=\"%1\"").arg(QDir::toNativeSeparators(appDir));
    const QString installerLogArg = QStringLiteral("/LOG=\"%1\"").arg(QDir::toNativeSeparators(innoLogPath));

    QStringList nonAbsoluteCriticalPaths;
    auto requireAbsolutePath = [&nonAbsoluteCriticalPaths](const QString& label, const QString& path) {
        if (path.trimmed().isEmpty() || !QFileInfo(path).isAbsolute()) {
            nonAbsoluteCriticalPaths << QStringLiteral("%1=%2").arg(label, path.isEmpty() ? QStringLiteral("<empty>") : path);
        }
    };
    requireAbsolutePath(QStringLiteral("package"), downloadedFile);
    requireAbsolutePath(QStringLiteral("exe"), exePath);
    requireAbsolutePath(QStringLiteral("appDir"), appDir);
    requireAbsolutePath(QStringLiteral("workDir"), workDir);
    requireAbsolutePath(QStringLiteral("applyScript"), psPath);
    requireAbsolutePath(QStringLiteral("applyLog"), logPath);
    requireAbsolutePath(QStringLiteral("latestApplyLog"), latestLogPath);
    requireAbsolutePath(QStringLiteral("latestStatus"), latestStatusPath);
    requireAbsolutePath(QStringLiteral("statusJson"), statusJsonPath);
    requireAbsolutePath(QStringLiteral("latestStatusJson"), latestStatusJsonPath);
    requireAbsolutePath(QStringLiteral("innoLog"), innoLogPath);
    requireAbsolutePath(QStringLiteral("appliedMarker"), appliedMarkerPath);
    requireAbsolutePath(QStringLiteral("updateState"), updateStateFilePath);
    requireAbsolutePath(QStringLiteral("transactionState"), transactionPath);
    requireAbsolutePath(QStringLiteral("failedPackages"), failedPackagesPath);
    requireAbsolutePath(QStringLiteral("lockState"), lockStatePath);
    requireAbsolutePath(QStringLiteral("updateFileLock"), updateFileLockStatePath);
    if (!nonAbsoluteCriticalPaths.isEmpty()) {
        const QString detail = nonAbsoluteCriticalPaths.join(QStringLiteral("; "));
        appendUpdateRuntimeLog(QStringLiteral("UPDATE_FAILED"),
                               QStringLiteral("PREPARE_HANDOFF blocked because critical path is not absolute: %1").arg(detail));
        if (errorOut) *errorOut = QString::fromUtf8("Handoff має не абсолютний критичний шлях: %1").arg(detail);
        return false;
    }

    const QString lower = downloadedFile.toLower();
    if (installedMode && !lower.endsWith(QStringLiteral(".exe"))) {
        if (errorOut) {
            *errorOut = QString::fromUtf8("Для встановленої версії потрібен installer .exe, але в GitHub Release його не знайдено.");
        }
        qWarning().noquote() << QStringLiteral("UPDATE_HANDOFF blocked non-installer package for installed mode: %1").arg(downloadedFile);
        return false;
    }
    if (!lower.endsWith(QStringLiteral(".exe")) && (installedMode || !appDirWritable)) {
        if (errorOut) {
            *errorOut = QString::fromUtf8("ZIP-оновлення дозволене лише для portable-версії з правом запису в каталог застосунку. Для встановленої версії потрібен інсталяційний .exe.");
        }
        return false;
    }

    QString hashError;
    const QString packageSha256 = computeFileSha256(downloadedFile, &hashError);
    if (packageSha256.isEmpty()) {
        appendUpdateRuntimeLog(QStringLiteral("UPDATE_FAILED"),
                               QStringLiteral("PREPARE_HANDOFF blocked because package SHA256 cannot be calculated path=%1 error=%2")
                                   .arg(downloadedFile, hashError));
        if (errorOut) *errorOut = QString::fromUtf8("Не вдалося обчислити SHA256 installer/payload перед запуском оновлювача: %1").arg(hashError);
        return false;
    }
    QString exeHashError;
    const QString currentExeSha256 = computeFileSha256(exePath, &exeHashError);
    qInfo().noquote() << QStringLiteral("UPDATE_PRECHECK package=%1 sha256=%2 expectedVersion=%3 appDir=%4 exe=%5 installedMode=%6 appDirWritable=%7 ps=%8 log=%9 pid=%10 currentExeSha256=%11")
                         .arg(downloadedFile,
                              packageSha256.isEmpty() ? QStringLiteral("<sha256_error>") : packageSha256,
                              expectedVersion,
                              appDir,
                              exePath,
                              installedMode ? QStringLiteral("true") : QStringLiteral("false"),
                              appDirWritable ? QStringLiteral("true") : QStringLiteral("false"),
                              psPath,
                              logPath)
                         .arg(appPid)
                         .arg(currentExeSha256.isEmpty() ? QStringLiteral("<exe_sha256_error>") : currentExeSha256);
    qInfo().noquote() << QStringLiteral("UPDATE_INSTALLER_PREP package=%1 appDir=%2 installerDirArg=%3 innoLog=%4 installerLogArg=%5 runtimeWorkDir=%6")
                         .arg(downloadedFile,
                              appDir,
                              installerDirArg,
                              innoLogPath,
                              installerLogArg,
                              workDir);
    appendUpdateRuntimeLog(QStringLiteral("PREPARE_HANDOFF"),
                           QStringLiteral("installerPackage=%1 appDir=%2 exe=%3 workDir=%4 ps=%5 status=%6 dirArg=%7 innoLog=%8 logArg=%9 expectedVersion=%10")
                               .arg(downloadedFile,
                                    appDir,
                                    exePath,
                                    workDir,
                                    psPath,
                                    statusPath,
                                    installerDirArg,
                                    innoLogPath,
                                    installerLogArg)
                               .arg(expectedVersion));

    QFile ps(psPath);
    if (!ps.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        if (errorOut) *errorOut = QString::fromUtf8("Не вдалося створити apply_update.ps1");
        return false;
    }

    QString script;
    auto add = [&script](const QString& line = QString()) {
        script += line;
        script += QLatin1Char('\n');
    };

    add(QStringLiteral("param([switch]$ElevatedChild)"));
    add(QStringLiteral("$ErrorActionPreference = 'Stop'"));
    add(QStringLiteral("[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)"));
    add(QStringLiteral("$LogPath = %1").arg(psSingleQuoted(QDir::toNativeSeparators(logPath))));
    add(QStringLiteral("$LatestLogPath = %1").arg(psSingleQuoted(QDir::toNativeSeparators(latestLogPath))));
    add(QStringLiteral("$StatusPath = %1").arg(psSingleQuoted(QDir::toNativeSeparators(statusPath))));
    add(QStringLiteral("$StatusJsonPath = %1").arg(psSingleQuoted(QDir::toNativeSeparators(statusJsonPath))));
    add(QStringLiteral("$LatestStatusPath = %1").arg(psSingleQuoted(QDir::toNativeSeparators(latestStatusPath))));
    add(QStringLiteral("$LatestStatusJsonPath = %1").arg(psSingleQuoted(QDir::toNativeSeparators(latestStatusJsonPath))));
    add(QStringLiteral("$PackagePath = %1").arg(psSingleQuoted(QDir::toNativeSeparators(downloadedFile))));
    add(QStringLiteral("$ExePath = %1").arg(psSingleQuoted(QDir::toNativeSeparators(exePath))));
    add(QStringLiteral("$AppLogPath = %1").arg(psSingleQuoted(QDir::toNativeSeparators(appLogPath))));
    add(QStringLiteral("$AppDir = %1").arg(psSingleQuoted(QDir::toNativeSeparators(appDir))));
    add(QStringLiteral("$ActiveDbPath = %1").arg(psSingleQuoted(QDir::toNativeSeparators(activeDbPath))));
    add(QStringLiteral("$LegacyDbPath = %1").arg(psSingleQuoted(QDir::toNativeSeparators(legacyDbPath))));
    add(QStringLiteral("$ArchiveDbPath = %1").arg(psSingleQuoted(QDir::toNativeSeparators(archiveDbPath))));
    add(QStringLiteral("$RegistryDbPath = %1").arg(psSingleQuoted(QDir::toNativeSeparators(registryDbPath))));
    add(QStringLiteral("$SealedDbPath = %1").arg(psSingleQuoted(QDir::toNativeSeparators(sealedDbPath))));
    add(QStringLiteral("$SealedLegacyDbPath = %1").arg(psSingleQuoted(QDir::toNativeSeparators(sealedLegacyDbPath))));
    add(QStringLiteral("$SealedArchiveDbPath = %1").arg(psSingleQuoted(QDir::toNativeSeparators(sealedArchiveDbPath))));
    add(QStringLiteral("$SealedRegistryDbPath = %1").arg(psSingleQuoted(QDir::toNativeSeparators(sealedRegistryDbPath))));
    add(QStringLiteral("$BackupDir = %1").arg(psSingleQuoted(QDir::toNativeSeparators(backupDir))));
    add(QStringLiteral("$InnoLogDir = %1").arg(psSingleQuoted(QDir::toNativeSeparators(innoLogDir))));
    add(QStringLiteral("$InnoLogPath = %1").arg(psSingleQuoted(QDir::toNativeSeparators(innoLogPath))));
    add(QStringLiteral("$AppliedMarkerPath = %1").arg(psSingleQuoted(QDir::toNativeSeparators(appliedMarkerPath))));
    add(QStringLiteral("$UpdateStatePath = %1").arg(psSingleQuoted(QDir::toNativeSeparators(updateStateFilePath))));
    add(QStringLiteral("$TransactionPath = %1").arg(psSingleQuoted(QDir::toNativeSeparators(transactionPath))));
    add(QStringLiteral("$FailedPackagesPath = %1").arg(psSingleQuoted(QDir::toNativeSeparators(failedPackagesPath))));
    add(QStringLiteral("$LockStatePath = %1").arg(psSingleQuoted(QDir::toNativeSeparators(lockStatePath))));
    add(QStringLiteral("$UpdateFileLockPath = %1").arg(psSingleQuoted(QDir::toNativeSeparators(updateFileLockStatePath))));
    add(QStringLiteral("$UserDesktopDir = %1").arg(psSingleQuoted(QDir::toNativeSeparators(userDesktopDir))));
    add(QStringLiteral("$UserProgramsDir = %1").arg(psSingleQuoted(QDir::toNativeSeparators(userProgramsDir))));
    add(QStringLiteral("$InstallDirArg = %1").arg(psSingleQuoted(installerDirArg)));
    add(QStringLiteral("$InnoLogArg = %1").arg(psSingleQuoted(installerLogArg)));
    add(QStringLiteral("$InstallerWorkingDir = $AppDir"));
    add(QStringLiteral("$AppProcessId = %1").arg(appPid));
    add(QStringLiteral("$RunId = %1").arg(psSingleQuoted(runId)));
    add(QStringLiteral("$ExpectedVersion = %1").arg(psSingleQuoted(expectedVersion)));
    add(QStringLiteral("$PreviousVersion = %1").arg(psSingleQuoted(UpdateService::currentVersion())));
    add(QStringLiteral("$PackageSha256 = %1").arg(psSingleQuoted(packageSha256)));
    add(QStringLiteral("$PreviousExeSha256 = %1").arg(psSingleQuoted(currentExeSha256)));
    add(QStringLiteral("$InstallerStartMaxAttempts = 2"));
    add(QStringLiteral("$PackageExtension = [System.IO.Path]::GetExtension($PackagePath).ToLowerInvariant()"));
    add(QStringLiteral("$InstallerArgs = @('/SP-','/VERYSILENT','/SUPPRESSMSGBOXES','/NOCANCEL','/NORESTART','/CLOSEAPPLICATIONS','/FORCECLOSEAPPLICATIONS','/TASKS=',$InstallDirArg,$InnoLogArg)"));
    add(QStringLiteral("$InstallerArgsLine = $InstallerArgs -join ' '"));
    add(QStringLiteral("$script:InstallerPid = 0"));
    add(QStringLiteral("$script:TransactionStartedAtUtc = (Get-Date).ToUniversalTime().ToString('o')"));
    add(QStringLiteral("$script:UpdateOnceMutex = $null"));
    add(QStringLiteral("$ExpectedPublisher = 'CarService PointG'"));
