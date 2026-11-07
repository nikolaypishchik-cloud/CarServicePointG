// Extracted from src/app/update/UpdateServiceSupport.cpp for P1/P2 monolith reduction.
// Included exactly once by the original translation unit/header; do not compile separately.

    atomicWriteJsonFile(failedUpdatePackagesPath(), root);
}


QString normalizeSha256Digest(QString s);
int compareVersions(const QString& a, const QString& b);

// UPDATE_SKIP_ALREADY_APPLIED_PACKAGE marker: already-applied package SHA prevents repeat install loops.
QString appliedUpdateMarkerPath()
{
    return QDir(updateRuntimeDirPath()).filePath(QStringLiteral("applied_update_marker.json"));
}

QString updateStatePath()
{
    const QString programData = qEnvironmentVariable("ProgramData").trimmed();
    if (!programData.isEmpty()) {
        const QString dirPath = QDir(programData).filePath(QStringLiteral("CarServicePointG"));
        if (QDir().mkpath(dirPath)) {
            return QDir(dirPath).filePath(QStringLiteral("update_state.json"));
        }
    }
    return QDir(updateRuntimeDirPath()).filePath(QStringLiteral("update_state.json"));
}

bool markerMatchesPackageFile(const QString& path, const QString& releaseVersion, const QString& packageSha256)
{
    const QString expectedSha = normalizeSha256Digest(packageSha256);
    if (expectedSha.isEmpty()) return false;

    QFile f(path);
    if (!f.exists() || !f.open(QIODevice::ReadOnly)) return false;

    QJsonParseError pe{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) return false;

    const QJsonObject obj = doc.object();
    const QString markerVersion = firstNonEmptyVersionText(
        obj.value(QStringLiteral("expectedVersion")).toString(),
        firstNonEmptyVersionText(
            obj.value(QStringLiteral("installed_version")).toString(),
            obj.value(QStringLiteral("version")).toString()));
    if (markerVersion.trimmed().isEmpty() || compareVersions(markerVersion, releaseVersion) != 0) return false;

    const QString markerSha = normalizeSha256Digest(firstNonEmptyVersionText(
        obj.value(QStringLiteral("packageSha256")).toString(),
        obj.value(QStringLiteral("installed_asset_sha256")).toString()));
    return !markerSha.isEmpty() && markerSha == expectedSha;
}

bool isAppliedUpdatePackage(const QString& releaseVersion, const QString& packageSha256)
{
    return markerMatchesPackageFile(appliedUpdateMarkerPath(), releaseVersion, packageSha256)
        || markerMatchesPackageFile(updateStatePath(), releaseVersion, packageSha256);
}


void clearUpdateRuntimeLog()
{
    const QString path = updateRuntimeLogPath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    const QFileInfo info(path);
    if (info.exists() && info.size() > 0) {
        const QString archiveDirPath = QDir(updateRuntimeDirPath()).filePath(QStringLiteral("logs"));
        QDir().mkpath(archiveDirPath);
        const QString archivePath = QDir(archiveDirPath).filePath(
            QStringLiteral("apply_update_%1.log")
                .arg(QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd_HHmmss_zzz"))));
        QFile::copy(path, archivePath);
        pruneArchivedUpdateLogs(20);
    }
    safeRemoveUpdateFile(path);
}

void appendUpdateRuntimeLog(const QString& stage, const QString& message)
{
    const QString path = updateRuntimeLogPath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) return;
    const QString line = QStringLiteral("[%1] %2 %3\n")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")),
             stage.trimmed().isEmpty() ? QStringLiteral("UPDATE") : stage.trimmed(),
             message.trimmed());
    f.write(line.toUtf8());
}

QString statusField(const QString& status, const QString& key)
{
    const QRegularExpression rx(QStringLiteral("(?:^|[;\\s])%1=([^;\\r\\n]+)").arg(QRegularExpression::escape(key)));
    const auto m = rx.match(status);
    return m.hasMatch() ? m.captured(1).trimmed() : QString();
}

