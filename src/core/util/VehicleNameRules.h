#pragma once

#include <QPair>
#include <QString>
#include <QStringList>
#include <QVector>

namespace VehicleNameRules {
using Rule = QPair<QString, QString>;

QVector<Rule> makeRules();
QVector<Rule> modelRules();
QVector<Rule> techRules();
QString normalizeBrandSpecificModels(QString text);
QString normalizeModelHints(QString text);
QStringList canonicalBrands();

} // namespace VehicleNameRules
