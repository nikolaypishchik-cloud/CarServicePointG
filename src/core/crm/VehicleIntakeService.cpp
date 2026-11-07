#include "infrastructure/db/sqlite/Db.h"
#include "core/util/DateFormats.h"
#include "core/crm/VehicleIntakeService.h"
#include "core/cloudsync/EntityUidService.h"
#include "core/crm/CrmVehicleService.h"
#include "core/services/MutationExecutor.h"
#include "core/services/settings/GeneralSettingsService.h"
#include "core/util/PlateUtils.h"
#include "core/util/VehicleNameUtils.h"
#include "runtime/AppRuntime.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPageSize>
#include <QPdfWriter>
#include <QPainter>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QVariant>
#include <QTextOption>

namespace {

bool execPrepared(QSqlQuery& q, QString* errorText)
{
    if (q.exec()) return true;
    if (errorText) *errorText = q.lastError().text();
    return false;
}

QString v(const QVariantMap& m, const QString& k)
{
    const QString text = m.value(k).toString().trimmed();
    // Optional text must bind as empty TEXT, not SQL NULL (including pdf_path).
    return text.isNull() ? QStringLiteral("") : text;
}

void drawKeyValue(QPainter& painter, int x, int& y, const QString& key, const QString& value)
{
    painter.drawText(x, y, QString::fromUtf8("%1: %2").arg(key, value));
    y += 30;
}

void drawImageFit(QPainter& painter, const QString& path, const QRect& target, const QString& caption)
{
    QImage image(path);
    painter.drawRect(target);
    painter.drawText(target.adjusted(8, 6, -8, -6), Qt::AlignTop | Qt::AlignLeft, caption);
    if (image.isNull()) {
        painter.drawText(target, Qt::AlignCenter, QString::fromUtf8("файл не знайдено / не зображення"));
        return;
    }
    const QRect imageRect = target.adjusted(10, 28, -10, -10);
    const QImage scaled = image.scaled(imageRect.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    const QPoint topLeft(imageRect.x() + (imageRect.width() - scaled.width()) / 2,
                         imageRect.y() + (imageRect.height() - scaled.height()) / 2);
    painter.drawImage(topLeft, scaled);
}


bool tableExists(QSqlDatabase db, const QString& table)
{
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT 1 FROM sqlite_master WHERE type='table' AND name=? LIMIT 1"));
    q.addBindValue(table);
    return q.exec() && q.next();
}

bool columnExists(QSqlDatabase db, const QString& table, const QString& column)
{
    QSqlQuery q(db);
    q.exec(QStringLiteral("PRAGMA table_info(%1)").arg(table));
    while (q.next()) {
        if (q.value(1).toString().compare(column, Qt::CaseInsensitive) == 0) return true;
    }
    return false;
}

bool ensureColumn(QSqlDatabase db, const QString& table, const QString& column, const QString& ddl, QString* errorText)
{
    if (!tableExists(db, table)) return true;
    if (columnExists(db, table, column)) return true;
    QSqlQuery q(db);
    if (q.exec(QStringLiteral("ALTER TABLE %1 ADD COLUMN %2;").arg(table, ddl))) return true;
    if (errorText) *errorText = q.lastError().text();
    return false;
}

bool ensureVehicleIntakeActColumns(QSqlDatabase db, QString* errorText)
{
    if (!ensureColumn(db, QStringLiteral("vehicle_intake"), QStringLiteral("act_number"), QStringLiteral("act_number TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("vehicle_intake"), QStringLiteral("act_date"), QStringLiteral("act_date TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    if (!ensureColumn(db, QStringLiteral("vehicle_intake"), QStringLiteral("accepted_at"), QStringLiteral("accepted_at TEXT NOT NULL DEFAULT ''"), errorText)) return false;
    return true;
}

QString fileSha256(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return QString();
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) hash.addData(file.read(1024 * 1024));
    return QString::fromLatin1(hash.result().toHex());
}

bool ensureLocalFileAttachment(QSqlDatabase db,
                               const QString& fileUid,
                               const QString& entityType,
                               const QString& entityUid,
                               const QString& localPath,
                               const QString& mimeType,
                               QString* errorText)
{
    if (fileUid.trimmed().isEmpty() || localPath.trimmed().isEmpty()) return true;
    if (!tableExists(db, QStringLiteral("file_attachments"))) return true;
    QFileInfo info(localPath);
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT INTO file_attachments(file_uid,entity_type,entity_uid,local_path,mime_type,file_name,sha256,size_bytes,sync_status) "
        "VALUES(?,?,?,?,?,?,?,?,?) "
        "ON CONFLICT(file_uid) DO UPDATE SET entity_type=excluded.entity_type, entity_uid=excluded.entity_uid, "
        "local_path=excluded.local_path, mime_type=excluded.mime_type, file_name=excluded.file_name, "
        "sha256=excluded.sha256, size_bytes=excluded.size_bytes, sync_status='pending'"));
    q.addBindValue(fileUid);
    q.addBindValue(entityType);
    q.addBindValue(entityUid);
    q.addBindValue(localPath);
    q.addBindValue(mimeType);
    q.addBindValue(info.fileName());
    q.addBindValue(fileSha256(localPath));
    q.addBindValue(info.exists() ? info.size() : 0);
    q.addBindValue(QStringLiteral("pending"));
    return execPrepared(q, errorText);
}

QJsonObject jsonObjectFromString(const QString& json)
{
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    return doc.isObject() ? doc.object() : QJsonObject{};
}

QString jstr(const QJsonObject& object, const QString& key)
{
    const QString text = object.value(key).toString().trimmed();
    return text.isNull() ? QStringLiteral("") : text;
}

bool jbool(const QJsonObject& object, const QString& key, bool defaultValue = false)
{
    return object.contains(key) ? object.value(key).toBool() : defaultValue;
}

QJsonObject jobj(const QJsonObject& object, const QString& key)
{
    const QJsonValue value = object.value(key);
    return value.isObject() ? value.toObject() : QJsonObject{};
}

QString displayDate(const QString& iso)
{
    const QDate date = DateFormats::parseDate(iso);
    return date.isValid() ? date.toString(QStringLiteral("dd.MM.yyyy")) : iso;
}

QString displayDateTime(const QString& iso)
{
    const QDateTime dt = DateFormats::parseDateTime(iso);
    return dt.isValid() ? dt.toString(QStringLiteral("dd.MM.yyyy HH:mm")) : iso;
}

QString displayTime(const QString& iso)
{
    const QDateTime dt = DateFormats::parseDateTime(iso);
    return dt.isValid() ? dt.time().toString(QStringLiteral("HH:mm")) : QString();
}

void drawWrapped(QPainter& painter, const QRect& rect, const QString& text, int flags = Qt::AlignLeft | Qt::AlignTop)
{
    QTextOption option;
    option.setWrapMode(QTextOption::WordWrap);
    option.setAlignment(Qt::Alignment(flags));
    painter.drawText(rect, text, option);
}

QPen thinPen(const QColor& color = Qt::black)
{
    QPen pen(color);
    pen.setWidthF(0.6);
    pen.setCapStyle(Qt::SquareCap);
    pen.setJoinStyle(Qt::MiterJoin);
    return pen;
}

QPen accentPen()
{
    QPen pen(QColor(172, 22, 22));
    pen.setWidthF(0.8);
    pen.setCapStyle(Qt::SquareCap);
    return pen;
}

QString pointGLogoPath()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QDir(appDir).filePath(QStringLiteral("resources/assets/logo.jpg")),
        QDir(appDir).filePath(QStringLiteral("../resources/assets/logo.jpg")),
        QDir::current().filePath(QStringLiteral("resources/assets/logo.jpg")),
        QStringLiteral("X:/CS_PG_v2_1_1/resources/assets/logo.jpg")
    };
    for (const QString& candidate : candidates) {
        if (QFileInfo::exists(candidate)) return candidate;
    }
    return QString();
}

