#pragma once

#include <QString>

namespace OilServiceClassifier {

enum class OilWorkKind {
    None,
    EngineOilService,
    PowerSteeringFluidService,
    AutomaticTransmissionFluidService,
    ManualTransmissionOilService,
    TransferCaseOilService,
    DifferentialOilService,
    OtherNonEngineOilService
};

QString normalizeOilTextForMatch(const QString& text);
OilWorkKind classifyOilWork(const QString& serviceTitle);
bool isEngineOilServiceTitle(const QString& serviceTitle);
bool isNonEngineOilServiceTitle(const QString& serviceTitle);
bool isMotorOilProductName(const QString& partName);
bool isEngineOilMaintenancePartName(const QString& partName);
bool matchesOilWorkKindPart(OilWorkKind kind, const QString& partName);

} // namespace OilServiceClassifier
