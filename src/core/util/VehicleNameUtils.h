#pragma once
#include <QString>
#include <QStringList>

namespace VehicleNameUtils {
QStringList uniqueModelOptions(const QStringList& values);
QString normalizeVehicleFreeText(const QString& input);
QString normalizeVehicleFreeTextProgressive(const QString& input);
QString normalizeVehicleModel(const QString& brand, const QString& model);
}
