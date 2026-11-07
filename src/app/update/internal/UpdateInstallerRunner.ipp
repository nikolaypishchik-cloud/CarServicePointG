// Extracted from src/app/update/UpdateServiceSupport.cpp for P1/P2 monolith reduction.
// Included exactly once by the original translation unit/header; do not compile separately.

                               .arg(rawTag,
                                    out.latestVersion,
                                    releaseId,
                                    out.htmlUrl,
                                    assetsUrl,
                                    out.draft ? QStringLiteral("true") : QStringLiteral("false"),
                                    out.prerelease ? QStringLiteral("true") : QStringLiteral("false"),
                                    publishedAt,
                                    createdAt));

    if (out.latestVersion.isEmpty()) {
        if (errorOut) *errorOut = QStringLiteral("Сервер не повернув номер нової версії");
        appendUpdateRuntimeLog(QStringLiteral("UPDATE_FAILED"), QStringLiteral("manifest_download_failed: missing_tag_name"));
        return out;
    }
    if (out.prerelease || out.draft) {
        if (errorOut) *errorOut = QStringLiteral("GitHub повернув неготовий реліз (draft/prerelease). Для автооновлення потрібен published release.");
        appendUpdateRuntimeLog(QStringLiteral("UPDATE_FAILED"),
                               QStringLiteral("manifest_download_failed: release_not_published draft=%1 prerelease=%2")
                                   .arg(out.draft ? QStringLiteral("true") : QStringLiteral("false"),
                                        out.prerelease ? QStringLiteral("true") : QStringLiteral("false")));
        return out;
    }

    const QJsonArray assets = obj.value(QStringLiteral("assets")).toArray();
    QHash<QString, QString> checksumUrlByName;
    QStringList checksumNames;
    CandidateAsset installerCandidate;
    CandidateAsset portableCandidate;

    for (const QJsonValue& v : assets) {
        const QJsonObject a = v.toObject();
        const QString name = a.value(QStringLiteral("name")).toString();
        const QString url = a.value(QStringLiteral("browser_download_url")).toString();
        const QString state = a.value(QStringLiteral("state")).toString().trimmed().toLower();
        if (name.trimmed().isEmpty() || url.trimmed().isEmpty()) continue;
        if (!state.isEmpty() && state != QStringLiteral("uploaded")) continue;

        if (!downloadUrlMatchesAssetName(name, url)) {
            qWarning().noquote() << QStringLiteral("UPDATE_CHECK ignoring asset because browser_download_url filename does not match asset name: name=%1 url=%2")
                                    .arg(name, url);
            continue;
        }

        qint64 size = -1;
        if (a.contains(QStringLiteral("size"))) {
            size = a.value(QStringLiteral("size")).toVariant().toLongLong();
        }
        const QString low = name.toLower();
        QDateTime assetUpdatedAt = QDateTime::fromString(a.value(QStringLiteral("updated_at")).toString().trimmed(), Qt::ISODate);
        if (assetUpdatedAt.isValid()) assetUpdatedAt = assetUpdatedAt.toUTC();
        const QString sha256 = digestFromAssetObject(a);
        qInfo().noquote() << QStringLiteral("UPDATE_CHECK asset name=%1 size=%2 digest=%3")
                             .arg(name).arg(size).arg(sha256.isEmpty() ? QStringLiteral("<none>") : sha256);

        if (looksLikeChecksumAsset(low)) {
            checksumNames.push_back(name);
            checksumUrlByName.insert(name, url);
            continue;
        }

        const bool versionMatch = assetNameMatchesVersion(low, out.latestVersion);

        if (low.endsWith(QStringLiteral(".zip"))) {
            if (!versionMatch) {
                qWarning().noquote() << QStringLiteral("UPDATE_CHECK ignoring zip asset without latest-version marker: name=%1 latest=%2")
                                        .arg(name, out.latestVersion);
                continue;
            }
            int score = 0;
            if (versionMatch) score += 40;
            if (low.contains(QStringLiteral("portable"))) score += 10;
            if (low.contains(QStringLiteral("setup")) || low.contains(QStringLiteral("installer"))) score -= 10;
            if (!sha256.isEmpty()) score += 5;
            if (size > 0) score += 1;
            considerCandidate(portableCandidate, name, url, sha256, size, assetUpdatedAt, score);
            continue;
        }

        if (low.endsWith(QStringLiteral(".exe")) && looksLikeInstallerAsset(low) && !looksLikeBlockedExecutableAsset(low)) {
            if (!versionMatch) {
                qWarning().noquote() << QStringLiteral("UPDATE_CHECK ignoring installer asset without latest-version marker: name=%1 latest=%2")
                                        .arg(name, out.latestVersion);
                continue;
            }
            const bool canonicalInstaller = isCanonicalInstallerAssetName(low, out.latestVersion);
            if (prefersInstallerPackage() && !canonicalInstaller) {
                qWarning().noquote() << QStringLiteral("UPDATE_CHECK ignoring non-canonical installer asset for installed mode: name=%1 expected=CarServicePointG_Setup_x86_x64_v%2.exe")
                                        .arg(name, normalizeVersion(out.latestVersion));
                continue;
            }
            int score = 0;
            score += 20;
            if (versionMatch) score += 40;
            if (canonicalInstaller) score += 100;
            if (low.contains(QStringLiteral("setup")) || low.contains(QStringLiteral("install")) || low.contains(QStringLiteral("installer"))) score += 30;
            if (low.startsWith(QStringLiteral("carservicepointg_")) || low.startsWith(QStringLiteral("carservicepointg-"))) score += 15;
            if (!sha256.isEmpty()) score += 5;
            if (size > 0) score += 1;
            considerCandidate(installerCandidate, name, url, sha256, size, assetUpdatedAt, score);
        }
    }

    out.installerAssetName = installerCandidate.name;
    out.installerDownloadUrl = installerCandidate.url;
    out.portableAssetName = portableCandidate.name;
    out.portableDownloadUrl = portableCandidate.url;

    const bool preferInstaller = prefersInstallerPackage();
    CandidateAsset selected;
    // Professional update contract: use a verified installer asset whenever it exists.
    // Portable ZIP is only a reserve path for writable portable builds; installed builds must never
    // apply a random zip over an installed application directory.
    if (!installerCandidate.url.isEmpty()) {
        selected = installerCandidate;
    } else if (!preferInstaller && !portableCandidate.url.isEmpty()) {
        selected = portableCandidate;
    } else if (preferInstaller) {
        if (errorOut) {
            *errorOut = QString::fromUtf8("Для встановленої версії потрібен installer .exe, але в GitHub Release його не знайдено.");
        }
        qWarning().noquote() << QStringLiteral("UPDATE_CHECK no installer asset for installed mode; portable=%1")
                                .arg(portableCandidate.name);
        appendUpdateRuntimeLog(QStringLiteral("UPDATE_FAILED"), QStringLiteral("no_valid_installer_asset"));
        return out;
    }

    out.assetName = selected.name;
    out.downloadUrl = selected.url;
    out.assetDigestSha256 = selected.sha256;
    out.expectedDownloadSize = selected.size;
    out.assetUpdatedAt = selected.updatedAt.isValid() ? selected.updatedAt.toString(Qt::ISODate) : QString();
    out.expectedSha256 = selected.sha256;

    const QString checksumAssetName = chooseChecksumAssetName(out.assetName, checksumNames);
    if (!checksumAssetName.isEmpty()) {
        out.checksumAssetName = checksumAssetName;
        out.checksumDownloadUrl = checksumUrlByName.value(checksumAssetName);
        if (out.checksumDownloadUrl.trimmed().isEmpty()) {
            if (errorOut) *errorOut = QStringLiteral("Для вибраного SHA256 asset не знайдено browser_download_url. Оновлення скасовано.");
            appendUpdateRuntimeLog(QStringLiteral("UPDATE_FAILED"),
                                   QStringLiteral("sha256_missing: checksum asset selected but download URL is empty asset=%1").arg(out.checksumAssetName));
            return out;
        }
    }

    qInfo().noquote() << QStringLiteral("UPDATE_CHECK selected asset=%1 checksumAsset=%2 expectedSize=%3 expectedSha=%4")
                         .arg(out.assetName,
                              out.checksumAssetName.isEmpty() ? QStringLiteral("<none>") : out.checksumAssetName)
                         .arg(out.expectedDownloadSize)
                         .arg(out.expectedSha256.isEmpty() ? QStringLiteral("<from checksum>") : out.expectedSha256);
    appendUpdateRuntimeLog(QStringLiteral("SELECT_ASSET"),
                           QStringLiteral("selected=%1 installer=%2 portable=%3 blockedFilters=vc_redist/unins/maintenancetool/helper/debug")
                               .arg(out.assetName,
                                    out.installerAssetName.isEmpty() ? QStringLiteral("<none>") : out.installerAssetName,
                                    out.portableAssetName.isEmpty() ? QStringLiteral("<none>") : out.portableAssetName));
    appendUpdateRuntimeLog(QStringLiteral("DOWNLOAD_MANIFEST"),
                           QStringLiteral("asset audit selected=%1 installer=%2 portable=%3 checksum=%4 downloadUrl=%5 checksumUrl=%6 expectedSize=%7 expectedSha=%8")
                               .arg(out.assetName,
                                    out.installerAssetName.isEmpty() ? QStringLiteral("<none>") : out.installerAssetName,
                                    out.portableAssetName.isEmpty() ? QStringLiteral("<none>") : out.portableAssetName,
                                    out.checksumAssetName.isEmpty() ? QStringLiteral("<none>") : out.checksumAssetName,
                                    out.downloadUrl,
                                    out.checksumDownloadUrl.isEmpty() ? QStringLiteral("<none>") : out.checksumDownloadUrl)
                               .arg(out.expectedDownloadSize)
                               .arg(out.expectedSha256.isEmpty() ? QStringLiteral("<from checksum asset>") : out.expectedSha256));

    if (out.downloadUrl.isEmpty()) {
        if (errorOut) *errorOut = QStringLiteral("У GitHub Release не знайдено валідний installer/portable asset оновлення");
        appendUpdateRuntimeLog(QStringLiteral("UPDATE_FAILED"), QStringLiteral("no_valid_installer_asset"));
        return out;
    }
    if (out.expectedDownloadSize <= 0) {
        if (errorOut) *errorOut = QStringLiteral("GitHub Release asset не містить коректного розміру файлу. Оновлення скасовано.");
        appendUpdateRuntimeLog(QStringLiteral("UPDATE_FAILED"),
                               QStringLiteral("manifest_download_failed: asset_size_missing asset=%1").arg(out.assetName));
        return out;
    }
    if (out.expectedSha256.isEmpty() && out.checksumDownloadUrl.isEmpty()) {
        if (errorOut) *errorOut = QStringLiteral("У релізі не знайдено digest/SHA256 для вибраного installer/payload. Оновлення скасовано.");
        appendUpdateRuntimeLog(QStringLiteral("UPDATE_FAILED"),
                               QStringLiteral("sha256_missing: no digest/checksum asset for %1").arg(out.assetName));
        return out;
    }

    const int versionCompare = compareVersions(out.currentVersion, out.latestVersion);
    if (versionCompare == 0 && !out.expectedSha256.isEmpty() && isAppliedUpdatePackage(out.latestVersion, out.expectedSha256)) {
        appendUpdateRuntimeLog(QStringLiteral("DOWNLOAD_MANIFEST"),
                               QStringLiteral("UPDATE_SKIP_ALREADY_APPLIED_PACKAGE same-version package already applied; marker=%1 latest=%2 packageSha=%3")
                                   .arg(appliedUpdateMarkerPath(), out.latestVersion, out.expectedSha256));
        out.sameVersionHotfix = false;
        out.ok = false;
        return out;
    }

    if (versionCompare == 0) {
        appendUpdateRuntimeLog(QStringLiteral("DOWNLOAD_MANIFEST"),
                               QStringLiteral("UPDATE_SKIP_SAME_VERSION: current=%1 latest=%2 asset=%3 sha=%4. Same-version update by GitHub asset timestamp is disabled to prevent update loops.")
                                   .arg(out.currentVersion, out.latestVersion, out.assetName,
                                        out.expectedSha256.isEmpty() ? QStringLiteral("<from checksum asset>") : out.expectedSha256));
        out.sameVersionHotfix = false;
        out.ok = false;
        return out;
    }
    if (versionCompare > 0) {
        appendUpdateRuntimeLog(QStringLiteral("DOWNLOAD_MANIFEST"),
                               QStringLiteral("UPDATE_SKIP_OLDER_VERSION: current=%1 latest=%2 asset=%3")
                                   .arg(out.currentVersion, out.latestVersion, out.assetName));
        out.sameVersionHotfix = false;
        out.ok = false;
        return out;
    }

    out.sameVersionHotfix = false;
    out.ok = true;
    return out;
}