void setBodyFont(QPainter& painter, int size = 8, bool bold = false)
{
    QFont f(QStringLiteral("Arial"), size);
    f.setBold(bold);
    painter.setFont(f);
}

void drawSectionTitle(QPainter& painter, int x, int& y, int w, const QString& title)
{
    painter.setPen(accentPen());
    painter.drawLine(x, y, x + w, y);
    y += 8;
    setBodyFont(painter, 8, true);
    painter.setPen(thinPen(Qt::black));
    painter.drawText(QRect(x, y, w, 24), Qt::AlignLeft | Qt::AlignVCenter, title);
    y += 28;
    setBodyFont(painter, 8);
}

void drawBox(QPainter& painter, const QRect& rect, const QString& title, const QString& value)
{
    painter.setPen(thinPen());
    painter.drawRect(rect);
    setBodyFont(painter, 8, true);
    painter.drawText(rect.adjusted(6, 4, -6, -4), Qt::AlignLeft | Qt::AlignTop, title);
    setBodyFont(painter, 8);
    drawWrapped(painter, rect.adjusted(6, 24, -6, -6), value);
}

void drawCheck(QPainter& painter, int x, int y, const QString& label, bool checked)
{
    painter.setPen(thinPen());
    painter.drawRect(x, y - 10, 10, 10);
    if (checked) {
        painter.drawLine(x + 2, y - 5, x + 4, y - 2);
        painter.drawLine(x + 4, y - 2, x + 9, y - 10);
    }
    painter.drawText(x + 15, y, label);
}

void drawKeyValueBox(QPainter& painter, const QRect& rect, const QString& key, const QString& value)
{
    painter.setPen(thinPen());
    painter.drawRect(rect);
    setBodyFont(painter, 7, true);
    painter.drawText(rect.adjusted(5, 3, -5, -3), Qt::AlignLeft | Qt::AlignTop, key);
    setBodyFont(painter, 8);
    drawWrapped(painter, rect.adjusted(5, 20, -5, -3), value);
}

void drawHorizontalValue(QPainter& painter, const QRect& rect, const QString& key, const QString& value)
{
    painter.setPen(thinPen());
    painter.drawRect(rect);
    setBodyFont(painter, 7, true);
    painter.drawText(rect.adjusted(6, 0, -6, 0), Qt::AlignLeft | Qt::AlignVCenter, key);
    setBodyFont(painter, 8);
    drawWrapped(painter, rect.adjusted(108, 5, -6, -4), value, Qt::AlignLeft | Qt::AlignVCenter);
}

void drawPageFooter(QPainter& painter, int page, int pages, int x = 58, int w = 680)
{
    painter.setPen(accentPen());
    painter.drawLine(x, 1034, x + w, 1034);
    painter.setPen(thinPen(Qt::black));
    setBodyFont(painter, 8);
    painter.drawText(QRect(x, 1040, w, 22), Qt::AlignRight, QString::fromUtf8("Сторінка %1 з %2").arg(page).arg(pages));
}

int drawPdfHeader(QPainter& painter,
                  int x,
                  int y,
                  int w,
                  const GeneralSettingsService::PdfServiceRequisitesPayload& serviceRequisites)
{
    const int headerH = 104;
    const int logoW = 252;
    const int separatorX = x + logoW + 10;
    const QString logoPath = pointGLogoPath();
    const QImage logo(logoPath);
    if (!logo.isNull()) {
        const QRect logoRect(x, y + 6, logoW - 14, 70);
        const QImage scaled = logo.scaled(logoRect.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        const QPoint topLeft(logoRect.x(), logoRect.y() + (logoRect.height() - scaled.height()) / 2);
        painter.drawImage(topLeft, scaled);
    } else {
        setBodyFont(painter, 17, true);
        painter.setPen(thinPen(Qt::black));
        painter.drawText(QRect(x, y + 12, logoW - 14, 44), Qt::AlignLeft | Qt::AlignVCenter, serviceRequisites.name);
    }

    painter.setPen(accentPen());
    painter.drawLine(separatorX, y + 8, separatorX, y + 82);

    setBodyFont(painter, 7);
    painter.setPen(thinPen(Qt::black));
    const QString requisites = QString::fromUtf8(
        "%1\n"
        "Юридична адреса: %2\n"
        "Фактична адреса: %3\n"
        "Телефон: %4    Email: %5\n"
        "Сайт: %6    Код ЄДРПОУ: %7\n"
        "р/р: %8")
        .arg(serviceRequisites.name,
             serviceRequisites.legalAddress,
             serviceRequisites.actualAddress,
             serviceRequisites.phone,
             serviceRequisites.email,
             serviceRequisites.website,
             serviceRequisites.edrpou,
             serviceRequisites.bankAccount);
    drawWrapped(painter, QRect(separatorX + 18, y + 4, w - logoW - 28, 84), requisites);

    painter.setPen(accentPen());
    painter.drawLine(x, y + 92, x + w, y + 92);
    return y + headerH;
}

QString conditionLabel(const QJsonObject& zone)
{
    return jstr(zone, QStringLiteral("status")) == QStringLiteral("issues")
        ? QString::fromUtf8("є зауваження")
        : QString::fromUtf8("без зауважень");
}

QString pdfValue(const QString& value, bool blank)
{
    return blank ? QString() : value.trimmed();
}

} // namespace