QString trimReleaseTag(QString s)
{
    s = s.trimmed();
    if (s.startsWith('v', Qt::CaseInsensitive)) s.remove(0, 1);
    return s;
}

QString normalizeVersion(const QString& v)
{
    QString s = trimReleaseTag(v);
    s.replace(QRegularExpression(QStringLiteral("[^0-9.]")), QString());
    while (s.endsWith('.')) s.chop(1);
    return s;
}

int compareVersions(const QString& a, const QString& b)
{
    const QStringList pa = normalizeVersion(a).split('.', Qt::SkipEmptyParts);
    const QStringList pb = normalizeVersion(b).split('.', Qt::SkipEmptyParts);
    const int n = qMax(pa.size(), pb.size());
    for (int i = 0; i < n; ++i) {
        const int va = (i < pa.size()) ? pa.at(i).toInt() : 0;
        const int vb = (i < pb.size()) ? pb.at(i).toInt() : 0;
        if (va < vb) return -1;
        if (va > vb) return 1;
    }
    return 0;
}

QString normalizedPathLower(const QString& path)
{
    QString p = QDir::fromNativeSeparators(QDir::cleanPath(path)).toLower();
    while (p.endsWith('/')) p.chop(1);
    return p;
}

bool isUnderDirectory(const QString& path, const QString& dir)
{
    const QString p = normalizedPathLower(path);
    const QString d = normalizedPathLower(dir);
    return !d.isEmpty() && (p == d || p.startsWith(d + '/'));
}

bool isProgramFilesLocation(const QString& path)
{
    const QStringList roots = {
        qEnvironmentVariable("ProgramFiles"),
        qEnvironmentVariable("ProgramFiles(x86)"),
        qEnvironmentVariable("ProgramW6432")
    };
    for (const QString& root : roots) {
        if (!root.trimmed().isEmpty() && isUnderDirectory(path, root)) return true;
    }
    return false;
}

bool isWritableDirectory(const QString& dirPath)
{
    QDir dir(dirPath);
    if (!dir.exists()) return false;
    const QString probe = dir.filePath(QStringLiteral(".__cs_write_probe__"));
    QFile f(probe);
    const bool ok = f.open(QIODevice::WriteOnly | QIODevice::Truncate);
    if (ok) {
        f.close();
        safeRemoveUpdateFile(probe, {QStringLiteral(".__cs_write_probe__")});
    }
    return ok;
}

bool hasInstalledUninstaller(const QString& appDir)
{
    QDir dir(appDir);
    const QStringList patterns = {
        QStringLiteral("unins*.exe"),
        QStringLiteral("unins*.dat"),
        QStringLiteral("unins*.msg")
    };
    for (const QString& pattern : patterns) {
        const QFileInfoList matches = dir.entryInfoList({pattern}, QDir::Files | QDir::NoSymLinks | QDir::Readable);
        if (!matches.isEmpty()) return true;
    }
    return false;
}

bool prefersInstallerPackage()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    return hasInstalledUninstaller(appDir)
        || isProgramFilesLocation(appDir)
        || !isWritableDirectory(appDir);
}

bool looksLikeInstallerAsset(const QString& lowName)
{
    if (!lowName.endsWith(".exe")) return false;

    if (lowName.contains("setup") || lowName.contains("install") || lowName.contains("installer")) {
        return true;
    }

    // Canonical GitHub release assets for 2.1.1 use the setup-style name
    // CarServicePointG_Setup_x86_x64_<version>.exe so the updater can
    // reliably select an installer package. Keep the older versioned
    // CarServicePointG_<version>.exe compatibility path only to avoid treating
    // the plain runtime executable CarServicePointG.exe as an update installer.
    return (lowName.startsWith("carservicepointg_")
            || lowName.startsWith("carservicepointg-"))
        && lowName.contains(QRegularExpression(QStringLiteral("[0-9]+[._-][0-9]+[._-][0-9]+")));
}