class UpdateFlow final : public QObject
{
public:
    UpdateFlow(QWidget* parent, bool silentIfNoUpdates, const QString& runId)
        : QObject(qApp)
        , m_parent(parent)
        , m_silentIfNoUpdates(silentIfNoUpdates)
        , m_runId(runId)
        , m_runDir(updateRunDirPath(runId))
    {
        m_runtimeTicket = RuntimeJob::RuntimeJobDispatcher::instance().startLatest(
            RuntimeJob::Family::Update,
            QStringLiteral("UpdateFlow:%1").arg(m_runId),
            180000,
            45);
        m_timeout.setSingleShot(true);
        if (auto* app = QCoreApplication::instance()) {
            connect(app, &QCoreApplication::aboutToQuit, this, [this]() {
                m_closingDown = true;
                RuntimeJob::RuntimeJobDispatcher::instance().cancelFamily(RuntimeJob::Family::Update);
                cleanupReply();
                if (m_progress) {
                    m_progress->hide();
                    m_progress->deleteLater();
                    m_progress = nullptr;
                }
            });
        }
    }

    void start()
    {
        startCheck();
    }

private:
    void finish()
    {
        if (m_runtimeTicket.isValid() && !m_runtimeFinished) {
            RuntimeJob::RuntimeJobDispatcher::instance().finish(
                m_runtimeTicket,
                RuntimeJob::RuntimeJobDispatcher::instance().shouldApply(m_runtimeTicket)
                    ? RuntimeJob::Status::Succeeded
                    : RuntimeJob::Status::Cancelled);
            m_runtimeFinished = true;
        }
        cleanupReply();
        if (m_progress) {
            m_progress->hide();
            m_progress->deleteLater();
            m_progress = nullptr;
        }
        if (m_output) {
            if (m_output->isOpen()) m_output->close();
            m_output.reset();
        }
        if (!m_handoffStarted) clearUpdateLockState(m_runId);
        s_updateFlowActive.storeRelease(0);
        deleteLater();
    }

