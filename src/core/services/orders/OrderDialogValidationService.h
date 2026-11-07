#pragma once

#include "core/services/OrderWorkspaceService.h"

#include <QString>
#include <QStringList>

class OrderDialogValidationService
{
public:
    struct ValidationResult {
        bool ok = false;
        QStringList missingFields;
        bool masterMissing = false;
        QString effectiveStatus;
    };

    static ValidationResult validateForDraft(const OrderWorkspaceInput& input);
    static ValidationResult validateForSubmission(const OrderWorkspaceInput& input,
                                                  const QString& requestedStatus,
                                                  double worksSum,
                                                  double partsSum);
    static QString requiredFieldsWarning(const QStringList& missingFields);
};
