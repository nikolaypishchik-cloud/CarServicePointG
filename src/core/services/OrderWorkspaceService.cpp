#include "core/services/orders/ClientReceiptReader.h"
#include "core/util/DateFormats.h"
#include "core/services/OrderWorkspaceService.h"

#include "infrastructure/db/sqlite/Db.h"
#include "core/util/PlateUtils.h"
#include "core/crm/CrmClientService.h"
#include "core/services/OrderIdentityService.h"
#include "core/backup/BackupCommitPublisher.h"
#include "core/services/MutationExecutor.h"

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QDate>
#include <QDateTime>
#include <QDebug>
#include <QApplication>
#include <QUuid>
#include <cmath>
#include <memory>
#include <QFileInfo>
#include "runtime/AppRuntime.h"
#include "infrastructure/db/sqlite/ScopedConnectionManager.h"

namespace {
QString nonNullText(const QString& value)
{
    return value.isNull() ? QStringLiteral("") : value;
}

QString normalizedWorkspaceStatus(QString value)
{
    value = value.trimmed().toLower();
    if (value == QStringLiteral("closed") || value == QString::fromUtf8("закритий") || value == QString::fromUtf8("закрыт")) {
        return QStringLiteral("closed");
    }
    if (value == QStringLiteral("in_progress") || value == QString::fromUtf8("в роботі") || value == QString::fromUtf8("в работе")) {
        return QStringLiteral("in_progress");
    }
    if (value == QStringLiteral("open") || value == QString::fromUtf8("відкритий") || value == QString::fromUtf8("открыт")) {
        return QStringLiteral("open");
    }
    return value;
}

bool hasRealClosedAtForPayroll(const QString& value)
{
    const QString trimmed = value.trimmed();
    if (trimmed.isEmpty()
        || trimmed == QStringLiteral("0")
        || trimmed == QStringLiteral("0000-00-00")
        || trimmed == QStringLiteral("0000-00-00 00:00:00")
        || trimmed == QStringLiteral("1899-12-30")
        || trimmed == QStringLiteral("1900-01-01")
        || trimmed == QStringLiteral("1970-01-01")) {
        return false;
    }
    return DateFormats::parseDateTime(trimmed).isValid()
        || DateFormats::parseDate(trimmed.left(10)).isValid();
}

QString masterNameForCloseSnapshot(QSqlDatabase database, const QString& masterId)
{
    const QString id = masterId.trimmed();
    if (id.isEmpty()) return QString();
    QSqlQuery q(database);
    q.prepare(QStringLiteral("SELECT COALESCE(name,'') FROM masters WHERE id=? LIMIT 1"));
    q.addBindValue(id);
    if (q.exec() && q.next()) return q.value(0).toString().trimmed();
    return QString();
}

static bool shouldPublishOrderWorkspaceBackup()
{
    return !(qApp && qApp->property("csDisableOrderSaveBackup").toBool());
}

MutationResult upsertOrderInternal(const OrderWorkspaceInput& input, int* orderNumber)
{
    QSqlDatabase db = Database::db();
    const QDateTime effectiveOpenedAt = input.openedAt.isValid() ? input.openedAt : QDateTime::currentDateTime();
    const QDateTime creationTime = input.createdAt.isValid() ? input.createdAt : QDateTime::currentDateTimeUtc();
    OrderIdentityService::Allocation allocation;

    int number = input.number;
    bool exists = false;
    int visibleNumber = input.visibleNumber;
    int existingVisibleNumber = 0;
    int existingYear = 0;
    QString existingUid;
    QString existingStatus;
    QString existingClosedAt;
    QString existingMasterNameClosed;

    MutationExecutor::Request request;
    request.sourceModule = QStringLiteral("OrderWorkspaceService");
    request.entityType = QStringLiteral("work_order");
    request.entityId = QString::number(number > 0 ? number : input.number);
    request.operation = QStringLiteral("upsert");
    request.description = QString::fromUtf8("Збереження заголовка наряду");
    request.changedTables = {QStringLiteral("work_orders"), QStringLiteral("annual_order_counters"), QStringLiteral("order_creation_reservations"), QStringLiteral("service_masters"), QStringLiteral("crm_clients"), QStringLiteral("crm_vehicles"), QStringLiteral("crm_client_events"), QStringLiteral("workspace_order_state"), QStringLiteral("order_registry")};
    request.beginErrorFallback = QString::fromUtf8("Не вдалося відкрити транзакцію збереження наряду.");
    request.commitErrorFallback = QString::fromUtf8("Не вдалося завершити збереження наряду.");

    // Invalid input is rejected before starting a mutation, not reported as a DB failure.
    if (!std::isfinite(input.discount) || input.discount < 0
        || (input.discountMode == QStringLiteral("percent") && input.discount > 100)
        || (input.discountMode != QStringLiteral("percent") && input.discountMode != QStringLiteral("uah") && input.discountMode != QStringLiteral("legacy"))) {
        MutationResult invalid;
        invalid.sourceModule=request.sourceModule;
        invalid.entityType=request.entityType;
        invalid.entityId=request.entityId;
        invalid.operation=request.operation;
        invalid.errorText=QString::fromUtf8("Перевірте знижку: сума не може бути від’ємною, відсоток — понад 100; оберіть % або грн.");
        return invalid;
    }

    const auto execResult = MutationExecutor::execute(db, request, [&](QSqlDatabase& txDb, QString* txError) {
        auto loadExisting = [&]() {
            QSqlQuery chk(txDb);
            if (!chk.prepare(QStringLiteral("SELECT COALESCE(order_uid,''), COALESCE(order_number,0), COALESCE(order_year,0), COALESCE(status,''), COALESCE(closed_at,''), COALESCE(master_name_closed,'') FROM work_orders WHERE number=? LIMIT 1"))) {
                if (txError) *txError = chk.lastError().text(); return false;
            }
            chk.addBindValue(number);
            if (!chk.exec()) { if (txError) *txError = chk.lastError().text(); return false; }
            if (chk.next()) {
                exists = true;
                if (chk.value(0).toString().trimmed().isEmpty() || chk.value(1).toInt() <= 0 || chk.value(2).toInt() <= 0) {
                    if (txError) *txError = QStringLiteral("Existing order identity requires migration before editing"); return false;
                }
                if (allocation.replayed && (chk.value(0).toString() != allocation.orderUid
                    || chk.value(1).toInt() != allocation.visibleNumber || chk.value(2).toInt() != allocation.year)) {
                    if (txError) *txError = QStringLiteral("Creation request refers to another order identity"); return false;
                }
                existingUid = chk.value(0).toString();
                existingVisibleNumber = chk.value(1).toInt(); existingYear = chk.value(2).toInt();
                existingStatus = chk.value(3).toString(); existingClosedAt = chk.value(4).toString();
                existingMasterNameClosed = chk.value(5).toString();
            }
            if (chk.lastError().isValid()) { if (txError) *txError = chk.lastError().text(); return false; }
            return true;
        };
        if (number > 0 && !loadExisting()) return false;
        if (!exists) {
            const int preferred = number > 0 ? number : Database::nextOrderInternalNumber();
            std::unique_ptr<ScopedSqliteConnection> registry;
            std::function<QSqlDatabase()> registryResolver;
            const auto registryPath = AppRuntime::registryDbFilePath();
            if (QFileInfo::exists(registryPath)) {
                registry = std::make_unique<ScopedSqliteConnection>(registryPath, QStringLiteral("annual_identity_registry"), txError);
                if (!registry->isOpen()) return false;
                registryResolver = [&] { return registry->db(); };
            }
            if (!OrderIdentityService::allocateAnnualIdentity(txDb, input.creationRequestUid, creationTime,
                                                               preferred, &allocation, txError, registryResolver)) return false;
            number = allocation.internalNumber;
            if (allocation.replayed) {
                if (!loadExisting()) return false;
                if (!exists) { if (txError) *txError = QStringLiteral("The original order was deleted; its creation request cannot recreate it"); return false; }
                request.entityId=QString::number(number); request.operation=QStringLiteral("replay_create");
                request.description=QStringLiteral("Repeated confirmed creation request");
                return Database::finalizeOrderWorkspaceInTransaction(txDb, number, txError);
            }
        }

        // Editing must not replace a migrated annual number with the internal key.
        visibleNumber = exists && existingVisibleNumber > 0 ? existingVisibleNumber : allocation.visibleNumber;
        const int orderYear = exists && existingYear > 0 ? existingYear : allocation.year;

        const QString plateNorm = PlateUtils::normalizePlate(input.plateNorm.trimmed().isEmpty() ? input.plate : input.plateNorm);
        const QString plateDisplay = PlateUtils::formatPlateDisplay(plateNorm.isEmpty() ? input.plate : plateNorm);

        const QString normalizedStatus = normalizedWorkspaceStatus(input.status);
        const QString statusForStorage = normalizedStatus.isEmpty() ? nonNullText(input.status) : normalizedStatus;
        const bool closingNow = (statusForStorage == QStringLiteral("closed"));
        const bool wasClosed = (normalizedWorkspaceStatus(existingStatus) == QStringLiteral("closed"));
        QString closedAtForStorage;
        QString masterNameClosedForStorage;
        if (closingNow) {
            closedAtForStorage = (wasClosed && hasRealClosedAtForPayroll(existingClosedAt))
                ? existingClosedAt.trimmed()
                : QDateTime::currentDateTime().toString(Qt::ISODate);
            masterNameClosedForStorage = (wasClosed && !existingMasterNameClosed.trimmed().isEmpty())
                ? existingMasterNameClosed.trimmed()
                : masterNameForCloseSnapshot(txDb, input.masterId);
        }

        QSqlQuery q(txDb);
        QString newUid;
        if (!exists) {
            q.prepare(QStringLiteral(
                "INSERT INTO work_orders(number, order_uid, order_number, order_year, opened_at, closed_at, status, plate, plate_norm, vin, car, color, car_color, car_year, odometer, engine_type, engine_volume, fuel_type, client, client_phone, master_id, master_name_closed, works_sum, parts_sum, discount, discount_mode) "
                "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)"));
            q.addBindValue(number);
            newUid = allocation.orderUid;
            q.addBindValue(newUid);
            q.addBindValue(visibleNumber);
            q.addBindValue(orderYear);
        } else {
            q.prepare(QStringLiteral(
                "UPDATE work_orders SET opened_at=?, closed_at=?, status=?, plate=?, plate_norm=?, vin=?, car=?, color=?, car_color=?, car_year=?, odometer=?, engine_type=?, engine_volume=?, fuel_type=?, client=?, client_phone=?, master_id=?, master_name_closed=?, works_sum=?, parts_sum=?, discount=?, discount_mode=?, order_number=?, order_year=COALESCE(NULLIF(order_year,0), ?) WHERE number=?"));
        }

        q.addBindValue(effectiveOpenedAt.toString(Qt::ISODate));
        q.addBindValue(closedAtForStorage);
        q.addBindValue(statusForStorage);
        q.addBindValue(nonNullText(plateDisplay.isEmpty() ? input.plate : plateDisplay));
        q.addBindValue(nonNullText(plateNorm));
        q.addBindValue(nonNullText(input.vin));
        q.addBindValue(nonNullText(input.car));
        q.addBindValue(nonNullText(input.color));
        q.addBindValue(nonNullText(input.color));
        q.addBindValue(input.carYear);
        q.addBindValue(input.odometer);
        q.addBindValue(nonNullText(input.engineType));
        q.addBindValue(nonNullText(input.engineVolume));
        q.addBindValue(nonNullText(input.fuelType));
        q.addBindValue(nonNullText(input.client));
        q.addBindValue(nonNullText(input.clientPhone));
        q.addBindValue(nonNullText(input.masterId));
        q.addBindValue(nonNullText(masterNameClosedForStorage));
        q.addBindValue(input.worksSum);
        q.addBindValue(input.partsSum);
        q.addBindValue(input.discount);
        q.addBindValue(input.discountMode);
        if (exists) {
            q.addBindValue(visibleNumber);
            q.addBindValue(orderYear);
            q.addBindValue(number);
        }

        if (!q.exec()) {
            if (txError) *txError = q.lastError().text();
            return false;
        }
        if (q.numRowsAffected()!=1) {
            if (txError) *txError = QStringLiteral("Order header write did not affect the expected record");
            return false;
        }

        // Mandatory relationships and payroll snapshots belong to the same
        // transaction as the header and annual reservation, before audit/outbox.
        if (!CrmClientService::ensureFromWorkOrder(txDb, number, nullptr, nullptr, txError)) return false;
        QSqlQuery snapshot(txDb);
        const QString savedUid = exists ? existingUid : newUid;
        if (closingNow) {
            if (!snapshot.prepare(QStringLiteral("UPDATE service_masters SET master_name_closed=CASE WHEN ? AND TRIM(COALESCE(master_name_closed,''))<>'' THEN master_name_closed ELSE COALESCE((SELECT name FROM masters WHERE id=service_masters.master_id),'') END WHERE service_id IN (SELECT id FROM work_order_services WHERE order_uid=?)"))) {
                if (txError) *txError = snapshot.lastError().text(); return false;
            }
            snapshot.addBindValue(wasClosed);
        } else {
            if (!snapshot.prepare(QStringLiteral("UPDATE service_masters SET master_name_closed='' WHERE service_id IN (SELECT id FROM work_order_services WHERE order_uid=?)"))) {
                if (txError) *txError = snapshot.lastError().text(); return false;
            }
        }
        snapshot.addBindValue(savedUid);
        if (!snapshot.exec()) { if (txError) *txError = snapshot.lastError().text(); return false; }

        if (!Database::finalizeOrderWorkspaceInTransaction(txDb, number, txError)) return false;
        if (!exists && !ClientReceiptReader::beginTrackingNewOrder(txDb,newUid,txError)) return false;
        request.entityId = QString::number(number);
        request.operation = exists ? QStringLiteral("update") : QStringLiteral("create");
        request.description = exists ? QString::fromUtf8("Оновлення заголовка наряду") : QString::fromUtf8("Створення заголовка наряду");
        return true;
    });
    if (!execResult.ok) {
        return execResult;
    }

    Database::publishFinalizedOrderWorkspace(number);
    if (allocation.replayed) {
        auto replayResult = execResult;
        replayResult.entityId = QString::number(number); replayResult.operation = QStringLiteral("replay_create");
        if (orderNumber) *orderNumber = number;
        return replayResult;
    }
    const QString orderUid = Database::resolveOrderUid(number);
    if (shouldPublishOrderWorkspaceBackup()) {
        BackupCommitPublisher::publish(
            BackupCommitPublisher::makeEvent(QStringLiteral("OrderWorkspaceService"),
                                             QStringLiteral("order.header"),
                                             QStringLiteral("work_order"),
                                             QString::number(number),
                                             orderUid,
                                             number,
                                             exists ? QStringLiteral("update") : QStringLiteral("create"),
                                             exists ? QString::fromUtf8("Оновлено заголовок наряду") : QString::fromUtf8("Створено наряд"),
                                             {QStringLiteral("work_orders")}));
    }

    MutationResult finalResult = execResult;
    finalResult.ok = true;
    finalResult.entityId = QString::number(number);
    finalResult.operation = exists ? QStringLiteral("update") : QStringLiteral("create");
    finalResult.description = exists ? QString::fromUtf8("Оновлено заголовок наряду") : QString::fromUtf8("Створено наряд");
    if (orderNumber) *orderNumber = number;
    return finalResult;
}

} // namespace

