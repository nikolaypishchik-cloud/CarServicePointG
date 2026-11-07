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

