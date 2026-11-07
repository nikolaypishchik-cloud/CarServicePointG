#include "core/services/orders/PartMarkupPolicyService.h"
#include "core/util/StoreNameNormalizer.h"

#include "infrastructure/db/sqlite/Db.h"
#include "core/util/FormatUtils.h"
#include "core/util/OilServiceClassifier.h"
#include "core/services/orders/PartPricingEngine.h"
#include "core/services/WorkshopInventoryService.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QSqlRecord>
#include <QDate>
#include <algorithm>
#include <QtGlobal>

namespace {

QMap<QString,double> loadInventoryClientPrices(QSqlDatabase db, QMap<QString,double>* purchasePrices, QString* error = nullptr)
{
    QMap<QString,double> prices;
    if (!db.isValid() || !db.isOpen()) {if(error)*error=QStringLiteral("Inventory pricing database is not open");return prices;}
    QSqlQuery query(db);
    const QString materialOnly = db.record("workshop_inventory_purchases").indexOf("stock_kind") >= 0
        ? QStringLiteral("stock_kind='material' AND ") : QString();
    query.prepare("SELECT material_name,client_price,unit_cost FROM workshop_inventory_purchases "
                  "WHERE " + materialOnly + "client_price IS NOT NULL AND date(CASE WHEN substr(trim(purchased_on),3,1)='.' AND substr(trim(purchased_on),6,1)='.' THEN CASE WHEN substr(trim(purchased_on),7,4) GLOB '[0-9][0-9][0-9][0-9]' THEN substr(trim(purchased_on),7,4) ELSE '20'||substr(trim(purchased_on),7,2) END||'-'||substr(trim(purchased_on),4,2)||'-'||substr(trim(purchased_on),1,2) ELSE purchased_on END)<=date(?) ORDER BY date(CASE WHEN substr(trim(purchased_on),3,1)='.' AND substr(trim(purchased_on),6,1)='.' THEN CASE WHEN substr(trim(purchased_on),7,4) GLOB '[0-9][0-9][0-9][0-9]' THEN substr(trim(purchased_on),7,4) ELSE '20'||substr(trim(purchased_on),7,2) END||'-'||substr(trim(purchased_on),4,2)||'-'||substr(trim(purchased_on),1,2) ELSE purchased_on END),id");
    query.addBindValue(QDate::currentDate().toString(Qt::ISODate));
    if (query.exec()) while(query.next()) {
        prices.insert(WorkshopInventoryService::normalizedMaterialKey(query.value(0).toString()),query.value(1).toDouble());
        purchasePrices->insert(WorkshopInventoryService::normalizedMaterialKey(query.value(0).toString()),query.value(2).toDouble());
    }
    if(query.lastError().isValid()){if(error)*error=query.lastError().text();return {};}
    return prices;
}

QString settingString(QSqlDatabase db, const QString& settingKey, const QString& defaultValue, QString* error = nullptr)
{
    if (!db.isValid() || !db.isOpen()) {if(error)*error=QStringLiteral("Pricing settings database is not open");return defaultValue;}
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT value FROM settings WHERE \"key\"=? LIMIT 1"));
    q.addBindValue(settingKey);
    if (q.exec() && q.next()) return q.value(0).toString();
    if(q.lastError().isValid() && error)*error=q.lastError().text();
    return defaultValue;
}

QMap<QString,double> parseMarkupMap(const QString& raw)
{
    QMap<QString,double> out;
    const auto doc = QJsonDocument::fromJson(raw.toUtf8());
    if (!doc.isObject()) return out;
    const auto obj = doc.object();
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        bool ok = false;
        const double value = it.value().toVariant().toDouble(&ok);
        const QString key = it.key().trimmed();
        if (!key.isEmpty() && ok) out.insert(key, value);
    }
    return out;
}

QMap<QString,double> loadMarkupMapFromSetting(const QString& settingKey)
{
    return parseMarkupMap(Database::getSetting(settingKey, QStringLiteral("{}")).toString());
}

QMap<QString,double> loadMarkupMapFromSetting(QSqlDatabase db, const QString& settingKey, QString* error = nullptr)
{
    return parseMarkupMap(settingString(db, settingKey, QStringLiteral("{}"),error));
}

QString detectOilGradeFromText(const QString& text)
{
    static const QRegularExpression re(
        QStringLiteral(R"((?<!\d)(\d{1,2})\s*[WwВв]\s*[-/]?\s*(\d{2})(?!\d))"),
        QRegularExpression::UseUnicodePropertiesOption);
    const auto m = re.match(text);
    if (!m.hasMatch()) return QString();
    return QStringLiteral("%1W%2").arg(m.captured(1)).arg(m.captured(2));
}