namespace VehicleIntakeService {

namespace {

QString normalizedPlateKey(const QString& plate)
{
    return PlateUtils::normalizePlate(plate).trimmed().toUpper();
}

QString vehicleLabelFromParts(const QString& brand, const QString& model)
{
    return VehicleNameUtils::normalizeVehicleFreeText(QStringLiteral("%1 %2").arg(brand.trimmed(), model.trimmed()).trimmed());
}

QString tableColumnOrEmpty(QSqlDatabase db, const QString& table, const QString& column)
{
    return columnExists(db, table, column) ? QStringLiteral("COALESCE(%1,'')").arg(column) : QStringLiteral("''");
}

QString tableIntColumnOrZero(QSqlDatabase db, const QString& table, const QString& column)
{
    return columnExists(db, table, column) ? QStringLiteral("COALESCE(%1,0)").arg(column) : QStringLiteral("0");
}

void fillIfEmpty(QString* target, const QString& value)
{
    if (target && target->trimmed().isEmpty() && !value.trimmed().isEmpty()) {
        *target = value.trimmed();
    }
}

void overwriteIfPositiveMileage(VehiclePrefill* out, const QString& mileage)
{
    if (!out) return;
    bool ok = false;
    const int value = mileage.trimmed().toInt(&ok);
    if (ok && value > 0) {
        out->mileage = QString::number(value);
    }
}

void applyCrmVehicle(const CrmVehicleService::VehicleRecord& vehicle, VehiclePrefill* out)
{
    if (!out) return;
    out->found = true;
    if (out->source.isEmpty()) out->source = QStringLiteral("crm_vehicles");
    fillIfEmpty(&out->vehicleUid, vehicle.vehicleUid);
    fillIfEmpty(&out->clientUid, vehicle.clientUid);
    fillIfEmpty(&out->plate, PlateUtils::formatPlateDisplay(normalizedPlateKey(vehicle.plate)));
    fillIfEmpty(&out->vin, vehicle.vin.toUpper());
    fillIfEmpty(&out->vehicleLabel, vehicleLabelFromParts(vehicle.brand, vehicle.model));
    if (out->year.isEmpty() && vehicle.year > 0) out->year = QString::number(vehicle.year);
    fillIfEmpty(&out->engineType, vehicle.engine);
    fillIfEmpty(&out->fuelType, vehicle.fuelType);
    if (vehicle.mileage > 0) out->mileage = QString::number(vehicle.mileage);
}

void enrichClient(QSqlDatabase db, VehiclePrefill* out)
{
    if (!out || out->clientUid.trimmed().isEmpty() || !tableExists(db, QStringLiteral("crm_clients"))) return;
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT COALESCE(full_name,''), COALESCE(phone,'') FROM crm_clients WHERE client_uid=? LIMIT 1"));
    q.addBindValue(out->clientUid.trimmed());
    if (q.exec() && q.next()) {
        fillIfEmpty(&out->clientName, q.value(0).toString());
        fillIfEmpty(&out->clientPhone, q.value(1).toString());
    }
}

bool queryLatestOrderByWhere(QSqlDatabase db,
                             const QString& table,
                             const QString& whereSql,
                             const QList<QVariant>& binds,
                             VehiclePrefill* out,
                             QString* errorText)
{
    if (!tableExists(db, table)) return false;
    const QString orderUidExpr = tableColumnOrEmpty(db, table, QStringLiteral("order_uid"));
    const QString orderNumberExpr = tableIntColumnOrZero(db, table, QStringLiteral("order_number"));
    const QString numberExpr = tableIntColumnOrZero(db, table, QStringLiteral("number"));
    const QString clientUidExpr = tableColumnOrEmpty(db, table, QStringLiteral("client_uid"));
    const QString vehicleUidExpr = tableColumnOrEmpty(db, table, QStringLiteral("vehicle_uid"));
    const QString clientExpr = tableColumnOrEmpty(db, table, QStringLiteral("client"));
    const QString phoneExpr = tableColumnOrEmpty(db, table, QStringLiteral("client_phone"));
    const QString carExpr = tableColumnOrEmpty(db, table, QStringLiteral("car"));
    const QString plateExpr = tableColumnOrEmpty(db, table, QStringLiteral("plate"));
    const QString vinExpr = tableColumnOrEmpty(db, table, QStringLiteral("vin"));
    const QString yearExpr = tableIntColumnOrZero(db, table, QStringLiteral("car_year"));
    const QString odometerExpr = tableIntColumnOrZero(db, table, QStringLiteral("odometer"));
    const QString engineTypeExpr = tableColumnOrEmpty(db, table, QStringLiteral("engine_type"));
    const QString engineVolumeExpr = tableColumnOrEmpty(db, table, QStringLiteral("engine_volume"));
    const QString fuelExpr = tableColumnOrEmpty(db, table, QStringLiteral("fuel_type"));
    const QString masterNameExpr = columnExists(db, table, QStringLiteral("master_name_closed"))
        ? QStringLiteral("COALESCE(master_name_closed,'')")
        : (columnExists(db, table, QStringLiteral("master_id")) && tableExists(db, QStringLiteral("masters"))
            ? QStringLiteral("COALESCE((SELECT name FROM masters WHERE id=%1.master_id),'')").arg(table)
            : QStringLiteral("''"));
    const QString openedExpr = tableColumnOrEmpty(db, table, QStringLiteral("opened_at"));
    const QString closedExpr = tableColumnOrEmpty(db, table, QStringLiteral("closed_at"));

    const QStringList selectExpressions = {
        orderUidExpr, orderNumberExpr, numberExpr, clientUidExpr, vehicleUidExpr,
        clientExpr, phoneExpr, carExpr, plateExpr, vinExpr, yearExpr, odometerExpr,
        engineTypeExpr, engineVolumeExpr, fuelExpr, masterNameExpr
    };
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT %1 FROM %2 WHERE %3 "
        "ORDER BY COALESCE(NULLIF(%4,''), NULLIF(%5,''), '') DESC, %6 DESC LIMIT 1")
        .arg(selectExpressions.join(QLatin1Char(',')), table, whereSql, closedExpr, openedExpr, numberExpr));
    for (const QVariant& bind : binds) q.addBindValue(bind);
    if (!q.exec()) {
        if (errorText) *errorText = q.lastError().text();
        return false;
    }
    if (!q.next()) return false;

    if (!out) return true;
    out->found = true;
    if (out->source.isEmpty()) out->source = QStringLiteral("work_orders");
    const QString orderUid = q.value(0).toString().trimmed();
    const int orderNumber = q.value(1).toInt() > 0 ? q.value(1).toInt() : q.value(2).toInt();
    fillIfEmpty(&out->orderUid, orderUid.isEmpty() && orderNumber > 0 ? QString::number(orderNumber) : orderUid);
    fillIfEmpty(&out->clientUid, q.value(3).toString());
    fillIfEmpty(&out->vehicleUid, q.value(4).toString());
    fillIfEmpty(&out->clientName, q.value(5).toString());
    fillIfEmpty(&out->clientPhone, q.value(6).toString());
    fillIfEmpty(&out->vehicleLabel, VehicleNameUtils::normalizeVehicleFreeText(q.value(7).toString()));
    fillIfEmpty(&out->plate, PlateUtils::formatPlateDisplay(normalizedPlateKey(q.value(8).toString())));
    fillIfEmpty(&out->vin, q.value(9).toString().toUpper());
    if (out->year.isEmpty() && q.value(10).toInt() > 0) out->year = q.value(10).toString();
    fillIfEmpty(&out->engineType, q.value(12).toString().trimmed().isEmpty() ? q.value(13).toString() : q.value(12).toString());
    fillIfEmpty(&out->fuelType, q.value(14).toString());
    fillIfEmpty(&out->masterName, q.value(15).toString());
    overwriteIfPositiveMileage(out, q.value(11).toString());
    return true;
}

bool queryLatestOrderByPlate(QSqlDatabase db, const QString& plate, VehiclePrefill* out, QString* errorText)
{
    const QString plateNorm = normalizedPlateKey(plate);
    if (plateNorm.isEmpty()) return false;
    QString lastError;
    for (const QString& table : {QStringLiteral("work_orders"), QStringLiteral("router_work_orders")}) {
        QStringList clauses;
        QList<QVariant> binds;
        if (columnExists(db, table, QStringLiteral("plate_norm"))) {
            clauses << QStringLiteral("UPPER(REPLACE(REPLACE(REPLACE(COALESCE(plate_norm,''),' ',''),'-',''),'_',''))=?");
            binds << plateNorm;
        }
        if (columnExists(db, table, QStringLiteral("plate"))) {
            clauses << QStringLiteral("UPPER(REPLACE(REPLACE(REPLACE(COALESCE(plate,''),' ',''),'-',''),'_',''))=?");
            binds << plateNorm;
        }
        if (clauses.isEmpty()) continue;
        QString err;
        if (queryLatestOrderByWhere(db, table, clauses.join(QStringLiteral(" OR ")), binds, out, &err)) return true;
        if (!err.trimmed().isEmpty()) lastError = err;
    }
    if (errorText && !lastError.isEmpty()) *errorText = lastError;
    return false;
}

bool queryOrderByKey(QSqlDatabase db, const QString& key, VehiclePrefill* out, QString* errorText)
{
    const QString trimmed = key.trimmed();
    if (trimmed.isEmpty()) return false;
    bool numberOk = false;
    const int number = trimmed.toInt(&numberOk);
    QString lastError;
    for (const QString& table : {QStringLiteral("work_orders"), QStringLiteral("router_work_orders")}) {
        QStringList clauses;
        QList<QVariant> binds;
        if (columnExists(db, table, QStringLiteral("order_uid"))) {
            clauses << QStringLiteral("COALESCE(order_uid,'')=?");
            binds << trimmed;
        }
        if (columnExists(db, table, QStringLiteral("order_number"))) {
            clauses << QStringLiteral("COALESCE(order_number,0)=?");
            binds << (numberOk ? number : -1);
        }
        if (columnExists(db, table, QStringLiteral("number"))) {
            clauses << QStringLiteral("COALESCE(number,0)=?");
            binds << (numberOk ? number : -1);
        }
        if (clauses.isEmpty()) continue;
        QString err;
        if (queryLatestOrderByWhere(db, table, clauses.join(QStringLiteral(" OR ")), binds, out, &err)) return true;
        if (!err.trimmed().isEmpty()) lastError = err;
    }
    if (errorText && !lastError.isEmpty()) *errorText = lastError;
    return false;
}

