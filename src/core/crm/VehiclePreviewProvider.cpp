#include "core/crm/VehiclePreviewProvider.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QImageReader>
#include <QList>
#include <QPixmap>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>

#include <algorithm>

namespace {

bool vehiclePreviewDeepDebug()
{
    static const bool enabled = qEnvironmentVariableIntValue("CS_DEEP_DEBUG") > 0;
    return enabled;
}

struct PreviewCandidate {
    QString path;
    QString normalizedBaseName;
    QStringList tokens;
    QStringList matchTokens;
    QList<int> years;
    int year = 0;
    QString color;
    int score = 0;
};

QString normalizeToken(const QString& raw)
{
    static const QRegularExpression separators(QStringLiteral("[\\s\\-.(),]+"));
    static const QRegularExpression unsupported(QStringLiteral("[^\\p{L}\\p{N}_]+"));
    static const QRegularExpression repeated(QStringLiteral("_+"));
    static const QRegularExpression edges(QStringLiteral("^_|_$"));
    QString value = raw.simplified().toLower();
    value.replace(separators, QStringLiteral("_"));
    value.replace(unsupported, QStringLiteral("_"));
    value.replace(repeated, QStringLiteral("_"));
    return value.trimmed().remove(edges);
}

QStringList normalizedTokens(const QString& raw)
{
    return normalizeToken(raw).split(QLatin1Char('_'), Qt::SkipEmptyParts);
}

QString normalizeModel(const QString& raw)
{
    QString value = raw;
    value.remove(QRegularExpression(QStringLiteral("\\b\\d+[\\.,]\\d+\\b")));
    value.remove(QRegularExpression(QString::fromUtf8("\\b(tdi|tfsi|fsi|tsi|cdi|hdi|dci|crdi|дизель|бензин|hybrid|гібрид|electric|електро)\\b"),
                                    QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption));
    value.remove(QRegularExpression(QStringLiteral("\\b(19|20)\\d{2}\\b")));
    return normalizeToken(value);
}

QStringList normalizeModelTokens(const QString& raw)
{
    return normalizeModel(raw).split(QLatin1Char('_'), Qt::SkipEmptyParts);
}

QString normalizedColor(const QString& raw)
{
    const QString value = raw.simplified().toLower();
    if (value.contains(QString::fromUtf8("чор")) || value.contains(QStringLiteral("black"))) return QStringLiteral("black");
    if (value.contains(QString::fromUtf8("біл")) || value.contains(QStringLiteral("white"))) return QStringLiteral("white");
    if (value.contains(QString::fromUtf8("сір")) || value.contains(QStringLiteral("gray")) || value.contains(QStringLiteral("grey"))) return QStringLiteral("gray");
    if (value.contains(QString::fromUtf8("сріб")) || value.contains(QStringLiteral("silver"))) return QStringLiteral("silver");
    if (value.contains(QString::fromUtf8("черв")) || value.contains(QStringLiteral("red"))) return QStringLiteral("red");
    if (value.contains(QString::fromUtf8("син")) || value.contains(QStringLiteral("blue"))) return QStringLiteral("blue");
    if (value.contains(QString::fromUtf8("зел")) || value.contains(QStringLiteral("green"))) return QStringLiteral("green");
    if (value.contains(QString::fromUtf8("корич")) || value.contains(QStringLiteral("brown"))) return QStringLiteral("brown");
    if (value.contains(QString::fromUtf8("беж")) || value.contains(QStringLiteral("beige"))) return QStringLiteral("beige");
    return normalizeToken(value);
}

QString knownFileColor(const QString& raw)
{
    const QString color = normalizedColor(raw);
    static const QStringList knownColors = {
        QStringLiteral("black"),
        QStringLiteral("white"),
        QStringLiteral("gray"),
        QStringLiteral("silver"),
        QStringLiteral("red"),
        QStringLiteral("blue"),
        QStringLiteral("green"),
        QStringLiteral("brown"),
        QStringLiteral("beige")
    };
    return knownColors.contains(color) ? color : QString();
}

void addPreviewRoot(QStringList* roots, const QString& path)
{
    const QString clean = QDir::cleanPath(path);
    if (clean.isEmpty() || roots->contains(clean)) return;
    roots->push_back(clean);
}

void addUpwardPreviewRoots(QStringList* roots, const QString& startPath)
{
    QDir dir(startPath);
    for (int i = 0; i < 8; ++i) {
        addPreviewRoot(roots, dir.filePath(QStringLiteral("resources/assets/vehicle_previews")));
        if (!dir.cdUp()) break;
    }
}

QStringList localRoots()
{
    QStringList roots;
    const QString appDir = QCoreApplication::applicationDirPath();
    addPreviewRoot(&roots, QDir(appDir).filePath(QStringLiteral("resources/assets/vehicle_previews")));
    addPreviewRoot(&roots, QDir(appDir).filePath(QStringLiteral("assets/vehicle_previews")));
    addPreviewRoot(&roots, QDir(appDir).filePath(QStringLiteral("../resources/assets/vehicle_previews")));
    addPreviewRoot(&roots, QDir(appDir).filePath(QStringLiteral("../assets/vehicle_previews")));
    addUpwardPreviewRoots(&roots, appDir);
    addPreviewRoot(&roots, QDir::current().filePath(QStringLiteral("resources/assets/vehicle_previews")));
    addPreviewRoot(&roots, QDir::current().filePath(QStringLiteral("assets/vehicle_previews")));
    addUpwardPreviewRoots(&roots, QDir::currentPath());
    addPreviewRoot(&roots, QStringLiteral("X:/CS_PG_v2_1_1/resources/assets/vehicle_previews"));
    return roots;
}

bool isSupportedImage(const QFileInfo& file)
{
    const QString suffix = file.suffix().toLower();
    return suffix == QStringLiteral("png")
        || suffix == QStringLiteral("jpg")
        || suffix == QStringLiteral("jpeg")
        || suffix == QStringLiteral("webp");
}

bool isForbiddenGenericName(const QString& normalizedBaseName)
{
    return normalizedBaseName.contains(QStringLiteral("generic"))
        || normalizedBaseName.contains(QStringLiteral("placeholder"))
        || normalizedBaseName.contains(QStringLiteral("silhouette"));
}

QList<int> extractYears(const QStringList& tokens)
{
    QList<int> years;
    for (const QString& token : tokens) {
        bool ok = false;
        const int value = token.toInt(&ok);
        if (ok && value >= 1900 && value <= 2099) years.push_back(value);
    }
    return years;
}

QStringList removeYearTokens(const QStringList& tokens)
{
    QStringList result;
    for (const QString& token : tokens) {
        bool ok = false;
        const int value = token.toInt(&ok);
        if (ok && value >= 1900 && value <= 2099) continue;
        result.push_back(token);
    }
    return result;
}

QString extractColor(const QStringList& tokens)
{
    for (const QString& token : tokens) {
        const QString color = knownFileColor(token);
        if (!color.isEmpty()) return color;
    }
    return QString();
}

bool containsAllTokens(const QStringList& haystack, const QStringList& needles)
{
    if (needles.isEmpty()) return false;
    for (const QString& token : needles) {
        if (!haystack.contains(token)) return false;
    }
    return true;
}

bool containsJoinedModelToken(const QStringList& haystack, const QStringList& modelTokens)
{
    if (modelTokens.size() <= 1) return false;
    return haystack.contains(modelTokens.join(QString()));
}

bool isIgnorableDescriptorToken(const QString& token, const QStringList& modelTokens)
{
    if (knownFileColor(token).isEmpty() == false) return true;

    bool ok = false;
    const int numeric = token.toInt(&ok);
    if (ok && numeric >= 1900 && numeric <= 2100) return true;
    if (ok && numeric >= 0 && numeric <= 9 && !modelTokens.contains(token)) return true;

    static const QStringList ignored = {
        QStringLiteral("tdi"),
        QStringLiteral("tfsi"),
        QStringLiteral("fsi"),
        QStringLiteral("tsi"),
        QStringLiteral("cdi"),
        QStringLiteral("hdi"),
        QStringLiteral("dci"),
        QStringLiteral("crdi"),
        QStringLiteral("diesel"),
        QString::fromUtf8("дизель"),
        QString::fromUtf8("бензин"),
        QStringLiteral("gasoline"),
        QStringLiteral("petrol"),
        QStringLiteral("hybrid"),
        QString::fromUtf8("гібрид"),
        QStringLiteral("electric"),
        QString::fromUtf8("електро")
    };
    return ignored.contains(token);
}

bool modelTokensMatch(const QStringList& fileTokens, const QStringList& modelTokens)
{
    return containsAllTokens(fileTokens, modelTokens)
        || containsJoinedModelToken(fileTokens, modelTokens);
}

struct VehicleLookup {
    QString brand;
    QString model;
};

QStringList stripLeadingTokens(QStringList tokens, const QStringList& leading)
{
    if (tokens.size() < leading.size() || leading.isEmpty()) return tokens;
    for (int i = 0; i < leading.size(); ++i) {
        if (tokens.value(i) != leading.value(i)) return tokens;
    }
    return tokens.mid(leading.size());
}

VehicleLookup resolveVehicleLookup(const QString& brand, const QString& model, const QString& displayName = QString())
{
    VehicleLookup lookup{brand.simplified(), model.simplified()};

    const QString rawIdentity = !lookup.model.isEmpty()
        ? lookup.model
        : displayName.simplified();
    QStringList identityTokens = normalizedTokens(rawIdentity);

    if (lookup.brand.isEmpty() && identityTokens.size() >= 2) {
        lookup.brand = identityTokens.first();
        if (lookup.model.isEmpty()) {
            QStringList modelTokens = identityTokens;
            modelTokens.removeFirst();
            lookup.model = modelTokens.join(QLatin1Char(' '));
        }
    }

    if (lookup.model.isEmpty() && !displayName.trimmed().isEmpty()) {
        QStringList modelTokens = normalizedTokens(displayName);
        modelTokens = stripLeadingTokens(modelTokens, normalizedTokens(lookup.brand));
        lookup.model = modelTokens.join(QLatin1Char(' '));
    }

    if (!lookup.brand.isEmpty() && !lookup.model.isEmpty()) {
        QStringList modelTokens = normalizeModelTokens(lookup.model);
        modelTokens = stripLeadingTokens(modelTokens, normalizedTokens(lookup.brand));
        lookup.model = modelTokens.join(QLatin1Char(' '));
    }

    return lookup;
}

QFileInfoList scanPreviewFiles(const QStringList& roots)
{
    QFileInfoList files;
    QStringList seen;
    for (const QString& root : roots) {
        QDir dir(root);
        if (!dir.exists()) continue;

        const QFileInfoList rootFiles = dir.entryInfoList(QDir::Files | QDir::Readable, QDir::Name);
        for (const QFileInfo& file : rootFiles) {
            const QString path = QDir::cleanPath(file.absoluteFilePath());
            if (seen.contains(path)) continue;
            seen.push_back(path);
            files.push_back(file);
        }

        QDirIterator it(root, QDir::Files | QDir::Readable, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            const QFileInfo file = it.fileInfo();
            const QString path = QDir::cleanPath(file.absoluteFilePath());
            if (seen.contains(path)) continue;
            seen.push_back(path);
            files.push_back(file);
        }
    }
    std::sort(files.begin(), files.end(), [](const QFileInfo& a, const QFileInfo& b) {
        return QDir::cleanPath(a.absoluteFilePath()) < QDir::cleanPath(b.absoluteFilePath());
    });
    return files;
}

QString rootsSignature(const QStringList& roots)
{
    QStringList parts;
    for (const QString& root : roots) {
        const QFileInfo info(root);
        parts.push_back(QStringLiteral("%1|%2|%3")
                            .arg(QDir::cleanPath(root))
                            .arg(info.exists() ? info.lastModified().toMSecsSinceEpoch() : 0)
                            .arg(info.exists() ? info.size() : 0));
    }
    return parts.join(QStringLiteral(";"));
}

const QFileInfoList& indexedPreviewFiles(const QStringList& roots)
{
    static QFileInfoList cachedFiles;
    static QString cachedSignature;
    const QString signature = rootsSignature(roots);
    if (cachedSignature != signature) {
        cachedFiles = scanPreviewFiles(roots);
        cachedSignature = signature;
        if (vehiclePreviewDeepDebug()) {
            qDebug().noquote() << QStringLiteral("[VehiclePreview] indexed roots=%1 files=%2")
                                      .arg(roots.join(QStringLiteral("; ")))
                                      .arg(cachedFiles.size());
        }
    }
    return cachedFiles;
}

QList<PreviewCandidate> scanCandidates(const QString& brand, const QString& model, int* scannedFileCount = nullptr, QStringList* scannedRoots = nullptr)
{
    QList<PreviewCandidate> candidates;

    const QStringList brandTokens = normalizedTokens(brand);
    const QStringList modelTokens = normalizeModelTokens(model);
    if (brandTokens.isEmpty() || modelTokens.isEmpty()) return candidates;

    const QStringList roots = localRoots();
    if (scannedRoots) *scannedRoots = roots;

    int supportedFiles = 0;
    const QFileInfoList files = indexedPreviewFiles(roots);
    for (const QFileInfo& file : files) {
        if (!isSupportedImage(file)) continue;
        ++supportedFiles;

        PreviewCandidate candidate;
        candidate.path = file.absoluteFilePath();
        candidate.normalizedBaseName = normalizeToken(file.completeBaseName());
        candidate.tokens = normalizedTokens(file.completeBaseName());
        candidate.years = extractYears(candidate.tokens);
        candidate.year = candidate.years.isEmpty() ? 0 : candidate.years.first();
        candidate.matchTokens = removeYearTokens(candidate.tokens);
        if (isForbiddenGenericName(candidate.normalizedBaseName)) continue;
        if (!containsAllTokens(candidate.matchTokens, brandTokens)) continue;
        if (!modelTokensMatch(candidate.matchTokens, modelTokens)) continue;

        candidate.color = extractColor(candidate.tokens);
        candidates.push_back(candidate);
    }

    if (scannedFileCount) *scannedFileCount = supportedFiles;
    return candidates;
}

int scoreCandidate(PreviewCandidate* candidate,
                   const QStringList& brandTokens,
                   const QStringList& modelTokens,
                   int requestedYear,
                   const QString& requestedColor)
{
    if (!candidate) return -1;

    const bool exactColor = !requestedColor.isEmpty() && candidate->color == requestedColor;

    int score = 100000;

    if (requestedYear >= 1900 && requestedYear <= 2099 && !candidate->years.isEmpty()) {
        int nearestDiff = 10000;
        bool exactYear = false;
        for (const int candidateYear : candidate->years) {
            const int diff = qAbs(candidateYear - requestedYear);
            nearestDiff = qMin(nearestDiff, diff);
            if (diff == 0) exactYear = true;
        }
        score += exactYear ? 10000 : qMax(0, 5000 - nearestDiff * 20);
    }

    if (exactColor) score += 100;

    int descriptorTokens = 0;
    for (const QString& token : candidate->matchTokens) {
        if (brandTokens.contains(token) || modelTokens.contains(token)) continue;
        if (modelTokens.size() > 1 && token == modelTokens.join(QString())) continue;
        if (isIgnorableDescriptorToken(token, modelTokens)) continue;
        ++descriptorTokens;
    }
    score += qMax(0, 50 - descriptorTokens * 10);

    const QString exactBaseName = (brandTokens + modelTokens).join(QLatin1Char('_'));
    if (candidate->normalizedBaseName == exactBaseName) score += 50;

    const QString suffix = QFileInfo(candidate->path).suffix().toLower();
    if (suffix == QStringLiteral("png") || suffix == QStringLiteral("webp")) score += 10;
    return score;
}

QString bestPreviewPath(const QString& brand, const QString& model, const QString& displayName, int year, const QString& color)
{
    const VehicleLookup lookup = resolveVehicleLookup(brand, model, displayName);
    const QStringList brandTokens = normalizedTokens(lookup.brand);
    const QStringList modelTokens = normalizeModelTokens(lookup.model);
    if (brandTokens.isEmpty() || modelTokens.isEmpty()) {
        static QSet<QString> loggedEmptyLookups;
        const QString logKey = QStringLiteral("%1|%2|empty").arg(normalizeToken(brand), normalizeModel(model));
        if (!loggedEmptyLookups.contains(logKey)) {
            loggedEmptyLookups.insert(logKey);
            qInfo().noquote() << QStringLiteral("CRM_PREVIEW_MISS brand=%1 model=%2 normalized=%3/%4 paths_count=%5")
                                     .arg(brand.simplified(),
                                          model.simplified(),
                                          normalizeToken(brand),
                                          normalizeModel(model),
                                          QString::number(localRoots().size()));
        }
        if (vehiclePreviewDeepDebug()) {
            qDebug().noquote() << QStringLiteral("[VehiclePreview] skipped empty brand/model raw_brand=\"%1\" raw_model=\"%2\" raw_display=\"%3\"")
                                      .arg(brand.simplified(),
                                           model.simplified(),
                                           displayName.simplified());
        }
        return QString();
    }

    int scannedFileCount = 0;
    QStringList roots;
    QList<PreviewCandidate> candidates = scanCandidates(lookup.brand, lookup.model, &scannedFileCount, &roots);
    if (candidates.isEmpty()) {
        static QSet<QString> loggedMisses;
        const QString logKey = QStringLiteral("%1|%2|%3").arg(brandTokens.join(QLatin1Char('_')), modelTokens.join(QLatin1Char('_'))).arg(scannedFileCount);
        if (!loggedMisses.contains(logKey)) {
            loggedMisses.insert(logKey);
            qInfo().noquote() << QStringLiteral("CRM_PREVIEW_MISS brand=%1 model=%2 normalized=%3/%4 paths_count=%5")
                                     .arg(lookup.brand,
                                          lookup.model,
                                          brandTokens.join(QLatin1Char('_')),
                                          modelTokens.join(QLatin1Char('_')),
                                          QString::number(roots.size()));
        }
        if (vehiclePreviewDeepDebug()) {
            qDebug().noquote() << QStringLiteral("[VehiclePreview] no candidate brand=\"%1\" model=\"%2\" files=%3")
                                      .arg(lookup.brand, lookup.model)
                                      .arg(scannedFileCount);
        }
        return QString();
    }

    const QString colorKey = normalizedColor(color);
    for (PreviewCandidate& candidate : candidates) {
        candidate.score = scoreCandidate(&candidate, brandTokens, modelTokens, year, colorKey);
    }

    std::sort(candidates.begin(), candidates.end(), [](const PreviewCandidate& a, const PreviewCandidate& b) {
        if (a.score != b.score) return a.score > b.score;
        return QDir::cleanPath(a.path) < QDir::cleanPath(b.path);
    });

    const PreviewCandidate& best = candidates.constFirst();
    static QSet<QString> loggedHits;
    const QString hitKey = QStringLiteral("%1|%2|%3").arg(brandTokens.join(QLatin1Char('_')), modelTokens.join(QLatin1Char('_')), QDir::cleanPath(best.path));
    if (!loggedHits.contains(hitKey)) {
        loggedHits.insert(hitKey);
        qInfo().noquote() << QStringLiteral("CRM_PREVIEW_HIT brand=%1 model=%2 file=%3")
                                 .arg(lookup.brand,
                                      lookup.model,
                                      QFileInfo(best.path).fileName());
    }
    if (vehiclePreviewDeepDebug()) {
        qDebug().noquote() << QStringLiteral("[VehiclePreview] selected=\"%1\" candidates=%2 score=%3")
                                  .arg(QDir::cleanPath(best.path))
                                  .arg(candidates.size())
                                  .arg(best.score);
    }
    return best.path;
}

QPixmap loadPreviewPixmap(const QString& path, const QSize& size)
{
    if (path.isEmpty()) return {};
    static QHash<QString, QPixmap> pixmapCache;
    const QString cacheKey = QStringLiteral("%1|%2x%3").arg(QDir::cleanPath(path)).arg(size.width()).arg(size.height());
    const auto cached = pixmapCache.constFind(cacheKey);
    if (cached != pixmapCache.constEnd()) return cached.value();

    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QImage image = reader.read();
    if (image.isNull()) {
        if (vehiclePreviewDeepDebug()) {
            qDebug().noquote() << QStringLiteral("[VehiclePreview] selected=\"%1\" image_load=false reason=\"%2\"")
                                      .arg(QDir::cleanPath(path), reader.errorString());
        }
        return {};
    }

    QPixmap pixmap = QPixmap::fromImage(image);
    const bool success = !pixmap.isNull();
    if (vehiclePreviewDeepDebug()) {
        qDebug().noquote() << QStringLiteral("[VehiclePreview] selected=\"%1\" image_load=%2 source_size=%3x%4 target_size=%5x%6")
                                  .arg(QDir::cleanPath(path))
                                  .arg(success ? QStringLiteral("true") : QStringLiteral("false"))
                                  .arg(image.width())
                                  .arg(image.height())
                                  .arg(size.width())
                                  .arg(size.height());
    }
    if (!success) return {};
    QPixmap result = size.isValid() ? pixmap.scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation) : pixmap;
    if (pixmapCache.size() > 512) pixmapCache.clear();
    pixmapCache.insert(cacheKey, result);
    return result;
}

} // namespace

