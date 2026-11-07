#include "core/services/orders/WorkPartsMutationService.h"
#include "core/diagnostics/RuntimeLoggingControl.h"

#include "core/services/MutationExecutor.h"
#include "core/repository/PartRepository.h"
#include "core/services/orders/OrderAttachmentService.h"
#include "core/services/orders/PartMutationService.h"
#include "core/services/PartsQueryService.h"
#include "infrastructure/db/sqlite/DbContext.h"

#include <QSqlDatabase>
#include <QSqlError>
#include <QDebug>
#include <functional>

namespace WorkPartsMutationService {

namespace {

QString mutationErrorOrDefault(const QString& errorText, const QString& fallback)
{
    return errorText.trimmed().isEmpty() ? fallback : errorText;
}

} // namespace

bool setServiceAttachment(int serviceId, const QString& filePath, const QString& displayName, QString* errorText)
{
    MutationExecutor::Request request;
    request.sourceModule = QStringLiteral("core.services.orders.WorkPartsMutationService");
    request.entityType = QStringLiteral("service_attachment");
    request.entityId = QString::number(serviceId);
    request.operation = QStringLiteral("set_service_attachment");
    request.description = QString::fromUtf8("Збереження metadata вкладення роботи");
    request.changedTables = {QStringLiteral("work_order_service_attachments")};
    request.beginErrorFallback = QString::fromUtf8("Не можу відкрити транзакцію збереження вкладення роботи.");
    request.commitErrorFallback = QString::fromUtf8("Не вдалося завершити збереження вкладення роботи.");

    const auto result = execute(request, [&](QSqlDatabase& txDb, QString* txError) {
        return OrderAttachmentService::setServiceAttachment(txDb, serviceId, filePath, displayName, txError);
    });
    if (!result.ok && errorText) *errorText = mutationErrorOrDefault(result.errorText, request.commitErrorFallback);
    return result.ok;
}

bool setPartAttachment(int partId, const QString& filePath, const QString& displayName,
                       const QString& invoiceNumber, const QString& invoiceDate, QString* errorText, bool confirmedImport, double confirmedAmount)
{
    MutationExecutor::Request request;
    request.sourceModule = QStringLiteral("core.services.orders.WorkPartsMutationService");
    request.entityType = QStringLiteral("part_attachment");
    request.entityId = QString::number(partId);
    request.operation = QStringLiteral("set_part_attachment");
    request.description = QString::fromUtf8("Збереження metadata вкладення запчастини");
    request.changedTables = {QStringLiteral("work_order_part_attachments"), QStringLiteral("invoice_part_evidence")};
    request.beginErrorFallback = QString::fromUtf8("Не можу відкрити транзакцію збереження вкладення запчастини.");
    request.commitErrorFallback = QString::fromUtf8("Не вдалося завершити збереження вкладення запчастини.");

    const auto result = execute(request, [&](QSqlDatabase& txDb, QString* txError) {
        return OrderAttachmentService::setPartAttachment(txDb, partId, filePath, displayName, invoiceNumber, invoiceDate, txError, confirmedImport, confirmedAmount);
    });
    if (!result.ok && errorText) *errorText = mutationErrorOrDefault(result.errorText, request.commitErrorFallback);
    return result.ok;
}

bool updatePartDeliveryDateValue(int partId, const QString& isoDate, const QString& orderUid,
                                 int orderNumber, QString* errorText)
{
    MutationExecutor::Request request;
    request.sourceModule = QStringLiteral("core.services.orders.WorkPartsMutationService");
    request.entityType = QStringLiteral("part");
    request.entityId = partId > 0 ? QString::number(partId) : orderUid;
    request.operation = QStringLiteral("update_part_delivery_installation_dates");
    request.description = QString::fromUtf8("Оновлення дати встановлення/доставки запчастини");
    request.changedTables = {QStringLiteral("work_order_parts"), QStringLiteral("work_orders")};
    request.beginErrorFallback = QString::fromUtf8("Не можу відкрити транзакцію оновлення дати запчастини.");
    request.commitErrorFallback = QString::fromUtf8("Не вдалося завершити оновлення дати запчастини.");

    PartRowSnapshot beforeRow;
    const auto result = execute(request, [&](QSqlDatabase& txDb, QString* txError) {
        beforeRow = PartsQueryService::loadRowById(txDb, partId, nullptr);
        // The checkbox in the order editor represents installation status.
        // Persist installed_at, and only backfill delivery_date when installing
        // a row that has never received a delivery date yet.
        if (!isoDate.trimmed().isEmpty()) {
            QString deliveryError;
            if (!PartMutationService::ensureDeliveryDateIfMissing(txDb, partId, isoDate, &deliveryError)) {
                if (txError) *txError = deliveryError;
                return false;
            }
        }
        return PartMutationService::updateInstalledAt(txDb, orderNumber, partId, isoDate, txError);
    });
    if (!result.ok) {
        if (errorText) *errorText = mutationErrorOrDefault(result.errorText, request.commitErrorFallback);
        return false;
    }
    PartsQueryService::invalidateCache();
    const PartRowSnapshot afterRow = PartsQueryService::loadRowById(partId, nullptr);
    if (RuntimeLoggingControl::deepDebugEnabled()) qInfo().noquote() << QStringLiteral("PART_INSTALLED_CHANGED partId=") << partId
                      << QStringLiteral(" oldInstalled=") << beforeRow.installed
                      << QStringLiteral(" newInstalled=") << afterRow.installed
                      << QStringLiteral(" oldRank=") << beforeRow.overdueRank
                      << QStringLiteral(" newRank=") << afterRow.overdueRank;
    if (beforeRow.deliveryDateRaw.trimmed() != afterRow.deliveryDateRaw.trimmed()) {
        if (RuntimeLoggingControl::deepDebugEnabled()) qInfo().noquote() << QStringLiteral("PART_DELIVERY_CHANGED partId=") << partId
                          << QStringLiteral(" oldDeliveryDate=") << beforeRow.deliveryDateRaw
                          << QStringLiteral(" newDeliveryDate=") << afterRow.deliveryDateRaw
                          << QStringLiteral(" oldRank=") << beforeRow.overdueRank
                          << QStringLiteral(" newRank=") << afterRow.overdueRank;
    }
    return true;
}

bool ensurePartDeliveryDateIfMissing(int partId, const QString& isoDate, QString* errorText)
{
    MutationExecutor::Request request;
    request.sourceModule = QStringLiteral("core.services.orders.WorkPartsMutationService");
    request.entityType = QStringLiteral("part");
    request.entityId = QString::number(partId);
    request.operation = QStringLiteral("ensure_part_delivery_date_if_missing");
    request.description = QString::fromUtf8("Заповнення дати доставки запчастини, якщо вона порожня");
    request.changedTables = {QStringLiteral("work_order_parts")};
    request.beginErrorFallback = QString::fromUtf8("Не можу відкрити транзакцію дати доставки запчастини.");
    request.commitErrorFallback = QString::fromUtf8("Не вдалося завершити оновлення дати доставки запчастини.");

    const auto result = execute(request, [&](QSqlDatabase& txDb, QString* txError) {
        return PartMutationService::ensureDeliveryDateIfMissing(txDb, partId, isoDate, txError);
    });
    if (!result.ok && errorText) *errorText = mutationErrorOrDefault(result.errorText, request.commitErrorFallback);
    return result.ok;
}

bool removePart(int orderNumber, int partId, QString* errorText)
{
    MutationExecutor::Request request;
    request.sourceModule = QStringLiteral("core.services.orders.WorkPartsMutationService");
    request.entityType = QStringLiteral("part");
    request.entityId = QString::number(partId);
    request.operation = QStringLiteral("remove_part");
    request.description = QString::fromUtf8("Видалення рядка запчастини");
    request.changedTables = {QStringLiteral("work_order_parts"), QStringLiteral("work_order_part_attachments"), QStringLiteral("work_orders")};
    request.beginErrorFallback = QString::fromUtf8("Не можу відкрити транзакцію видалення запчастини.");
    request.commitErrorFallback = QString::fromUtf8("Не вдалося завершити видалення запчастини.");

    const auto result = execute(request, [&](QSqlDatabase& txDb, QString* txError) {
        return PartRepository::remove(txDb, orderNumber, partId, txError);
    });
    if (!result.ok && errorText) *errorText = mutationErrorOrDefault(result.errorText, request.commitErrorFallback);
    return result.ok;
}

MutationExecutor::Result execute(const MutationExecutor::Request& request,
                                 const std::function<bool(QSqlDatabase&, QString*)>& body)
{
    QString contextError;
    auto context = DbContext::openDefault(QStringLiteral("WorkPartsMutationService::%1").arg(request.operation), &contextError);
    if (!context.ok) {
        MutationExecutor::Result result;
        result.ok = false;
        result.sourceModule = request.sourceModule;
        result.entityType = request.entityType;
        result.entityId = request.entityId;
        result.operation = request.operation;
        result.changedTables = request.changedTables;
        result.description = request.description;
        result.errorText = mutationErrorOrDefault(contextError, request.beginErrorFallback);
        qCritical().noquote() << QStringLiteral("DB_MUTATION canonical_context_open_failed operation=") << request.operation
                              << QStringLiteral(" contextId=") << context.contextId
                              << QStringLiteral(" connection=") << context.connectionName
                              << QStringLiteral(" error=") << result.errorText;
        return result;
    }
    return MutationExecutor::execute(context.db, request, body);
}


bool runInActiveTransaction(const QString& connectionName,
                            const std::function<bool(QSqlDatabase&, QString*)>& body,
                            QString* errorText)
{
    QString contextError;
    auto context = DbContext::fromConnectionName(connectionName, QStringLiteral("WorkPartsMutationService::active_transaction_step"), &contextError);
    if (!context.ok) {
        const QString detail = mutationErrorOrDefault(contextError, QStringLiteral("Active transaction connection is not available"));
        if (errorText) *errorText = detail;
        qCritical().noquote() << QStringLiteral("DB_MUTATION active_transaction_context_failed connection=") << connectionName
                              << QStringLiteral(" contextId=") << context.contextId
                              << QStringLiteral(" error=") << detail;
        return false;
    }
    if (RuntimeLoggingControl::deepDebugEnabled()) qInfo().noquote() << QStringLiteral("DB_MUTATION active_transaction_step connection=") << context.db.connectionName()
                      << QStringLiteral(" contextId=") << context.contextId;
    return body(context.db, errorText);
}


bool withDatabase(const QString& connectionName,
                  const std::function<bool(QSqlDatabase&, QString*)>& body,
                  QString* errorText)
{
    QString contextError;
    auto context = connectionName.trimmed().isEmpty()
        ? DbContext::openDefault(QStringLiteral("WorkPartsMutationService::deprecated_withDatabase"), &contextError)
        : DbContext::fromConnectionName(connectionName, QStringLiteral("WorkPartsMutationService::deprecated_withDatabase"), &contextError);
    if (!context.ok) {
        const QString detail = mutationErrorOrDefault(contextError, QStringLiteral("Database connection is not available"));
        if (errorText) *errorText = detail;
        qCritical().noquote() << QStringLiteral("DB_MUTATION with_database_context_failed connection=") << context.connectionName
                              << QStringLiteral(" contextId=") << context.contextId
                              << QStringLiteral(" error=") << detail;
        return false;
    }
    if (RuntimeLoggingControl::deepDebugEnabled()) qInfo().noquote() << QStringLiteral("DB_MUTATION with_database_ok connection=") << context.db.connectionName()
                      << QStringLiteral(" contextId=") << context.contextId;
    return body(context.db, errorText);
}

} // namespace WorkPartsMutationService