bool queryLatestIntakeByPlate(QSqlDatabase db, const QString& plate, VehiclePrefill* out, QString* errorText)
{
    if (!tableExists(db, QStringLiteral("vehicle_intake"))) return false;
    const QString plateNorm = normalizedPlateKey(plate);
    if (plateNorm.isEmpty()) return false;
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT order_uid,client_uid,vehicle_uid,mileage,fuel_level,checklist_json "
        "FROM vehicle_intake ORDER BY created_at DESC LIMIT 500"));
    if (!q.exec()) {
        if (errorText) *errorText = q.lastError().text();
        return false;
    }
    while (q.next()) {
        const QJsonObject act = jsonObjectFromString(q.value(5).toString());
        if (normalizedPlateKey(jstr(act, QStringLiteral("plate"))) != plateNorm) continue;
        if (!out) return true;
        out->found = true;
        if (out->source.isEmpty()) out->source = QStringLiteral("vehicle_intake");
        fillIfEmpty(&out->orderUid, q.value(0).toString());
        fillIfEmpty(&out->clientUid, q.value(1).toString());
        fillIfEmpty(&out->vehicleUid, q.value(2).toString());
        fillIfEmpty(&out->clientName, jstr(act, QStringLiteral("client_name")));
        fillIfEmpty(&out->clientPhone, jstr(act, QStringLiteral("client_phone")));
        fillIfEmpty(&out->vehicleLabel, VehicleNameUtils::normalizeVehicleFreeText(jstr(act, QStringLiteral("vehicle_label"))));
        fillIfEmpty(&out->plate, PlateUtils::formatPlateDisplay(plateNorm));
        fillIfEmpty(&out->vin, jstr(act, QStringLiteral("vin")).toUpper());
        fillIfEmpty(&out->year, jstr(act, QStringLiteral("year")));
        fillIfEmpty(&out->engineType, jstr(act, QStringLiteral("engine_type")));
        fillIfEmpty(&out->gearbox, jstr(act, QStringLiteral("gearbox")));
        fillIfEmpty(&out->fuelType, jstr(act, QStringLiteral("fuel_type")));
        fillIfEmpty(&out->mileage, jstr(act, QStringLiteral("mileage")).isEmpty() ? q.value(3).toString() : jstr(act, QStringLiteral("mileage")));
        return true;
    }
    return false;
}

} // namespace

QVariantMap vehiclePrefillToMap(const VehiclePrefill& prefill)
{
    QVariantMap m;
    m.insert(QStringLiteral("found"), prefill.found);
    m.insert(QStringLiteral("source"), prefill.source);
    m.insert(QStringLiteral("order_uid"), prefill.orderUid);
    m.insert(QStringLiteral("client_uid"), prefill.clientUid);
    m.insert(QStringLiteral("client_name"), prefill.clientName);
    m.insert(QStringLiteral("client_phone"), prefill.clientPhone);
    m.insert(QStringLiteral("vehicle_uid"), prefill.vehicleUid);
    m.insert(QStringLiteral("vehicle_label"), prefill.vehicleLabel);
    m.insert(QStringLiteral("plate"), prefill.plate);
    m.insert(QStringLiteral("vin"), prefill.vin);
    m.insert(QStringLiteral("year"), prefill.year);
    m.insert(QStringLiteral("engine_type"), prefill.engineType);
    m.insert(QStringLiteral("gearbox"), prefill.gearbox);
    m.insert(QStringLiteral("mileage"), prefill.mileage);
    m.insert(QStringLiteral("fuel_type"), prefill.fuelType);
    m.insert(QStringLiteral("master_name"), prefill.masterName);
    return m;
}

bool lookupVehicleByPlate(QSqlDatabase db, const QString& plate, VehiclePrefill* out, QString* errorText)
{
    if (out) *out = VehiclePrefill{};
    const QString plateNorm = normalizedPlateKey(plate);
    if (plateNorm.isEmpty()) {
        if (errorText) *errorText = QStringLiteral("Plate is empty");
        return false;
    }

    VehiclePrefill result;
    CrmVehicleService::VehicleRecord crmVehicle;
    if (CrmVehicleService::findByPlateOrVin(db, plateNorm, QString(), &crmVehicle, nullptr)) {
        applyCrmVehicle(crmVehicle, &result);
        enrichClient(db, &result);
    }
    queryLatestOrderByPlate(db, plateNorm, &result, nullptr);
    queryLatestIntakeByPlate(db, plateNorm, &result, nullptr);
    enrichClient(db, &result);

    if (!result.found) {
        if (errorText) *errorText = QString::fromUtf8("Авто не знайдено локально.");
        return false;
    }
    if (result.plate.trimmed().isEmpty()) result.plate = PlateUtils::formatPlateDisplay(plateNorm);
    if (out) *out = result;
    return true;
}

bool lookupOrderForIntake(QSqlDatabase db, const QString& orderKey, VehiclePrefill* out, QString* errorText)
{
    if (out) *out = VehiclePrefill{};
    VehiclePrefill result;
    if (!queryOrderByKey(db, orderKey, &result, errorText)) return false;
    if (!result.plate.trimmed().isEmpty()) {
        VehiclePrefill enriched = result;
        queryLatestIntakeByPlate(db, result.plate, &enriched, nullptr);
        result = enriched;
    }
    enrichClient(db, &result);
    if (out) *out = result;
    return true;
}

bool saveIntakeDirect(QSqlDatabase db, const QVariantMap& p, const QString& preferredUid, QString* outUid, QString* errorText);
bool updateSignaturePathDirect(QSqlDatabase db, const QString& intakeUid, const QString& signaturePath, QString* errorText);
bool addPhotoDirect(QSqlDatabase db, const QString& intakeUid, const QString& orderUid, const QString& localPath, const QString& photoType, const QString& preferredPhotoUid, QString* photoUid, QString* errorText);
bool generatePdfDirect(QSqlDatabase db, const QString& intakeUid, const QString& outputPath, QString* errorText);

