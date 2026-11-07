#pragma once

#include <QSqlDatabase>
#include <QString>
#include <QVariantMap>

namespace CrmClientService {

struct ClientRecord {
    int id = 0;
    QString clientUid;
    QString fullName;
    QString firstName;
    QString lastName;
    QString phone;
    QString phoneAlt;
    QString email;
    QString source;
    QString status = QStringLiteral("active");
    QString loyaltyLevel;
    double discountPercent = 0.0;
    QString notes;
};

struct BackfillResult {
    int clientsCreated = 0;
    int clientsUpdated = 0;
    int vehiclesCreated = 0;
    int vehiclesUpdated = 0;
    int ordersLinked = 0;
    int eventsCreated = 0;
    int skipped = 0;
};

bool findByPhone(QSqlDatabase db, const QString& phone, ClientRecord* out, QString* errorText = nullptr);
bool findByNormalizedPhone(QSqlDatabase db, const QString& phone, ClientRecord* out, QString* errorText = nullptr);
bool findByNormalizedName(QSqlDatabase db, const QString& fullName, ClientRecord* out, QString* errorText = nullptr);
bool updateMissingClientFields(QSqlDatabase db, const QString& clientUid, const ClientRecord& input, QString* errorText = nullptr);
bool findByUid(QSqlDatabase db, const QString& clientUid, ClientRecord* out, QString* errorText = nullptr);
bool createClient(QSqlDatabase db, const ClientRecord& input, QString* clientUid, QString* errorText = nullptr);

// Direct in-transaction helper for sync/import paths that already run under
// MutationExecutor or a schema migration. Only allowlisted CRM fields are
// written; missing fields are preserved on update.
bool upsertClientDirect(QSqlDatabase db,
                        const QString& clientUid,
                        const QVariantMap& fields,
                        QString* errorText = nullptr);
bool findOrCreateFromOrder(QSqlDatabase db,
                           const QString& fullName,
                           const QString& phone,
                           QString* clientUid,
                           QString* errorText = nullptr);
bool ensureLocalSchema(QSqlDatabase db, QString* errorText = nullptr);
bool ensureFromWorkOrder(QSqlDatabase db, int orderNumber, QString* clientUid = nullptr, QString* vehicleUid = nullptr, QString* errorText = nullptr);
bool backfillWorkOrders(QSqlDatabase db, QString* errorText = nullptr);
bool backfillExistingData(QSqlDatabase db, BackfillResult* result = nullptr, QString* errorText = nullptr);

} // namespace CrmClientService
