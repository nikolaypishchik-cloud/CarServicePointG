#pragma once

// P2 architecture module prelude.
// Generated during monolith-removal p2 so converted implementation slices
// compile as separate .cpp modules without being text-included by the owner.
// Owner: src/core/services/ReportsDataLoader.cpp

#include "core/services/ReportsDataLoader.h"
#include "infrastructure/db/sqlite/Db.h"
#include "infrastructure/db/sqlite/ScopedConnectionManager.h"
#include "core/util/FormatUtils.h"
#include "core/util/PlateUtils.h"
#include "core/util/OilServiceClassifier.h"
#include "core/services/ReportsDateUtils.h"
#include <QDate>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QRegularExpression>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QThread>
#include <algorithm>