QList<PartMarkupPolicyService::OilMarkupRule> parseOilMarkupRules(const QString& raw)
{
    QList<PartMarkupPolicyService::OilMarkupRule> out;
    const auto doc = QJsonDocument::fromJson(raw.toUtf8());
    if (doc.isArray()) {
        for (const auto& v : doc.array()) {
            if (!v.isObject()) continue;
            const auto o = v.toObject();
            bool ok = false;
            const double pct = o.value(QStringLiteral("pct")).toVariant().toDouble(&ok);
            if (!ok) continue;
            PartMarkupPolicyService::OilMarkupRule rule;
            rule.store = o.value(QStringLiteral("store")).toString().trimmed();
            rule.brand = o.value(QStringLiteral("brand")).toString().trimmed();
            rule.oilGrade = PartMarkupPolicyService::normalizeOilGrade(o.value(QStringLiteral("oil")).toString());
            rule.pct = pct;
            out.append(rule);
        }
        return out;
    }
    if (!doc.isObject()) return out;
    const auto obj = doc.object();
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        bool ok = false;
        const double pct = it.value().toVariant().toDouble(&ok);
        if (!ok) continue;
        PartMarkupPolicyService::OilMarkupRule rule;
        rule.store = it.key().trimmed();
        rule.pct = pct;
        out.append(rule);
    }
    return out;
}

QList<PartMarkupPolicyService::OilMarkupRule> loadOilMarkupRules()
{
    return parseOilMarkupRules(Database::getSetting(QStringLiteral("settings.fin.oil_store_markups"), QStringLiteral("[]")).toString());
}

QList<PartMarkupPolicyService::OilMarkupRule> loadOilMarkupRules(QSqlDatabase db, QString* error = nullptr)
{
    return parseOilMarkupRules(settingString(db, QStringLiteral("settings.fin.oil_store_markups"), QStringLiteral("[]"),error));
}

double lookupStoreMarkupPercent(const QMap<QString,double>& map, const QString& store, bool* found = nullptr)
{
    if (found) *found = false;
    const QString value = store.trimmed();
    if (value.isEmpty()) return 0.0;
    if (map.contains(value)) {
        if (found) *found = true;
        return map.value(value);
    }
    const QString key = PartMarkupPolicyService::canonStoreKey(value);
    for (auto it = map.begin(); it != map.end(); ++it) {
        if (PartMarkupPolicyService::canonStoreKey(it.key()) == key) {
            if (found) *found = true;
            return it.value();
        }
    }
    return 0.0;
}

const PartMarkupPolicyService::OilMarkupRule* findBestOilRule(const QList<PartMarkupPolicyService::OilMarkupRule>& rules, const QString& store, const QString& oilGrade)
{
    const QString storeKey = PartMarkupPolicyService::canonStoreKey(store);
    const QString gradeKey = PartMarkupPolicyService::normalizeOilGrade(oilGrade);
    auto matchesStore = [&](const QString& candidate) {
        return !candidate.trimmed().isEmpty() && PartMarkupPolicyService::canonStoreKey(candidate) == storeKey;
    };
    auto matchesGrade = [&](const QString& candidate) {
        return PartMarkupPolicyService::normalizeOilGrade(candidate).isEmpty() || PartMarkupPolicyService::normalizeOilGrade(candidate) == gradeKey;
    };

    const int priorities[][2] = {{1,1}, {1,0}};
    for (const auto& priority : priorities) {
        for (const auto& row : rules) {
            const bool hasStore = !row.store.trimmed().isEmpty();
            const bool hasGrade = !PartMarkupPolicyService::normalizeOilGrade(row.oilGrade).isEmpty();
            if (int(hasStore) != priority[0] || int(hasGrade) != priority[1]) continue;
            if (matchesStore(row.store) && matchesGrade(row.oilGrade)) return &row;
        }
    }
    return nullptr;
}

double lookupOilMarkupPercent(const QList<PartMarkupPolicyService::OilMarkupRule>& rules, const QString& store, const QString& oilGrade, bool* found = nullptr)
{
    if (found) *found = false;
    const PartMarkupPolicyService::OilMarkupRule* rule = findBestOilRule(rules, store, oilGrade);
    if (!rule) return 0.0;
    if (found) *found = true;
    return rule->pct;
}

} // namespace

QString PartMarkupPolicyService::canonStoreKey(const QString& raw)
{
    return StoreNameNormalizer::canonKey(raw);
}

bool PartMarkupPolicyService::isInventoryStore(const QString& store)
{
    const QString key=canonStoreKey(store);
    return key==canonStoreKey(QString::fromUtf8("База")) || key==canonStoreKey(QString::fromUtf8("Склад СТО"));
}

QString PartMarkupPolicyService::normalizeOilGrade(QString raw)
{
    raw = raw.trimmed().toUpper();
    raw.replace(QChar(0x0412), QLatin1Char('W'));
    raw.replace(QChar(0x0432), QLatin1Char('W'));
    raw.remove(QRegularExpression(QStringLiteral("\\s+")));
    raw.remove(QRegularExpression(QStringLiteral("[-/]")));
    return raw;
}

