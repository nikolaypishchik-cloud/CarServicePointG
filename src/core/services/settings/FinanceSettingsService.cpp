#include "core/services/settings/FinanceSettingsService.h"

#include "infrastructure/db/sqlite/Db.h"
#include "infrastructure/db/sqlite/DbSettingsCacheSupport.h"
#include "core/services/orders/PricingRecalculationService.h"

#include <QRegularExpression>
#include <QSet>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QObject>
#include <QSqlQuery>
#include <QSqlError>
#include <algorithm>
#include <atomic>

namespace FinanceSettingsService {

void MarkupSettingsBroadcaster::broadcastMarkupSettingsChanged()
{
    emit markupSettingsChanged();
}

namespace {
MarkupSettingsBroadcaster* markupBroadcasterInstance()
{
    static MarkupSettingsBroadcaster broadcaster;
    return &broadcaster;
}
} // namespace

static std::atomic<quint64> g_markupSettingsRevision{1};

quint64 markupSettingsRevision()
{
    return g_markupSettingsRevision.load(std::memory_order_acquire);
}

void notifyMarkupSettingsChanged()
{
    g_markupSettingsRevision.fetch_add(1, std::memory_order_acq_rel);
    QMetaObject::invokeMethod(markupBroadcasterInstance(),
                              "broadcastMarkupSettingsChanged",
                              Qt::QueuedConnection);
}

MarkupSettingsBroadcaster* markupSettingsBroadcaster()
{
    return markupBroadcasterInstance();
}

namespace {
QString canonicalizeOilGradesInText_local(QString text)
{
    static const QRegularExpression oilRx(
        QStringLiteral(R"((?<!\d)(\d{1,2})\s*([WwВв])\s*[-/]?\s*(\d{2})(?!\d))"),
        QRegularExpression::UseUnicodePropertiesOption);
    text.replace(oilRx, QStringLiteral("\\1W\\3"));
    return text;
}

QString normalizeOilGrade_local(QString s)
{
    s = canonicalizeOilGradesInText_local(s.trimmed()).toUpper();
    s.replace(QChar(0x0412), QLatin1Char('W'));
    s.replace(QChar(0x0432), QLatin1Char('W'));
    s.remove(QRegularExpression(QStringLiteral("\\s+")));
    s.remove(QRegularExpression(QStringLiteral("[-/]")));
    return s;
}

QString detectOilGradeFromText_local(const QString& text)
{
    static const QRegularExpression re(
        QStringLiteral(R"((?<!\d)(\d{1,2})\s*[WwВв]\s*[-/]?\s*(\d{2})(?!\d))"),
        QRegularExpression::UseUnicodePropertiesOption);
    const auto m = re.match(text);
    if (!m.hasMatch()) return QString();
    return QStringLiteral("%1W%2").arg(m.captured(1)).arg(m.captured(2));
}


QVariant settingValueForDb(QSqlDatabase db, const QString& key, const QVariant& def)
{
    if (!db.isValid()) return Database::getSetting(key, def);
    if (!db.isOpen() && !db.open()) return def;
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT value FROM settings WHERE \"key\"=? LIMIT 1"));
    q.addBindValue(key);
    if (q.exec() && q.next()) return q.value(0);
    return def;
}

bool setSettingForDb(QSqlDatabase db, const QString& key, const QVariant& value, QString* errorText = nullptr)
{
    if (!db.isValid()) return Database::setSetting(key, value);
    if (!db.isOpen() && !db.open()) {
        if (errorText) *errorText = QStringLiteral("DB connection is not open for setting %1").arg(key);
        return false;
    }
    QSqlQuery q(db);
    q.prepare(QStringLiteral("INSERT INTO settings(\"key\", value) VALUES(?, ?) ON CONFLICT(\"key\") DO UPDATE SET value=excluded.value"));
    q.addBindValue(key);
    q.addBindValue(value);
    if (!q.exec()) {
        if (errorText) *errorText = q.lastError().text();
        DbSettingsCacheSupport::remove(key);
        return false;
    }
    DbSettingsCacheSupport::store(key, value);
    return true;
}

QStringList standardOilGradeChoices_local()
{
    return {
        QStringLiteral("0W8"), QStringLiteral("0W12"), QStringLiteral("0W16"), QStringLiteral("0W20"),
        QStringLiteral("0W30"), QStringLiteral("0W40"), QStringLiteral("0W50"),
        QStringLiteral("5W16"), QStringLiteral("5W20"), QStringLiteral("5W30"), QStringLiteral("5W40"),
        QStringLiteral("5W50"), QStringLiteral("5W60"),
        QStringLiteral("10W30"), QStringLiteral("10W40"), QStringLiteral("10W50"), QStringLiteral("10W60"),
        QStringLiteral("15W40"), QStringLiteral("15W50"), QStringLiteral("20W50")
    };
}
} // namespace


MarkupSnapshot loadMarkupSnapshot()
{
    MarkupSnapshot snapshot;
    snapshot.storeMarkupsJson = Database::getSetting(QStringLiteral("settings.fin.store_markups"), QStringLiteral("{}")).toString();
    snapshot.oilStoreMarkupsJson = Database::getSetting(QStringLiteral("settings.fin.oil_store_markups"), QStringLiteral("[]")).toString();
    snapshot.defaultPartsMarkupPercent = Database::getSetting(QStringLiteral("settings.fin.parts_markup"), 0.0).toDouble();
    snapshot.oilDefault = Database::getSetting(QStringLiteral("settings.fin.oil_markup"), 0.0).toDouble();
    return snapshot;
}


