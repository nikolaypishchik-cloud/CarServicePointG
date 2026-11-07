#pragma once

#include <QSqlDatabase>
#include <QString>
#include <QVariantMap>
#include <QHash>

namespace CrmVehicleService {

struct PrimaryStatusResult {
    QHash<QString, bool> vehicles;
    QString error;
    bool succeeded() const { return error.isEmpty(); }
};
PrimaryStatusResult primaryStatuses(const QString& clientUid, QSqlDatabase db = {});

struct VehicleRecord {
    int id = 0;
    QString vehicleUid;
    QString clientUid;
    QString plate;
    QString vin;
    QString brand;
    QString model;
    int year = 0;
    QString engine;
    QString fuelType;
    int mileage = 0;
    QString color;
    QString notes;
    bool isPrimary = false;
};

bool findByPlateOrVin(QSqlDatabase db, const QString& plate, const QString& vin, VehicleRecord* out, QString* errorText = nullptr);
bool findByPlateOrVinForClient(QSqlDatabase db, const QString& clientUid, const QString& plate, const QString& vin, VehicleRecord* out, QString* errorText = nullptr);
bool findByClientAndCarText(QSqlDatabase db, const QString& clientUid, const QString& carText, int year, VehicleRecord* out, QString* errorText = nullptr);
bool updateMissingVehicleFields(QSqlDatabase db, const QString& vehicleUid, const VehicleRecord& input, QString* errorText = nullptr);
bool createVehicle(QSqlDatabase db, const VehicleRecord& input, QString* vehicleUid, QString* errorText = nullptr);
bool setPrimaryStatus(QSqlDatabase db,
                      const QString& clientUid,
                      const QString& vehicleUid,
                      bool primary,
                      QString* errorText = nullptr);

// Direct in-transaction helper for sync/import paths. Only allowlisted
// vehicle fields are written; missing fields are preserved on update.
bool upsertVehicleDirect(QSqlDatabase db,
                         const QString& vehicleUid,
                         const QVariantMap& fields,
                         QString* errorText = nullptr);
bool findOrCreateFromOrder(QSqlDatabase db,
                           const QString& clientUid,
                           const QString& plate,
                           const QString& vin,
                           const QString& carText,
                           int year,
                           int mileage,
                           const QString& engine,
                           const QString& fuelType,
                           const QString& color,
                           QString* vehicleUid,
                           QString* errorText = nullptr);

} // namespace CrmVehicleService
