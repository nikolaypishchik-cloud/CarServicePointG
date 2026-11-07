#pragma once
#include "core/util/DateFormats.h"

#include "runtime/AppRuntime.h"
#include "core/services/CatalogImportTextUtils.h"

#include <QCryptographicHash>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QTextStream>
#include <QVector>
#include <QStringConverter>


// P2 split: catalog import local helpers are preserved through ordered inline fragments.
#include "infrastructure/db/sqlite/internal/DbCatalogImportTypes.ipp"
#include "infrastructure/db/sqlite/internal/DbCatalogImportNormalize.ipp"
#include "infrastructure/db/sqlite/internal/DbCatalogImportHelpers.ipp"