bool looksLikeChecksumAsset(const QString& lowName)
{
    return lowName.endsWith(".sha256")
        || lowName.endsWith(".sha256.txt")
        || lowName.endsWith(".sha256sum")
        || lowName.endsWith(".sha256sums")
        || lowName.endsWith("checksums.txt")
        || lowName.endsWith("sha256sums.txt")
        || lowName.contains("sha256");
}

bool looksLikeBlockedExecutableAsset(const QString& lowName)
{
    return lowName.contains("vc_redist")
        || lowName.contains("unins")
        || lowName.contains("maintenancetool")
        || lowName.contains("helper")
        || lowName.contains("debug");
}

QString normalizeSha256Digest(QString s)
{
    s = s.trimmed().toLower();
    if (s.startsWith(QStringLiteral("sha256:"))) s.remove(0, 7);
    static const QRegularExpression rx(QStringLiteral("^[a-f0-9]{64}$"));
    return rx.match(s).hasMatch() ? s : QString();
}

QString digestFromAssetObject(const QJsonObject& asset)
{
    return normalizeSha256Digest(asset.value(QStringLiteral("digest")).toString());
}

bool assetNameMatchesVersionToken(const QString& lowName, const QString& token)
{
    if (token.trimmed().isEmpty()) return false;
    const QString escaped = QRegularExpression::escape(token.toLower());
    const QRegularExpression rx(
        QStringLiteral("(^|[^0-9a-z])v?%1(?![._-]?[0-9])(?=$|[^0-9a-z])").arg(escaped),
        QRegularExpression::CaseInsensitiveOption);
    return rx.match(lowName).hasMatch();
}

bool assetNameMatchesVersion(const QString& lowName, const QString& version)
{
    const QString normalized = normalizeVersion(version);
    if (normalized.isEmpty()) return false;

    QString underscored = normalized;
    underscored.replace('.', '_');
    QString dashed = normalized;
    dashed.replace('.', '-');

    return assetNameMatchesVersionToken(lowName, normalized)
        || assetNameMatchesVersionToken(lowName, underscored)
        || assetNameMatchesVersionToken(lowName, dashed);
}

bool isCanonicalInstallerAssetName(const QString& lowName, const QString& version)
{
    const QString normalized = normalizeVersion(version);
    if (normalized.isEmpty()) return false;
    return lowName == QStringLiteral("carservicepointg_setup_x86_x64_v%1.exe").arg(normalized);
}


bool downloadUrlMatchesAssetName(const QString& assetName, const QString& downloadUrl)
{
    const QString name = assetName.trimmed();
    if (name.isEmpty()) return false;
    const QString decodedFileName = QUrl(downloadUrl).fileName(QUrl::FullyDecoded).trimmed();
    if (decodedFileName.compare(name, Qt::CaseInsensitive) == 0) return true;
    const QString decodedUrl = QUrl::fromPercentEncoding(downloadUrl.toUtf8()).toLower();
    return decodedUrl.endsWith(QStringLiteral("/") + name.toLower());
}

QString safeUpdatePackageFileName(QString assetName)
{
    assetName = QFileInfo(assetName.trimmed()).fileName();
    assetName.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]")), QStringLiteral("_"));
    assetName.replace(QRegularExpression(QStringLiteral("_+")), QStringLiteral("_"));
    if (assetName.isEmpty() || assetName == QStringLiteral(".") || assetName == QStringLiteral("..")) {
        return QStringLiteral("update_package.exe");
    }
    return assetName;
}

void configureGithubApiRequest(QNetworkRequest& req)
{
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("CarServicePointG-Updater/1.1"));
    req.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setMaximumRedirectsAllowed(10);
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
    req.setRawHeader("Cache-Control", "no-cache, no-store, max-age=0");
    req.setRawHeader("Pragma", "no-cache");
    req.setRawHeader("Expires", "0");
}

