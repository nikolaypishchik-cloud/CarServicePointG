#pragma once

#include <QString>

class QSqlDatabase;

class LegacyCanonicalAncillaryImportSupport
{
public:
    static bool finalizeImportedAncillaryData(QSqlDatabase database, QString* errorText = nullptr, bool preservePlanningKeys = false);
};