bool saveIntakeDirect(QSqlDatabase db, const QVariantMap& p, const QString& preferredUid, QString* outUid, QString* errorText)
{
    if (!ensureVehicleIntakeActColumns(db, errorText)) return false;
    const QString uid = !preferredUid.trimmed().isEmpty() ? preferredUid.trimmed() : (v(p, QStringLiteral("intake_uid")).isEmpty() ? EntityUidService::generateUid(QStringLiteral("intake")) : v(p, QStringLiteral("intake_uid")));
    const QString checklistJson = v(p, QStringLiteral("checklist_json")).isEmpty() ? QStringLiteral("{}") : v(p, QStringLiteral("checklist_json"));
    const QJsonObject act = jsonObjectFromString(checklistJson);
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT INTO vehicle_intake(intake_uid,order_uid,client_uid,vehicle_uid,mileage,fuel_level,exterior_state,interior_state,checklist_json,customer_signature_path,pdf_path,act_number,act_date,accepted_at,sync_status,updated_at) "
        "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,datetime('now')) "
        "ON CONFLICT(intake_uid) DO UPDATE SET order_uid=excluded.order_uid, client_uid=excluded.client_uid, vehicle_uid=excluded.vehicle_uid, mileage=excluded.mileage, fuel_level=excluded.fuel_level, exterior_state=excluded.exterior_state, interior_state=excluded.interior_state, checklist_json=excluded.checklist_json, customer_signature_path=excluded.customer_signature_path, pdf_path=excluded.pdf_path, act_number=excluded.act_number, act_date=excluded.act_date, accepted_at=excluded.accepted_at, sync_status='pending', updated_at=datetime('now')"));
    q.addBindValue(uid);
    q.addBindValue(v(p, QStringLiteral("order_uid")));
    q.addBindValue(v(p, QStringLiteral("client_uid")));
    q.addBindValue(v(p, QStringLiteral("vehicle_uid")));
    q.addBindValue(p.value(QStringLiteral("mileage")).toInt());
    q.addBindValue(v(p, QStringLiteral("fuel_level")));
    q.addBindValue(v(p, QStringLiteral("exterior_state")));
    q.addBindValue(v(p, QStringLiteral("interior_state")));
    q.addBindValue(checklistJson);
    q.addBindValue(v(p, QStringLiteral("customer_signature_path")));
    q.addBindValue(v(p, QStringLiteral("pdf_path")));
    q.addBindValue(jstr(act, QStringLiteral("act_number")));
    q.addBindValue(jstr(act, QStringLiteral("act_date")));
    q.addBindValue(jstr(act, QStringLiteral("accepted_at")));
    q.addBindValue(QStringLiteral("pending"));
    if (!execPrepared(q, errorText)) return false;
    if (outUid) *outUid = uid;
    return true;
}

bool updateSignaturePathDirect(QSqlDatabase db, const QString& intakeUid, const QString& signaturePath, QString* errorText)
{
    QSqlQuery q(db);
    q.prepare(QStringLiteral("UPDATE vehicle_intake SET customer_signature_path=?, sync_status='pending', updated_at=datetime('now') WHERE intake_uid=?"));
    q.addBindValue(signaturePath);
    q.addBindValue(intakeUid);
    if (!execPrepared(q, errorText)) return false;
    return ensureLocalFileAttachment(db, QStringLiteral("signature-") + intakeUid, QStringLiteral("vehicle_intake_signature"), intakeUid, signaturePath, QStringLiteral("image/png"), errorText);
}

bool addPhotoDirect(QSqlDatabase db, const QString& intakeUid, const QString& orderUid, const QString& localPath, const QString& photoType, const QString& preferredPhotoUid, QString* photoUid, QString* errorText)
{
    const QString uid = preferredPhotoUid.trimmed().isEmpty() ? EntityUidService::generateUid(QStringLiteral("photo")) : preferredPhotoUid.trimmed();
    QSqlQuery q(db);
    q.prepare(QStringLiteral("INSERT INTO vehicle_intake_photos(photo_uid,intake_uid,order_uid,local_path,photo_type) VALUES(?,?,?,?,?)"));
    q.addBindValue(uid);
    q.addBindValue(intakeUid);
    q.addBindValue(orderUid);
    q.addBindValue(localPath);
    q.addBindValue(photoType);
    if (!execPrepared(q, errorText)) return false;
    if (!ensureLocalFileAttachment(db, uid, QStringLiteral("vehicle_intake_photo"), intakeUid, localPath, QStringLiteral("image/jpeg"), errorText)) return false;
    if (photoUid) *photoUid = uid;
    return true;
}

struct IntakePdfData {
    QString orderUid;
    QString mileage;
    QString fuelLevel;
    QString checklistJson;
    QString signaturePath;
    QString actNumber;
    QString actDate;
    QString acceptedAt;
    bool blank = false;
};