    void cleanupReply()
    {
        if (!m_reply) return;
        QObject::disconnect(m_reply, nullptr, this, nullptr);
        m_reply->deleteLater();
        m_reply = nullptr;
        m_timeout.stop();
    }

    void showWarning(const QString& title, const QString& text)
    {
        if (m_closingDown) return;
        UpdateDialog dlg(m_parent.data());
        dlg.setInfoMode(title, text, QString::fromUtf8("Закрити"), false);
        dlg.exec();
    }

    void startCheck()
    {
        if (!RuntimeJob::RuntimeJobDispatcher::instance().shouldApply(m_runtimeTicket)) {
            appendUpdateRuntimeLog(QStringLiteral("CHECK_UPDATE"), QStringLiteral("update skipped: runtime dispatcher rejected update job"));
            finish();
            return;
        }
        RuntimeJob::RuntimeJobDispatcher::instance().markRunning(m_runtimeTicket);
        m_timedOut = false;
        clearUpdateRuntimeLog();
        writeUpdateLockState(m_runId, QStringLiteral("check_update"));
        writeUpdateTransactionState(m_runId, QStringLiteral("checking"), UpdateService::currentVersion());
        appendUpdateRuntimeLog(QStringLiteral("CHECK_UPDATE"),
                               QStringLiteral("run_id=%1 DOWNLOAD_MANIFEST api=%2 current=%3")
                                   .arg(m_runId, UpdateService::latestReleaseApiUrl(), UpdateService::currentVersion()));
        QNetworkRequest req{QUrl(UpdateService::latestReleaseApiUrl())};
        configureGithubApiRequest(req);

        m_reply = m_nam.get(req);
        connect(&m_timeout, &QTimer::timeout, m_reply, [this]() {
            m_timedOut = true;
            if (m_reply && m_reply->isRunning()) m_reply->abort();
        });
        connect(m_reply, &QNetworkReply::finished, this, [this]() { handleCheckFinished(); });
        m_timeout.start(15000);
    }

