#pragma once

#include <QSqlDatabase>
#include <QString>

class LegacyCanonicalImportNormalization
{
public:
    static bool finalizeCanonicalImportedRelations(QSqlDatabase database, QString* errorText = nullptr);
};