bool saveMarkupSettings(const MarkupSaveRequest& request,
                        bool* changed,
                        QString* errorText)
{
    return saveMarkupSettings(Database::db(), request, changed, errorText);
}

bool saveMarkupSettings(QSqlDatabase db,
                        const MarkupSaveRequest& request,
                        bool* changed,
                        QString* errorText)
{
    const QString oldStoreMarkups = settingValueForDb(db, QStringLiteral("settings.fin.store_markups"), QStringLiteral("{}")).toString();
    const QString oldOilStoreMarkups = settingValueForDb(db, QStringLiteral("settings.fin.oil_store_markups"), QStringLiteral("[]")).toString();
    const double oldPartsDefault = settingValueForDb(db, QStringLiteral("settings.fin.parts_markup"), 0.0).toDouble();
    const double oldOilDefault = settingValueForDb(db, QStringLiteral("settings.fin.oil_markup"), 0.0).toDouble();

    const bool hasChanges = oldStoreMarkups != request.storeMarkupsJson ||
                            oldOilStoreMarkups != request.oilStoreMarkupsJson ||
                            !qFuzzyCompare(oldPartsDefault + 1.0, request.defaultPartsMarkupPercent + 1.0) ||
                            !qFuzzyCompare(oldOilDefault + 1.0, request.oilDefault + 1.0);
    if (changed) *changed = hasChanges;

    QString settingError;
    if (!setSettingForDb(db, QStringLiteral("settings.fin.store_markups"), request.storeMarkupsJson, &settingError) ||
        !setSettingForDb(db, QStringLiteral("settings.fin.oil_store_markups"), request.oilStoreMarkupsJson, &settingError) ||
        !setSettingForDb(db, QStringLiteral("settings.fin.parts_markup"), request.defaultPartsMarkupPercent, &settingError) ||
        !setSettingForDb(db, QStringLiteral("settings.fin.oil_markup"), request.oilDefault, &settingError)) {
        if (errorText) *errorText = settingError;
        return false;
    }

    if (!hasChanges) return true;

    notifyMarkupSettingsChanged();

    if (request.recalculationMode == MarkupRecalculationMode::NoRecalculate
        || request.recalculationMode == MarkupRecalculationMode::NewRowsOnly) {
        return true;
    }

    const auto scope = request.recalculationMode == MarkupRecalculationMode::OpenOrdersOnly
        ? PricingRecalculationService::RecalculationScope::OpenOrdersOnly
        : PricingRecalculationService::RecalculationScope::AllOrders;

    QString recalcError;
    if (!PricingRecalculationService::recalcPartMarkupsFromSettings(db, scope, &recalcError)) {
        if (errorText) *errorText = recalcError;
        return false;
    }
    return true;
}

QList<OilMarkupRule> loadOilMarkupRules(const QString& raw)
{
    QList<OilMarkupRule> out;
    const auto doc = QJsonDocument::fromJson(raw.toUtf8());
    if (doc.isArray()) {
        for (const auto& v : doc.array()) {
            if (!v.isObject()) continue;
            const auto o = v.toObject();
            bool ok = false;
            const double pct = o.value(QStringLiteral("pct")).toVariant().toDouble(&ok);
            if (!ok) continue;
            OilMarkupRule row;
            row.store = o.value(QStringLiteral("store")).toString().trimmed();
            row.brand = o.value(QStringLiteral("brand")).toString().trimmed();
            row.oilGrade = normalizeOilGrade_local(o.value(QStringLiteral("oil")).toString());
            row.pct = pct;
            out.append(row);
        }
        return out;
    }
    if (!doc.isObject()) return out;
    const auto obj = doc.object();
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        bool ok = false;
        const double pct = it.value().toVariant().toDouble(&ok);
        if (!ok) continue;
        OilMarkupRule row;
        row.store = it.key().trimmed();
        row.pct = pct;
        out.append(row);
    }
    return out;
}

QStringList loadOilGradeChoices()
{
    QSet<QString> grades;
    for (const QString& g : standardOilGradeChoices_local()) grades.insert(g);

    QSqlQuery q(Database::db());
    if (q.exec(QStringLiteral("SELECT COALESCE(name,'') FROM work_order_parts"))) {
        while (q.next()) {
            const QString grade = detectOilGradeFromText_local(q.value(0).toString());
            if (!grade.isEmpty()) grades.insert(grade);
        }
    }

    QStringList out = grades.values();
    std::sort(out.begin(), out.end(), [](const QString& a, const QString& b) {
        static const QRegularExpression rx(QStringLiteral(R"((\d{1,2})W(\d{2}))"));
        const auto ma = rx.match(a);
        const auto mb = rx.match(b);
        if (ma.hasMatch() && mb.hasMatch()) {
            const int aw = ma.captured(1).toInt();
            const int bw = mb.captured(1).toInt();
            if (aw != bw) return aw < bw;
            const int ah = ma.captured(2).toInt();
            const int bh = mb.captured(2).toInt();
            if (ah != bh) return ah < bh;
        }
        return a.localeAwareCompare(b) < 0;
    });
    return out;
}

} // namespace FinanceSettingsService