    bool askToDownload()
    {
        if (m_closingDown) return false;
        UpdateDialog dlg(m_parent.data());
        dlg.setPromptMode(m_info.latestVersion,
                          m_info.currentVersion,
                          m_info.notes,
                          QString::fromUtf8("Завантажити"),
                          QString::fromUtf8("Пізніше"),
                          QString::fromUtf8("Деталі"));
        return dlg.exec() == QDialog::Accepted;
    }

    void handleCheckFinished()
    {
        QPointer<QNetworkReply> reply = m_reply;
        cleanupReply();
        if (!reply) {
            finish();
            return;
        }

        if (reply->error() != QNetworkReply::NoError) {
            appendUpdateRuntimeLog(QStringLiteral("UPDATE_FAILED"),
                                   QStringLiteral("manifest_download_failed: %1")
                                       .arg(buildNetworkErrorText(reply, m_timedOut)));
            if (!m_silentIfNoUpdates) {
                showWarning(QString::fromUtf8("Оновлення"),
                            QString::fromUtf8("Не вдалося перевірити оновлення.\n%1").arg(buildNetworkErrorText(reply, m_timedOut)));
            }
            finish();
            return;
        }

        const int manifestHttpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray manifestBytes = reply->readAll();
        appendUpdateRuntimeLog(QStringLiteral("DOWNLOAD_MANIFEST"),
                               QStringLiteral("manifest downloaded bytes=%1 http=%2 url=%3")
                                   .arg(manifestBytes.size())
                                   .arg(manifestHttpStatus)
                                   .arg(UpdateService::latestReleaseApiUrl()));
        if (manifestHttpStatus > 0 && (manifestHttpStatus < 200 || manifestHttpStatus >= 300)) {
            appendUpdateRuntimeLog(QStringLiteral("UPDATE_FAILED"),
                                   QStringLiteral("manifest_download_failed: http_status=%1").arg(manifestHttpStatus));
            if (!m_silentIfNoUpdates) {
                showWarning(QString::fromUtf8("Оновлення"),
                            QString::fromUtf8("GitHub Release manifest повернув HTTP %1. Оновлення скасовано.").arg(manifestHttpStatus));
            }
            finish();
            return;
        }
        QString parseError;
        m_info = parseReleaseInfo(manifestBytes, &parseError);
        if (!parseError.isEmpty()) {
            appendUpdateRuntimeLog(QStringLiteral("UPDATE_FAILED"),
                                   QStringLiteral("manifest_download_failed: validation=%1").arg(parseError));
            if (!m_silentIfNoUpdates) {
                showWarning(QString::fromUtf8("Оновлення"),
                            QString::fromUtf8("Не вдалося перевірити оновлення.\n%1").arg(parseError));
            }
            finish();
            return;
        }

        appendUpdateRuntimeLog(QStringLiteral("DOWNLOAD_MANIFEST"),
                               QStringLiteral("selected asset=%1 url=%2 checksumAsset=%3 expectedSize=%4 expectedSha=%5 latest=%6")
                                   .arg(m_info.assetName,
                                        m_info.downloadUrl,
                                        m_info.checksumAssetName.isEmpty() ? QStringLiteral("<none>") : m_info.checksumAssetName)
                                   .arg(m_info.expectedDownloadSize)
                                   .arg(m_info.expectedSha256.isEmpty() ? QStringLiteral("<from checksum asset>") : m_info.expectedSha256,
                                        m_info.latestVersion));
        if (!m_info.ok) {
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

        if (m_closingDown) {
            finish();
            return;
        }

        QString failedCooldownReason;
        if (!m_info.expectedSha256.isEmpty() && isFailedPackageBlocked(m_info.expectedSha256, m_info.latestVersion, &failedCooldownReason)) {
            appendUpdateRuntimeLog(QStringLiteral("CHECK_UPDATE"), QStringLiteral("UPDATE_SKIP_FAILED_PACKAGE_COOLDOWN: %1").arg(failedCooldownReason));
            if (m_silentIfNoUpdates) {
                finish();
                return;
            }
            appendUpdateRuntimeLog(QStringLiteral("CHECK_UPDATE"), QStringLiteral("Manual update retry override after failed-package cooldown warning: %1").arg(failedCooldownReason));
        }

        if (m_info.expectedSha256.isEmpty()) {
            m_promptAfterChecksum = true;
            startChecksumDownload();
            return;
        }

        if (!askToDownload()) {
            finish();
            return;
        }

        startDownload();
    }

    void startChecksumDownload()
    {
        if (m_info.checksumDownloadUrl.trimmed().isEmpty()) {
            appendUpdateRuntimeLog(QStringLiteral("UPDATE_FAILED"),
                                   QStringLiteral("sha256_missing: checksum asset missing for %1").arg(m_info.assetName));
            showWarning(QString::fromUtf8("Оновлення"),
                        QString::fromUtf8("Для пакета %1 не знайдено digest/SHA256. Оновлення скасовано.").arg(m_info.assetName));
            finish();
            return;
        }

        m_timedOut = false;
        appendUpdateRuntimeLog(QStringLiteral("VERIFY_SHA256"),
                               QStringLiteral("downloading checksum asset=%1 url=%2")
                                   .arg(m_info.checksumAssetName, m_info.checksumDownloadUrl));
        QNetworkRequest req{QUrl(m_info.checksumDownloadUrl)};
        configureDownloadRequest(req);
        m_reply = m_nam.get(req);
        connect(&m_timeout, &QTimer::timeout, m_reply, [this]() {
            m_timedOut = true;
            if (m_reply && m_reply->isRunning()) m_reply->abort();
        });
        connect(m_reply, &QNetworkReply::finished, this, [this]() { handleChecksumFinished(); });
        m_timeout.start(10000);
    }

    void handleChecksumFinished()
    {
        QPointer<QNetworkReply> reply = m_reply;
        cleanupReply();
        if (!reply) {
            finish();
            return;
        }

        if (reply->error() != QNetworkReply::NoError) {
            appendUpdateRuntimeLog(QStringLiteral("UPDATE_FAILED"),
                                   QStringLiteral("sha256_missing: checksum_download_failed %1").arg(buildNetworkErrorText(reply, m_timedOut)));
            showWarning(QString::fromUtf8("Оновлення"),
                        QString::fromUtf8("Не вдалося завантажити SHA256-файл релізу. Оновлення скасовано.\n%1").arg(buildNetworkErrorText(reply, m_timedOut)));
            finish();
            return;
        }

        const int checksumHttpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray checksumBytes = reply->readAll();
        appendUpdateRuntimeLog(QStringLiteral("VERIFY_SHA256"),
                               QStringLiteral("checksum downloaded asset=%1 bytes=%2 http=%3 url=%4")
                                   .arg(m_info.checksumAssetName)
                                   .arg(checksumBytes.size())
                                   .arg(checksumHttpStatus)
                                   .arg(m_info.checksumDownloadUrl));
        if (checksumHttpStatus > 0 && (checksumHttpStatus < 200 || checksumHttpStatus >= 300)) {
            appendUpdateRuntimeLog(QStringLiteral("UPDATE_FAILED"),
                                   QStringLiteral("sha256_missing: checksum_http_status=%1 asset=%2")
                                       .arg(checksumHttpStatus)
                                       .arg(m_info.checksumAssetName));
            showWarning(QString::fromUtf8("Оновлення"),
                        QString::fromUtf8("SHA256-файл релізу повернув HTTP %1. Оновлення скасовано.").arg(checksumHttpStatus));
            finish();
            return;
        }
        const QString checksumText = QString::fromUtf8(checksumBytes);
        m_info.expectedSha256 = extractExpectedSha256(checksumText, m_info.assetName, m_info.checksumAssetName);
        if (m_info.expectedSha256.isEmpty()) {
            appendUpdateRuntimeLog(QStringLiteral("UPDATE_FAILED"),
