#include "core/services/orders/OrderDialogValidationService.h"

#include <QObject>

namespace {
QStringList collectMissingRequiredOrderFields(const OrderWorkspaceInput& input)
{
    QStringList missing;
    if (input.plateNorm.trimmed().isEmpty() && input.plate.trimmed().isEmpty()) {
        missing << QString::fromUtf8("номер авто");
    }
    if (input.vin.trimmed().isEmpty()) {
        missing << QString::fromUtf8("VIN номер");
    }
    if (input.odometer <= 0) {
        missing << QString::fromUtf8("одометр авто");
    }
    if (!input.openedAt.isValid()) {
        missing << QString::fromUtf8("дату та час відкриття");
    }
    return missing;
}

QString resolveEffectiveStatus(const QString& requestedStatus, double worksSum, double partsSum)
{
    const bool hasOrderContent = (worksSum > 0.0001) || (partsSum > 0.0001);
    if (requestedStatus == QStringLiteral("open") && hasOrderContent) {
        return QStringLiteral("in_progress");
    }
    return requestedStatus;
}
}

OrderDialogValidationService::ValidationResult OrderDialogValidationService::validateForDraft(const OrderWorkspaceInput& input)
{
    ValidationResult result;
    result.missingFields = collectMissingRequiredOrderFields(input);
    result.masterMissing = input.masterId.trimmed().isEmpty();
    result.effectiveStatus = input.status;
    result.ok = result.missingFields.isEmpty() && !result.masterMissing;
    return result;
}

OrderDialogValidationService::ValidationResult OrderDialogValidationService::validateForSubmission(const OrderWorkspaceInput& input,
                                                                                                   const QString& requestedStatus,
                                                                                                   double worksSum,
                                                                                                   double partsSum)
{
    ValidationResult result = validateForDraft(input);
    result.effectiveStatus = resolveEffectiveStatus(requestedStatus, worksSum, partsSum);
    result.ok = result.missingFields.isEmpty() && !result.masterMissing;
    return result;
}

QString OrderDialogValidationService::requiredFieldsWarning(const QStringList& missingFields)
{
    if (missingFields.isEmpty()) {
        return QString();
    }
    return QObject::tr("Не введено: %1").arg(missingFields.join(QString::fromUtf8(", ")));
}