MutationResult OrderWorkspaceService::ensureEditableOrderDetailed(int orderNumber)
{
    MutationResult result;
    result.sourceModule = QStringLiteral("OrderWorkspaceService");
    result.entityType = QStringLiteral("work_order");
    result.entityId = QString::number(orderNumber);
    result.operation = QStringLiteral("ensure_editable");
    result.description = QString::fromUtf8("Підготовка наряду до редагування");
    result.changedTables = {QStringLiteral("work_orders"), QStringLiteral("workspace_order_state")};
    result.ok = (orderNumber <= 0) || Database::ensureOrderReadyForInteractiveOpen(orderNumber, &result.errorText);
    return result;
}

bool OrderWorkspaceService::ensureEditableOrder(int orderNumber, QString* errorText)
{
    const auto result = ensureEditableOrderDetailed(orderNumber);
    if (!result.ok && errorText) *errorText = result.errorText;
    return result.ok;
}

MutationResult OrderWorkspaceService::createDraftEditableOrderDetailed(const OrderWorkspaceInput& input, int* orderNumber)
{
    return upsertOrderInternal(input, orderNumber);
}

bool OrderWorkspaceService::createDraftEditableOrder(const OrderWorkspaceInput& input, int* orderNumber, QString* errorText)
{
    const auto result = createDraftEditableOrderDetailed(input, orderNumber);
    if (!result.ok && errorText) *errorText = result.errorText;
    return result.ok;
}

MutationResult OrderWorkspaceService::saveEditableOrderDetailed(const OrderWorkspaceInput& input, int* orderNumber)
{
    return upsertOrderInternal(input, orderNumber);
}

bool OrderWorkspaceService::saveEditableOrder(const OrderWorkspaceInput& input, int* orderNumber, QString* errorText)
{
    const auto result = saveEditableOrderDetailed(input, orderNumber);
    if (!result.ok && errorText) *errorText = result.errorText;
    return result.ok;
}