void configureDownloadRequest(QNetworkRequest& req)
{
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("CarServicePointG-Updater/1.1"));
    req.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setMaximumRedirectsAllowed(10);
    req.setRawHeader("Cache-Control", "no-cache, no-store, max-age=0");
    req.setRawHeader("Pragma", "no-cache");
    req.setRawHeader("Expires", "0");
}

QString buildNetworkErrorText(QNetworkReply* reply, bool timedOut)
{
    if (timedOut) return QString::fromUtf8("Операція перевищила час очікування.");
    if (!reply) return QString::fromUtf8("Мережева відповідь недоступна.");

    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QString base = reply->errorString().trimmed();
    if (base.isEmpty()) base = QString::fromUtf8("Невідома мережева помилка.");
    if (httpStatus > 0) {
        base += QString::fromUtf8(" (HTTP %1)").arg(httpStatus);
    }

    const QByteArray remaining = reply->rawHeader("X-RateLimit-Remaining");
    if (httpStatus == 403 && remaining == "0") {
        base += QString::fromUtf8(". GitHub API rate limit вичерпано.");
    }
    return base;
}

QString chooseChecksumAssetName(const QString& packageAssetName, const QStringList& checksumNames)
{
    if (packageAssetName.trimmed().isEmpty() || checksumNames.isEmpty()) return QString();

    const QString packageLower = packageAssetName.toLower();
    const QString packageSha = packageLower + QStringLiteral(".sha256");
    const QString packageShaTxt = packageLower + QStringLiteral(".sha256.txt");
    for (const QString& name : checksumNames) {
        const QString low = name.toLower();
        if (low == packageSha || low == packageShaTxt || low.endsWith(QStringLiteral("/") + packageSha)) {
            return name;
        }
    }

    for (const QString& name : checksumNames) {
        const QString low = name.toLower();
        if (low == QStringLiteral("sha256sums.txt") || low.endsWith(QStringLiteral("/sha256sums.txt"))) {
            return name;
        }
    }

    return QString();
}

QString extractExpectedSha256(const QString& checksumText, const QString& assetName)
{
    return extractExpectedSha256(checksumText, assetName, QString());
}

QString extractExpectedSha256(const QString& checksumText, const QString& assetName, const QString& checksumAssetName)
{
    const QString normalizedAsset = assetName.trimmed().toLower();
    const QString normalizedChecksum = checksumAssetName.trimmed().toLower();
    const QStringList lines = checksumText.split(QRegularExpression(QStringLiteral("[\r\n]+")), Qt::SkipEmptyParts);
    const QRegularExpression hashRx(QStringLiteral("(^|[^A-Fa-f0-9])([A-Fa-f0-9]{64})([^A-Fa-f0-9]|$)"));

    for (const QString& rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (line.isEmpty()) continue;
        const auto match = hashRx.match(line);
        if (!match.hasMatch()) continue;
        const QString lowLine = line.toLower();
        if (!normalizedAsset.isEmpty() && lowLine.contains(normalizedAsset)) {
            return normalizeSha256Digest(match.captured(2));
        }
    }

    const bool packageSpecificShaFile = !normalizedAsset.isEmpty()
        && (normalizedChecksum == normalizedAsset + QStringLiteral(".sha256")
            || normalizedChecksum == normalizedAsset + QStringLiteral(".sha256.txt")
            || normalizedChecksum.endsWith(QStringLiteral("/") + normalizedAsset + QStringLiteral(".sha256"))
            || normalizedChecksum.endsWith(QStringLiteral("/") + normalizedAsset + QStringLiteral(".sha256.txt")));

    if (packageSpecificShaFile && lines.size() == 1) {
        const auto match = hashRx.match(lines.first().trimmed());
        if (match.hasMatch()) return normalizeSha256Digest(match.captured(2));
    }

    return QString();
}