bool renderIntakeActPdf(const IntakePdfData& data, const QString& outputPath, QString* errorText)
{
    QFileInfo outInfo(outputPath);
    if (outputPath.trimmed().isEmpty()) {
        if (errorText) *errorText = QString::fromUtf8("Не вказано шлях для збереження PDF");
        return false;
    }
    if (!outInfo.absoluteDir().exists() && !QDir().mkpath(outInfo.absolutePath())) {
        if (errorText) {
            *errorText = QString::fromUtf8("Не вдалося створити папку для PDF:\n%1")
                             .arg(QDir::toNativeSeparators(outInfo.absolutePath()));
        }
        return false;
    }

    const QJsonObject act = jsonObjectFromString(data.checklistJson);
    const QJsonObject conditions = jobj(act, QStringLiteral("conditions"));
    const QJsonObject equipment = jobj(act, QStringLiteral("equipment"));
    const QJsonObject permissions = jobj(act, QStringLiteral("permissions"));
    const auto serviceRequisites = GeneralSettingsService::loadPdfServiceRequisites();

    QPdfWriter pdf(outputPath);
    pdf.setPageSize(QPageSize(QPageSize::A4));
    pdf.setResolution(96);
    QPainter painter(&pdf);
    if (!painter.isActive()) {
        if (errorText) *errorText = QStringLiteral("Не вдалося створити PDF-файл");
        return false;
    }
    painter.setPen(thinPen(Qt::black));
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setRenderHint(QPainter::Antialiasing, true);
    setBodyFont(painter, 8);

    const int x = 58;
    const int w = 680;
    int y = 42;

    const QString actNumber = pdfValue(jstr(act, QStringLiteral("act_number")).isEmpty() ? data.actNumber : jstr(act, QStringLiteral("act_number")), data.blank);
    const QString actDate = pdfValue(jstr(act, QStringLiteral("act_date")).isEmpty() ? data.actDate : jstr(act, QStringLiteral("act_date")), data.blank);
    const QString acceptedAt = pdfValue(jstr(act, QStringLiteral("accepted_at")).isEmpty() ? data.acceptedAt : jstr(act, QStringLiteral("accepted_at")), data.blank);
    const QString orderNumber = pdfValue(!jstr(act, QStringLiteral("order_number")).isEmpty() ? jstr(act, QStringLiteral("order_number")) : data.orderUid, data.blank);

    auto value = [&](const QString& key) {
        return pdfValue(jstr(act, key), data.blank);
    };
    auto dateLine = [&](const QString& iso) {
        const QString clean = pdfValue(iso, data.blank);
        const QDate date = DateFormats::parseDate(clean);
        if (!date.isValid()) return QString::fromUtf8("від «__» ______ 202__ р.");
        static const QStringList months = {
            QString(), QString::fromUtf8("січня"), QString::fromUtf8("лютого"), QString::fromUtf8("березня"),
            QString::fromUtf8("квітня"), QString::fromUtf8("травня"), QString::fromUtf8("червня"),
            QString::fromUtf8("липня"), QString::fromUtf8("серпня"), QString::fromUtf8("вересня"),
            QString::fromUtf8("жовтня"), QString::fromUtf8("листопада"), QString::fromUtf8("грудня")
        };
        return QString::fromUtf8("від «%1» %2 %3 р.")
            .arg(date.day(), 2, 10, QLatin1Char('0'))
            .arg(months.value(date.month()))
            .arg(date.year());
    };

    y = drawPdfHeader(painter, x, y, w, serviceRequisites);

    const int titleY = y + 2;
    setBodyFont(painter, 14, true);
    painter.setPen(thinPen(Qt::black));
    painter.drawText(QRect(x, titleY, 390, 32), Qt::AlignLeft | Qt::AlignVCenter, QString::fromUtf8("АКТ ПРИЙМАННЯ АВТО"));
    painter.setPen(QPen(QColor(172, 22, 22), 0.8));
    painter.drawText(QRect(x + 350, titleY, 150, 32), Qt::AlignLeft | Qt::AlignVCenter, QString::fromUtf8("В РЕМОНТ"));
    setBodyFont(painter, 9);
    painter.setPen(thinPen(Qt::black));
    painter.drawText(QRect(x, titleY + 34, 330, 24), Qt::AlignLeft | Qt::AlignVCenter, dateLine(actDate));
    painter.drawRect(QRect(x + w - 154, titleY + 32, 154, 28));
    painter.drawText(QRect(x + w - 146, titleY + 32, 138, 28), Qt::AlignVCenter | Qt::AlignLeft,
                     QString::fromUtf8("№ %1").arg(actNumber));
    y += 76;

    drawSectionTitle(painter, x, y, w, QString::fromUtf8("ДАНІ КЛІЄНТА / ЗАМОВНИКА"));
    drawHorizontalValue(painter, QRect(x, y, 430, 42), QString::fromUtf8("Клієнт"), value(QStringLiteral("client_name")));
    drawHorizontalValue(painter, QRect(x + 430, y, 250, 42), QString::fromUtf8("Телефон"), value(QStringLiteral("client_phone")));
    y += 56;

    drawSectionTitle(painter, x, y, w, QString::fromUtf8("ІНФОРМАЦІЯ ПРО ТРАНСПОРТНИЙ ЗАСІБ"));
    const int cellW = w / 4;
    const QString mileage = pdfValue(jstr(act, QStringLiteral("mileage")).isEmpty() ? data.mileage : jstr(act, QStringLiteral("mileage")), data.blank);
    const QString fuel = pdfValue(jstr(act, QStringLiteral("fuel_level")).isEmpty() ? data.fuelLevel : jstr(act, QStringLiteral("fuel_level")), data.blank);
    const QList<QPair<QString, QString>> carRows = {
        {QString::fromUtf8("Марка / модель"), value(QStringLiteral("vehicle_label"))},
        {QString::fromUtf8("Державний номер"), value(QStringLiteral("plate"))},
        {QStringLiteral("VIN"), value(QStringLiteral("vin"))},
        {QString::fromUtf8("Рік випуску"), value(QStringLiteral("year"))},
        {QString::fromUtf8("Тип двигуна"), value(QStringLiteral("engine_type"))},
        {QStringLiteral("КПП"), value(QStringLiteral("gearbox"))},
        {QString::fromUtf8("Пробіг, км"), mileage},
        {QString::fromUtf8("Рівень пального"), fuel},
        {QString::fromUtf8("Дата та час приймання"), displayDateTime(acceptedAt)},
        {QString::fromUtf8("Орієнтована дата видачі"), displayDateTime(pdfValue(jstr(act, QStringLiteral("planned_return_at")), data.blank))},
        {QString::fromUtf8("№ замовлення-наряду"), orderNumber},
        {QString::fromUtf8("Майстер"), value(QStringLiteral("master"))}
    };
    for (int i = 0; i < carRows.size(); ++i) {
        const int row = i / 4;
        const int col = i % 4;
        drawKeyValueBox(painter, QRect(x + col * cellW, y + row * 42, cellW, 42), carRows.at(i).first, carRows.at(i).second);
    }
    y += 142;

    drawSectionTitle(painter, x, y, w, QString::fromUtf8("ЗАЯВЛЕНА НЕСПРАВНІСТЬ / ПОБАЖАННЯ КЛІЄНТА"));
    drawBox(painter, QRect(x, y, w, 112), QString::fromUtf8("Опис зі слів клієнта"), value(QStringLiteral("complaint")));
    y += 130;

    drawSectionTitle(painter, x, y, w, QString::fromUtf8("ПОПЕРЕДНЯ ДІАГНОСТИКА / ПОГОДЖЕНІ РОБОТИ"));
    drawBox(painter, QRect(x, y, w, 150), QString::fromUtf8("Попередній перелік робіт, примітки, обмеження"), value(QStringLiteral("diagnostics")));
    drawPageFooter(painter, 1, 2, x, w);

    pdf.newPage();
    setBodyFont(painter, 8);
    painter.setPen(thinPen(Qt::black));
    y = drawPdfHeader(painter, x, 42, w, serviceRequisites);

    drawSectionTitle(painter, x, y, w, QString::fromUtf8("ЗОВНІШНІЙ СТАН АВТО НА МОМЕНТ ПРИЙМАННЯ"));
    const QList<QPair<QString, QString>> zones = {
        {QStringLiteral("body"), QString::fromUtf8("Кузов / ЛФП")},
        {QStringLiteral("glass"), QString::fromUtf8("Скло / оптика")},
        {QStringLiteral("interior"), QString::fromUtf8("Салон / багажник")},
        {QStringLiteral("tires"), QString::fromUtf8("Шини / диски")},
        {QStringLiteral("other"), QString::fromUtf8("Інші зони огляду")}
    };
    const int zoneW = 158;
    const int stateW = 220;
    const int noteW = w - zoneW - stateW;
    painter.setPen(thinPen());
    setBodyFont(painter, 8, true);
    painter.drawRect(QRect(x, y, w, 26));
    painter.drawLine(x + zoneW, y, x + zoneW, y + 26);
    painter.drawLine(x + zoneW + stateW, y, x + zoneW + stateW, y + 26);
    painter.drawText(QRect(x + 6, y, zoneW - 12, 26), Qt::AlignVCenter, QString::fromUtf8("Зона огляду"));
    painter.drawText(QRect(x + zoneW + 6, y, stateW - 12, 26), Qt::AlignVCenter, QString::fromUtf8("Стан або відмітка"));
    painter.drawText(QRect(x + zoneW + stateW + 6, y, noteW - 12, 26), Qt::AlignVCenter, QString::fromUtf8("Примітка про пошкодження"));
    setBodyFont(painter, 8);
    y += 26;
    for (const auto& zoneDef : zones) {
        const QJsonObject zone = jobj(conditions, zoneDef.first);
        const bool hasIssues = !data.blank && jstr(zone, QStringLiteral("status")) == QStringLiteral("issues");
        const bool noIssues = !data.blank && !hasIssues;
        painter.setPen(thinPen());
        painter.drawRect(QRect(x, y, w, 42));
        painter.drawLine(x + zoneW, y, x + zoneW, y + 42);
        painter.drawLine(x + zoneW + stateW, y, x + zoneW + stateW, y + 42);
        painter.drawText(QRect(x + 6, y + 3, zoneW - 12, 36), Qt::AlignVCenter, zoneDef.second);
        drawCheck(painter, x + zoneW + 8, y + 18, QString::fromUtf8("без зауважень"), noIssues);
        drawCheck(painter, x + zoneW + 112, y + 18, QString::fromUtf8("є зауваження"), hasIssues);
        drawWrapped(painter, QRect(x + zoneW + stateW + 6, y + 5, noteW - 12, 34), pdfValue(jstr(zone, QStringLiteral("note")), data.blank));
        y += 42;
    }
    y += 12;

    drawSectionTitle(painter, x, y, w, QString::fromUtf8("КОМПЛЕКТАЦІЯ ТА МАЙНО В АВТО"));
    const QList<QPair<QString, QString>> eq = {
        {QStringLiteral("main_key"), QString::fromUtf8("ключ основний")},
        {QStringLiteral("service_book"), QString::fromUtf8("сервісна книжка")},
        {QStringLiteral("spare_wheel"), QString::fromUtf8("запасне колесо")},
        {QStringLiteral("warning_triangle"), QString::fromUtf8("знак аварійної зупинки")},
        {QStringLiteral("charger"), QString::fromUtf8("зарядний пристрій")},
        {QStringLiteral("spare_key"), QString::fromUtf8("ключ запасний")},
        {QStringLiteral("jack"), QString::fromUtf8("домкрат")},
        {QStringLiteral("first_aid"), QString::fromUtf8("аптечка")},
        {QStringLiteral("mats"), QString::fromUtf8("килимки")},
        {QStringLiteral("docs_receipts"), QString::fromUtf8("документи / чеки")},
        {QStringLiteral("registration"), QString::fromUtf8("техпаспорт")},
        {QStringLiteral("wheel_wrench"), QString::fromUtf8("балонний ключ")},
        {QStringLiteral("extinguisher"), QString::fromUtf8("вогнегасник")},
        {QStringLiteral("media_screen"), QString::fromUtf8("магнітола / екран")},
        {QStringLiteral("other"), QString::fromUtf8("інше")}
    };
    for (int i = 0; i < eq.size(); ++i) {
        const int col = i % 3;
        const int row = i / 3;
        drawCheck(painter, x + col * 225, y + row * 22 + 15, eq.at(i).second, !data.blank && jbool(equipment, eq.at(i).first));
    }
    y += 114;
    drawBox(painter, QRect(x, y, w, 52), QString::fromUtf8("Цінні речі / особисті речі, що залишаються в авто"), value(QStringLiteral("valuables")));
    y += 68;

    drawSectionTitle(painter, x, y, w, QString::fromUtf8("ДОЗВОЛИ ТА УМОВИ ПРИЙМАННЯ"));
    const QList<QPair<QString, QString>> perm = {
        {QStringLiteral("initial_diagnostics"), QString::fromUtf8("первинна діагностика дозволена")},
        {QStringLiteral("test_drive"), QString::fromUtf8("тест-драйв дозволено")},
        {QStringLiteral("photo"), QString::fromUtf8("фотофіксація дозволена")},
        {QStringLiteral("extra_approval"), QString::fromUtf8("додаткові роботи - тільки після погодження з клієнтом")},
        {QStringLiteral("cost_approval"), QString::fromUtf8("попереднє погодження вартості")}
    };
    for (int i = 0; i < perm.size(); ++i) {
        const int col = i % 2;
        const int row = i / 2;
        drawCheck(painter, x + col * 340, y + row * 22 + 15, perm.at(i).second, !data.blank && jbool(permissions, perm.at(i).first, true));
    }
    y += 70;
    drawHorizontalValue(painter, QRect(x, y, w, 38), QString::fromUtf8("Спосіб зв'язку"), value(QStringLiteral("contact_method")));
    y += 50;

    const QString terms = QString::fromUtf8(
        "1. Автомобіль прийнято в ремонт у стані, зазначеному в цьому акті.\n"
        "2. Остаточний перелік робіт, термін та вартість визначаються після діагностики.\n"
        "3. Додаткові роботи і запчастини погоджуються із замовником до виконання.\n"
        "4. Сервіс не відповідає за особисті речі, які не внесені до акту приймання.\n"
        "5. Замовник підтверджує правильність даних та погоджує виконання зазначених робіт.");
    painter.setPen(thinPen());
    drawWrapped(painter, QRect(x, y, w, 86), terms);
    y += 96;

    drawSectionTitle(painter, x, y, w, QString::fromUtf8("ПРИЙМАННЯ-ПЕРЕДАЧА"));
    const int signatureY = y;
    painter.setPen(thinPen());
    painter.drawRect(QRect(x, y, w / 2, 76));
    painter.drawRect(QRect(x + w / 2, y, w / 2, 76));
    setBodyFont(painter, 8, true);
    painter.drawText(QRect(x + 8, y + 8, w / 2 - 16, 20), Qt::AlignLeft, QString::fromUtf8("Автомобіль передав / замовник"));
    painter.drawText(QRect(x + w / 2 + 8, y + 8, w / 2 - 16, 20), Qt::AlignLeft, QString::fromUtf8("Автомобіль прийняв / майстер-приймальник"));
    setBodyFont(painter, 8);
    painter.drawText(QRect(x + 8, y + 34, w / 2 - 16, 20), Qt::AlignLeft, QString::fromUtf8("ПІБ: %1").arg(value(QStringLiteral("customer_name"))));
    painter.drawText(QRect(x + 8, y + 60, w / 2 - 16, 18), Qt::AlignLeft, QString::fromUtf8("Підпис: __________________"));
    painter.drawText(QRect(x + w / 2 + 8, y + 34, w / 2 - 16, 20), Qt::AlignLeft, QString::fromUtf8("ПІБ: %1").arg(value(QStringLiteral("receiver_name"))));
    painter.drawText(QRect(x + w / 2 + 8, y + 60, w / 2 - 16, 18), Qt::AlignLeft, QString::fromUtf8("Підпис: __________________"));
    y += 86;
    painter.drawText(QRect(x, y, 300, 24), Qt::AlignLeft, QString::fromUtf8("Дата: %1").arg(displayDate(actDate)));
    painter.drawText(QRect(x + 330, y, 300, 24), Qt::AlignLeft, QString::fromUtf8("Час приймання: %1").arg(displayTime(acceptedAt)));

    const QString signaturePath = data.blank ? QString() : data.signaturePath;
    if (!signaturePath.isEmpty()) {
        const QImage signatureImage(signaturePath);
        if (!signatureImage.isNull()) {
            const QRect imageRect(x + 120, signatureY + 50, 160, 28);
            const QImage scaled = signatureImage.scaled(imageRect.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
            const QPoint topLeft(imageRect.x() + (imageRect.width() - scaled.width()) / 2,
                                 imageRect.y() + (imageRect.height() - scaled.height()) / 2);
            painter.drawImage(topLeft, scaled);
        }
    }
    drawPageFooter(painter, 2, 2, x, w);

    painter.end();
    return true;
}

bool generatePdfDirect(QSqlDatabase db, const QString& intakeUid, const QString& outputPath, QString* errorText)
{
    if (!ensureVehicleIntakeActColumns(db, errorText)) return false;
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT order_uid,mileage,fuel_level,exterior_state,interior_state,checklist_json,customer_signature_path,act_number,act_date,accepted_at FROM vehicle_intake WHERE intake_uid=?"));
    q.addBindValue(intakeUid);
    if (!q.exec() || !q.next()) {
        if (errorText) *errorText = QStringLiteral("Акт приймання не знайдено");
        return false;
    }

    IntakePdfData data;
    data.orderUid = q.value(0).toString();
    data.mileage = q.value(1).toString();
    data.fuelLevel = q.value(2).toString();
    data.checklistJson = q.value(5).toString();
    data.signaturePath = q.value(6).toString();
    data.actNumber = q.value(7).toString();
    data.actDate = q.value(8).toString();
    data.acceptedAt = q.value(9).toString();
    if (!renderIntakeActPdf(data, outputPath, errorText)) return false;

    QSqlQuery up(db);
    up.prepare(QStringLiteral("UPDATE vehicle_intake SET pdf_path=?, sync_status='pending', updated_at=datetime('now') WHERE intake_uid=?"));
    up.addBindValue(outputPath);
    up.addBindValue(intakeUid);
    if (!execPrepared(up, errorText)) return false;
    return ensureLocalFileAttachment(db, QStringLiteral("pdf-") + intakeUid, QStringLiteral("vehicle_intake_pdf"), intakeUid, outputPath, QStringLiteral("application/pdf"), errorText);
}


bool saveIntake(QSqlDatabase db, const QVariantMap& p, QString* outUid, QString* errorText)
{
    const QString requestedUid = p.value(QStringLiteral("intake_uid")).toString().trimmed();
    const QString plannedUid = requestedUid.isEmpty() ? EntityUidService::generateUid(QStringLiteral("intake")) : requestedUid;
    QString createdUid;
    MutationExecutor::Request request;
    request.sourceModule = QStringLiteral("VehicleIntakeService");
    request.entityType = QStringLiteral("vehicle_intake");
    request.entityId = plannedUid;
    request.operation = requestedUid.isEmpty() ? QStringLiteral("create") : QStringLiteral("update");
    request.description = QString::fromUtf8("Збереження акту приймання авто");
    request.changedTables = {QStringLiteral("vehicle_intake")};
    request.beginErrorFallback = QString::fromUtf8("Не вдалося почати збереження акту приймання.");
    request.commitErrorFallback = QString::fromUtf8("Не вдалося зберегти акт приймання.");
    const auto result = MutationExecutor::execute(db, request, [&](QSqlDatabase& txDb, QString* txError) {
        return saveIntakeDirect(txDb, p, plannedUid, &createdUid, txError);
    });
    if (!result.ok) { if (errorText) *errorText = result.errorText; return false; }
    if (outUid) *outUid = createdUid;
    return true;
}

bool updateSignaturePath(QSqlDatabase db, const QString& intakeUid, const QString& signaturePath, QString* errorText)
{
    MutationExecutor::Request request;
    request.sourceModule = QStringLiteral("VehicleIntakeService");
    request.entityType = QStringLiteral("vehicle_intake");
    request.entityId = intakeUid;
    request.operation = QStringLiteral("update");
    request.description = QString::fromUtf8("Додавання підпису клієнта до акту приймання");
    request.changedTables = {QStringLiteral("vehicle_intake"), QStringLiteral("file_attachments")};
    const auto result = MutationExecutor::execute(db, request, [&](QSqlDatabase& txDb, QString* txError) {
        return updateSignaturePathDirect(txDb, intakeUid, signaturePath, txError);
    });
    if (!result.ok && errorText) *errorText = result.errorText;
    return result.ok;
}

bool addPhoto(QSqlDatabase db, const QString& intakeUid, const QString& orderUid, const QString& localPath, const QString& photoType, QString* photoUid, QString* errorText)
{
    const QString plannedPhotoUid = EntityUidService::generateUid(QStringLiteral("photo"));
    QString createdPhotoUid;
    MutationExecutor::Request request;
    request.sourceModule = QStringLiteral("VehicleIntakeService");
    request.entityType = QStringLiteral("vehicle_intake_photo");
    request.entityId = plannedPhotoUid;
    request.operation = QStringLiteral("create");
    request.description = QString::fromUtf8("Додавання фото до акту приймання");
    request.changedTables = {QStringLiteral("vehicle_intake_photos"), QStringLiteral("vehicle_intake"), QStringLiteral("file_attachments")};
    const auto result = MutationExecutor::execute(db, request, [&](QSqlDatabase& txDb, QString* txError) {
        return addPhotoDirect(txDb, intakeUid, orderUid, localPath, photoType, plannedPhotoUid, &createdPhotoUid, txError);
    });
    if (!result.ok) { if (errorText) *errorText = result.errorText; return false; }
    if (photoUid) *photoUid = createdPhotoUid;
    return true;
}

QString blankActPdfPath(QString* errorText)
{
    const auto dirs = GeneralSettingsService::loadDirectoriesSettings();
    QString root = dirs.reportsDir.trimmed();
    if (root.isEmpty()) {
        root = QDir(AppRuntime::dataDir()).filePath(QStringLiteral("reports"));
    }
    const QString dirPath = QDir(root).filePath(QStringLiteral("intake/blank"));
    if (!QDir().mkpath(dirPath)) {
        if (errorText) {
            *errorText = QString::fromUtf8("Не вдалося створити папку для пустих актів:\n%1")
                             .arg(QDir::toNativeSeparators(dirPath));
        }
        return QString();
    }
    QFileInfo dirInfo(dirPath);
    if (!dirInfo.exists() || !dirInfo.isDir() || !dirInfo.isWritable()) {
        if (errorText) {
            *errorText = QString::fromUtf8("Папка пустих актів недоступна для запису:\n%1")
                             .arg(QDir::toNativeSeparators(dirPath));
        }
        return QString();
    }

    const QString fileName = QStringLiteral("VehicleIntake_BlankAct_%1.pdf")
                                 .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")));
    return QDir(dirPath).filePath(fileName);
}

bool createBlankIntakeActPdf(const QString& outputPath, QString* errorText)
{
    IntakePdfData data;
    data.checklistJson = QStringLiteral("{}");
    data.blank = true;
    return renderIntakeActPdf(data, outputPath, errorText);
}

bool generatePdf(QSqlDatabase db, const QString& intakeUid, const QString& outputPath, QString* errorText)
{
    MutationExecutor::Request request;
    request.sourceModule = QStringLiteral("VehicleIntakeService");
    request.entityType = QStringLiteral("vehicle_intake");
    request.entityId = intakeUid;
    request.operation = QStringLiteral("update");
    request.description = QString::fromUtf8("Формування PDF акту приймання авто");
    request.changedTables = {QStringLiteral("vehicle_intake"), QStringLiteral("file_attachments")};
    const auto result = MutationExecutor::execute(db, request, [&](QSqlDatabase& txDb, QString* txError) {
        return generatePdfDirect(txDb, intakeUid, outputPath, txError);
    });
    if (!result.ok && errorText) *errorText = result.errorText;
    return result.ok;
}

} // namespace VehicleIntakeService

