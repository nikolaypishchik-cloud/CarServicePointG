// Extracted from src/infrastructure/db/sqlite/DbCatalogImportLocalSupport.h for P2 prelude monolith reduction.
// Included exactly once by the owning header; do not compile separately.

namespace {
struct CatalogSourceWorkRow
{
    int sourcePartId = 0;
    int orderNumber = 0;
    QString orderUid;
    QString sourceDbLayer;
    QString rawCar;
    QString vehicleBrand;
    QString vehicleModel;
    int carYear = 0;
    QString fuelType;
    QString engineType;
    QString bodyType;
    QString partNumber;
    QString oem;
    QString name;
    QString normalizedName;
    QString normalizedPartNumber;
    QString normalizedOemNumber;
    QString brandName;
    QString manufacturer;
    QString supplier;
    QString normalizedStore;
    QString note;
    QString unit;
    QString currency;
    QString orderedAt;
    QString deliveryDate;
    QString seenAt;
    double qty = 0.0;
    double price = 0.0;
    double amount = 0.0;
    double purchasePrice = 0.0;
    double purchaseAmount = 0.0;
    double markupPercent = 0.0;
    int paid = 0;
    int orderedByAdmin = 0;
    int serviceId = 0;
    QString groupName;
    QString kind;
    QString groupKey;
    QString nodeTags;
    QString catalogMatchKey;
    QString dedupeSignature;
    bool fromLegacyLike = false;
};

struct CatalogAggregateWorkRow
{
    QVector<CatalogSourceWorkRow> sources;
    QString dedupeMode;
    QString vehicleBrand;
    QString vehicleModel;
    int yearFrom = 0;
    int yearTo = 0;
    QString fuelType;
    QString engineType;
    QString bodyType;
    QString partNumber;
    QString oem;
    QString normalizedPartNumber;
    QString normalizedOemNumber;
    QString name;
    QString normalizedName;
    QString brandName;
    QString manufacturer;
    QString supplier;
    QString note;
    QString unit;
    QString currency;
    QString kind;
    QString groupKey;
    QString groupName;
    QString nodeTags;
    QString catalogMatchKey;
    QString dedupeSignature;
    QString sourceOrderUidFirst;
    QString sourceOrderUidLast;
    int sourceOrderNumberFirst = 0;
    int sourceOrderNumberLast = 0;
    QString sourceDbLayer;
    QString lastStore;
    QString orderedAt;
    QString firstSeenAt;
    QString lastSeenAt;
    double qtyLast = 0.0;
    double priceLast = 0.0;
    double amountLast = 0.0;
    double minPrice = 0.0;
    double maxPrice = 0.0;
    double avgPrice = 0.0;
    double priceSum = 0.0;
    int priceCount = 0;
    int usageCount = 0;
    int ordersCount = 0;
    int orderedByAdmin = 0;
    int paid = 0;
    int serviceId = 0;
    bool isFromLegacyImport = false;
    QSet<QString> seenOrders;
    QSet<QString> storesSeen;
};

struct ExistingCatalogRecord
{
    int id = 0;
    QString partNumber;
    QString oem;
    QString normalizedPartNumber;
    QString normalizedOemNumber;
    QString name;
    QString normalizedName;
    QString brandName;
    QString manufacturer;
    QString supplier;
    QString normalizedStore;
    QString catalogMatchKey;
    QString dedupeSignature;
    QString vehicleBrand;
    QString vehicleModel;
    int yearFrom = 0;
    int yearTo = 0;
    QString fuelType;
    QString engineType;
    QString bodyType;
    QString groupName;
    QString note;
    QString sourceFirst;
    QString sourceLast;
    QString firstImportedAt;
    QString lastImportedAt;
    double price = 0.0;
    double minPrice = 0.0;
    double maxPrice = 0.0;
    double avgPrice = 0.0;
    double lastPrice = 0.0;
    double qtyLast = 0.0;
    double amountLast = 0.0;
    int usageCount = 0;
    int ordersCount = 0;
    QString lastStore;
    int lastOrderNumber = 0;
    QString lastOrderUid;
    QString sourceDbLayer;
    bool manuallyEdited = false;
};

struct CatalogEligibilityResult
{
    bool eligible = false;
    QString reasonCode;
    QString reasonText;
    bool canImportAfterRelax = false;
    QString recommendedAction;
};

struct CatalogImportDecisionDetail
{
    int sourcePartId = 0;
    int orderNumber = 0;
    QString orderUid;
    QString sourceDbLayer;
    QString partNumber;
    QString oemNumber;
    QString name;
    QString supplier;
    QString note;
    QString normalizedPartNumber;
    QString normalizedOemNumber;
    QString normalizedName;
    QString normalizedStore;
    QString dedupeMode;
    QString catalogMatchKey;
    QString sourceSignature;
    QString decision;
    QString reasonCode;
    QString reasonText;
    QString recommendedAction;
    QString candidateClass;
    QString sqlOperation;
    QString sqlErrorText;
    QString bindSnapshot;
    bool canImportAfterRelax = false;
    QList<int> candidateCatalogIds;
};

struct CatalogDbOperationResult
{
    bool ok = false;
    QString errorText;