QString computeFileSha256(const QString& filePath, QString* errorOut)
{
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly)) {
        if (errorOut) *errorOut = QString::fromUtf8("Не вдалося відкрити завантажений пакет для SHA256-перевірки");
        return QString();
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&f)) {
        if (errorOut) *errorOut = QString::fromUtf8("Не вдалося обчислити SHA256 завантаженого пакета");
        return QString();
    }

    return QString::fromLatin1(hash.result().toHex()).toLower();
}

UpdateService::ReleaseInfo parseReleaseInfo(const QByteArray& bytes, QString* errorOut)
{
    struct CandidateAsset {
        QString name;
        QString url;
        QString sha256;
        qint64 size = -1;
        QDateTime updatedAt;
        int score = -1;
    };

    auto considerCandidate = [](CandidateAsset& candidate,
                                const QString& name,
                                const QString& url,
                                const QString& sha256,
                                qint64 size,
                                const QDateTime& updatedAt,
                                int score) {
        if (score > candidate.score) {
            candidate.name = name;
            candidate.url = url;
            candidate.sha256 = sha256;
            candidate.size = size;
            candidate.updatedAt = updatedAt;
            candidate.score = score;
        }
    };

    UpdateService::ReleaseInfo out;
    out.currentVersion = UpdateService::currentVersion();
    out.apiUrl = UpdateService::latestReleaseApiUrl();
    qInfo().noquote() << QStringLiteral("UPDATE_CHECK start current=%1 api=%2 installedMode=%3")
                         .arg(out.currentVersion,
                              out.apiUrl,
                              prefersInstallerPackage() ? QStringLiteral("true") : QStringLiteral("false"));

    QJsonParseError pe{};
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        if (errorOut) *errorOut = QStringLiteral("Некоректна відповідь сервера оновлень");
        qWarning() << "UPDATE_CHECK failed: invalid json" << pe.errorString();
        appendUpdateRuntimeLog(QStringLiteral("UPDATE_FAILED"),
                               QStringLiteral("manifest_download_failed: invalid_json error=%1").arg(pe.errorString()));
        return out;
    }

    const QJsonObject obj = doc.object();
    out.latestVersion = trimReleaseTag(obj.value(QStringLiteral("tag_name")).toString());
    out.title = obj.value(QStringLiteral("name")).toString();
    out.notes = obj.value(QStringLiteral("body")).toString();
    out.htmlUrl = obj.value(QStringLiteral("html_url")).toString();
    out.prerelease = obj.value(QStringLiteral("prerelease")).toBool(false);
    out.draft = obj.value(QStringLiteral("draft")).toBool(false);
    const QString releaseId = QString::number(obj.value(QStringLiteral("id")).toVariant().toLongLong());
    const QString rawTag = obj.value(QStringLiteral("tag_name")).toString().trimmed();
    const QString publishedAt = obj.value(QStringLiteral("published_at")).toString().trimmed();
    const QString createdAt = obj.value(QStringLiteral("created_at")).toString().trimmed();
    const QString assetsUrl = obj.value(QStringLiteral("assets_url")).toString().trimmed();
    qInfo().noquote() << QStringLiteral("UPDATE_CHECK release latest=%1 rawTag=%2 id=%3 url=%4 draft=%5 prerelease=%6 publishedAt=%7 createdAt=%8 assets=%9")
                         .arg(out.latestVersion,
                              rawTag,
                              releaseId,
                              out.htmlUrl,
                              out.draft ? QStringLiteral("true") : QStringLiteral("false"),
                              out.prerelease ? QStringLiteral("true") : QStringLiteral("false"),
                              publishedAt,
                              createdAt)
                         .arg(obj.value(QStringLiteral("assets")).toArray().size());
    appendUpdateRuntimeLog(QStringLiteral("DOWNLOAD_MANIFEST"),
                           QStringLiteral("release rawTag=%1 latest=%2 id=%3 html=%4 assetsUrl=%5 draft=%6 prerelease=%7 publishedAt=%8 createdAt=%9")
