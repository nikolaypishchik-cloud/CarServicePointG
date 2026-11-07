#pragma once
#include <QSqlDatabase>
#include <QString>
#include <QVariantMap>
#include <QVector>

namespace VehicleIntakeService {
struct VehiclePrefill {
    bool found = false;
    QString source;
    QString orderUid;
    QString clientUid;
    QString clientName;
    QString clientPhone;
    QString vehicleUid;
    QString vehicleLabel;
    QString plate;
    QString vin;
    QString year;
    QString engineType;
    QString gearbox;
    QString mileage;
    QString fuelType;
    QString masterName;
};

// Read models contain values only; no SQL connection escapes to the UI.
struct IntakeRecord {
    QString uid, orderUid, clientUid, vehicleUid, mileage, fuelLevel;
    QString signaturePath, checklistJson, pdfPath;
};
bool listIntakes(QVector<IntakeRecord>* result, QString* errorText = nullptr);
bool loadIntake(const QString& uid, IntakeRecord* result, QString* errorText = nullptr);
bool lookupVehicleByPlate(const QString& plate, VehiclePrefill* out, QString* errorText = nullptr);
bool lookupOrderForIntake(const QString& orderKey, VehiclePrefill* out, QString* errorText = nullptr);
bool saveIntake(const QVariantMap& payload, QString* intakeUid, QString* errorText = nullptr);
bool updateSignaturePath(const QString& intakeUid, const QString& signaturePath, QString* errorText = nullptr);
bool addPhoto(const QString& intakeUid, const QString& orderUid, const QString& localPath, const QString& photoType, QString* photoUid = nullptr, QString* errorText = nullptr);
bool generatePdf(const QString& intakeUid, const QString& outputPath, QString* errorText = nullptr);
QVariantMap vehiclePrefillToMap(const VehiclePrefill& prefill);
bool lookupVehicleByPlate(QSqlDatabase db, const QString& plate, VehiclePrefill* out, QString* errorText = nullptr);
bool lookupOrderForIntake(QSqlDatabase db, const QString& orderKey, VehiclePrefill* out, QString* errorText = nullptr);
bool saveIntake(QSqlDatabase db, const QVariantMap& payload, QString* intakeUid, QString* errorText = nullptr);
bool updateSignaturePath(QSqlDatabase db, const QString& intakeUid, const QString& signaturePath, QString* errorText = nullptr);
bool addPhoto(QSqlDatabase db, const QString& intakeUid, const QString& orderUid, const QString& localPath, const QString& photoType, QString* photoUid = nullptr, QString* errorText = nullptr);
bool generatePdf(QSqlDatabase db, const QString& intakeUid, const QString& outputPath, QString* errorText = nullptr);
QString blankActPdfPath(QString* errorText = nullptr);
bool createBlankIntakeActPdf(const QString& outputPath, QString* errorText = nullptr);
}