PartMarkupPolicyService::PolicySnapshot PartMarkupPolicyService::loadPolicySnapshot()
{
    PolicySnapshot snapshot;
    snapshot.inventoryClientPrices = loadInventoryClientPrices(Database::db(), &snapshot.inventoryPurchasePrices);
    snapshot.storeMarkups = loadMarkupMapFromSetting(QStringLiteral("settings.fin.store_markups"));
    snapshot.oilRules = loadOilMarkupRules();
    snapshot.defaultPartsMarkupPercent = Database::getSetting(QStringLiteral("settings.fin.parts_markup"), QStringLiteral("0")).toDouble();
    snapshot.defaultOilMarkupPercent = Database::getSetting(QStringLiteral("settings.fin.oil_markup"), QStringLiteral("0")).toDouble();
    return snapshot;
}

PartMarkupPolicyService::PolicySnapshot PartMarkupPolicyService::loadPolicySnapshot(QSqlDatabase db, QString* error)
{
    PolicySnapshot snapshot;
    if(error)error->clear();
    snapshot.inventoryClientPrices = loadInventoryClientPrices(db, &snapshot.inventoryPurchasePrices,error);
    snapshot.storeMarkups = loadMarkupMapFromSetting(db, QStringLiteral("settings.fin.store_markups"),error);
    snapshot.oilRules = loadOilMarkupRules(db,error);
    snapshot.defaultPartsMarkupPercent = settingString(db, QStringLiteral("settings.fin.parts_markup"), QStringLiteral("0"),error).toDouble();
    snapshot.defaultOilMarkupPercent = settingString(db, QStringLiteral("settings.fin.oil_markup"), QStringLiteral("0"),error).toDouble();
    return snapshot;
}

double PartMarkupPolicyService::effectivePartMarkupPercent(const QString& partName, const QString& store)
{
    return effectivePartMarkupPercent(partName, store, loadPolicySnapshot());
}

double PartMarkupPolicyService::effectivePartMarkupPercent(const QString& partName, const QString& store, const PolicySnapshot& snapshot)
{
    const QString normalizedStore = store.trimmed();
    if (normalizedStore.isEmpty()) return 0.0;

    if (OilServiceClassifier::isMotorOilProductName(partName)) {
        const QString oilGrade = detectOilGradeFromText(partName);
        bool foundOil = false;
        const double oilStorePct = lookupOilMarkupPercent(snapshot.oilRules, normalizedStore, oilGrade, &foundOil);
        if (foundOil && oilStorePct > 0.0) return oilStorePct;

        bool foundPart = false;
        const double partStorePct = lookupStoreMarkupPercent(snapshot.storeMarkups, normalizedStore, &foundPart);
        if (foundPart) return partStorePct;
        return 0.0;
    }

    bool foundPart = false;
    const double storePct = lookupStoreMarkupPercent(snapshot.storeMarkups, normalizedStore, &foundPart);
    return foundPart ? storePct : 0.0;
}

QString PartMarkupPolicyService::effectiveOilBrand(const QString& partName, const QString& store)
{
    return effectiveOilBrand(partName, store, loadPolicySnapshot());
}

QString PartMarkupPolicyService::effectiveOilBrand(const QString& partName, const QString& store, const PolicySnapshot& snapshot)
{
    if (!OilServiceClassifier::isMotorOilProductName(partName)) return QString();

    const QString normalizedStore = store.trimmed();
    const QString oilGrade = detectOilGradeFromText(partName);
    const PartMarkupPolicyService::OilMarkupRule* rule = findBestOilRule(snapshot.oilRules, normalizedStore, oilGrade);
    return rule ? rule->brand.trimmed() : QString();
}

double PartMarkupPolicyService::applyMarkup(double purchasePrice, double pct)
{
    PartPricingInput input;
    input.purchasePrice = purchasePrice;
    input.qty = 1.0;
    input.manualMarkupOverride = true;
    input.manualMarkupPercent = pct;
    input.sourceType = QStringLiteral("policy_apply_markup");
    return PartPricingEngine::calculate(input, PartMarkupPolicyService::PolicySnapshot{}).clientPrice;
}

QStringList PartMarkupPolicyService::knownStoreNames()
{
    QStringList names;
    auto appendName = [&names](const QString& raw) {
        const QString name = raw.simplified().trimmed();
        if (!name.isEmpty() && !names.contains(name, Qt::CaseInsensitive)) names.append(name);
    };

    const auto storeMap = loadMarkupMapFromSetting(QStringLiteral("settings.fin.store_markups"));
    for (auto it = storeMap.begin(); it != storeMap.end(); ++it) appendName(it.key());

    const auto oilRules = loadOilMarkupRules();
    for (const auto& rule : oilRules) appendName(rule.store);

    std::sort(names.begin(), names.end(), [](const QString& a, const QString& b) {
        return QString::localeAwareCompare(a.toLower(), b.toLower()) < 0;
    });
    return names;
}