namespace VehicleIntakeService {
bool lookupVehicleByPlate(const QString& plate, VehiclePrefill* out, QString* errorText) {
    return lookupVehicleByPlate(Database::db(), plate, out, errorText);
}
bool lookupOrderForIntake(const QString& orderKey, VehiclePrefill* out, QString* errorText) {
    return lookupOrderForIntake(Database::db(), orderKey, out, errorText);
}
bool saveIntake(const QVariantMap& payload, QString* intakeUid, QString* errorText) {
    return saveIntake(Database::db(), payload, intakeUid, errorText);
}
bool updateSignaturePath(const QString& intakeUid, const QString& signaturePath, QString* errorText) {
    return updateSignaturePath(Database::db(), intakeUid, signaturePath, errorText);
}
bool addPhoto(const QString& intakeUid, const QString& orderUid, const QString& localPath, const QString& photoType, QString* photoUid, QString* errorText) {
    return addPhoto(Database::db(), intakeUid, orderUid, localPath, photoType, photoUid, errorText);
}
bool generatePdf(const QString& intakeUid, const QString& outputPath, QString* errorText) {
    return generatePdf(Database::db(), intakeUid, outputPath, errorText);
}
namespace {
IntakeRecord intakeRecord(const QSqlQuery& query) {
    return {query.value(0).toString(), query.value(1).toString(), query.value(2).toString(),
            query.value(3).toString(), query.value(4).toString(), query.value(5).toString(),
            query.value(6).toString(), query.value(7).toString(), query.value(8).toString()};
}
const QString intakeColumns = QStringLiteral("intake_uid,order_uid,client_uid,vehicle_uid,mileage,fuel_level,customer_signature_path,checklist_json,pdf_path");
}
bool listIntakes(QVector<IntakeRecord>* result, QString* errorText) {
    if (errorText) errorText->clear();
    if (!result) return false;
    result->clear();
    QSqlQuery query(Database::db());
    if (!query.exec("SELECT " + intakeColumns + " FROM vehicle_intake ORDER BY created_at DESC LIMIT 300")) {
        if (errorText) *errorText = query.lastError().text();
        return false;
    }
    QVector<IntakeRecord> records;
    while (query.next()) records.append(intakeRecord(query));
    if (query.lastError().isValid()) {
        if (errorText) *errorText = query.lastError().text();
        return false;
    }
    *result = records;
    return true;
}
bool loadIntake(const QString& uid, IntakeRecord* result, QString* errorText) {
    if (errorText) errorText->clear();
    if (!result) return false;
    *result = {};
    QSqlQuery query(Database::db());
    query.prepare("SELECT " + intakeColumns + " FROM vehicle_intake WHERE intake_uid=? LIMIT 1");
    query.addBindValue(uid);
    if (!query.exec() || !query.next()) {
        if (errorText) *errorText = query.lastError().isValid() ? query.lastError().text() : QStringLiteral("Акт приймання не знайдено.");
        return false;
    }
    *result = intakeRecord(query);
    return true;
}
}