namespace VehiclePreviewProvider {

QStringList candidateResourcePaths(const QString& brand, const QString& model, int year, const QString& color)
{
    Q_UNUSED(year)
    Q_UNUSED(color)

    QStringList paths;
    const VehicleLookup lookup = resolveVehicleLookup(brand, model);
    const QList<PreviewCandidate> candidates = scanCandidates(lookup.brand, lookup.model);
    for (const PreviewCandidate& candidate : candidates) {
        paths.push_back(candidate.path);
    }
    paths.removeDuplicates();
    return paths;
}

QStringList candidateResourcePaths(const QString& brand, const QString& model, const QString& color)
{
    return candidateResourcePaths(brand, model, 0, color);
}

QPixmap preview(const QString& brand, const QString& model, int year, const QString& color, const QSize& size)
{
    const QString path = bestPreviewPath(brand, model, QString(), year, color);
    return loadPreviewPixmap(path, size);
}

QPixmap preview(const QString& brand, const QString& model, const QString& displayName, int year, const QString& color, const QSize& size)
{
    const QString path = bestPreviewPath(brand, model, displayName, year, color);
    return loadPreviewPixmap(path, size);
}

QPixmap preview(const QString& brand, const QString& model, const QString& color, const QSize& size)
{
    return preview(brand, model, 0, color, size);
}

} // namespace VehiclePreviewProvider
