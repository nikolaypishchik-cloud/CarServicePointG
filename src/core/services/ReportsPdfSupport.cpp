#include "core/services/ReportsPdfSupport.h"

#include "core/util/FormatUtils.h"
#include "core/util/PlateUtils.h"
#include "core/util/OilServiceClassifier.h"
#include "core/services/ReportsDateUtils.h"
#include "infrastructure/db/sqlite/Db.h"

#include <QDate>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QPair>
#include <QRegularExpression>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QtMath>
#include <algorithm>

