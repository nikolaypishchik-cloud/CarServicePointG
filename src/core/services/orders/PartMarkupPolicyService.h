#pragma once

#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

class QSqlDatabase;

class PartMarkupPolicyService {
public:
    struct OilMarkupRule {
        QString store;
        QString brand;
        QString oilGrade;
        double pct = 0.0;
    };

    struct PolicySnapshot {
        QMap<QString, double> inventoryClientPrices;
        QMap<QString, double> inventoryPurchasePrices;
        QMap<QString, double> storeMarkups;
        QList<OilMarkupRule> oilRules;
        double defaultPartsMarkupPercent = 0.0;
        double defaultOilMarkupPercent = 0.0;
    };

    static PolicySnapshot loadPolicySnapshot();
    static PolicySnapshot loadPolicySnapshot(QSqlDatabase db, QString* error = nullptr);
    static double effectivePartMarkupPercent(const QString& partName, const QString& store);
    static double effectivePartMarkupPercent(const QString& partName, const QString& store, const PolicySnapshot& snapshot);
    static QString effectiveOilBrand(const QString& partName, const QString& store);
    static QString effectiveOilBrand(const QString& partName, const QString& store, const PolicySnapshot& snapshot);
    static double applyMarkup(double purchasePrice, double pct);
    static QStringList knownStoreNames();

    static QString canonStoreKey(const QString& raw);
    static bool isInventoryStore(const QString& store);
    static QString customerPartsStore() { return QString::fromUtf8("З/Ч клієнта"); }
    static bool isCustomerSupplied(const QString& store) {
        return store.simplified().compare(customerPartsStore(), Qt::CaseInsensitive) == 0;
    }
    static QString normalizeOilGrade(QString raw);
};
