#pragma once

#include "core/services/orders/OrderDialogValidationService.h"
#include "core/services/OrderWorkspaceService.h"

class QWidget;
class QObject;
class QTabWidget;
class QLineEdit;
class QSpinBox;
class QDateEdit;

namespace OrderDialogFlowController {

bool showValidationErrorAndFocus(QWidget* parent,
                                 const OrderDialogValidationService::ValidationResult& validation,
                                 QTabWidget* tabs,
                                 QLineEdit* plate,
                                 QLineEdit* vin,
                                 QSpinBox* odometer,
                                 QDateEdit* openedDate,
                                 const QString& masterMissingMessage);

int resolveDisplayNumber(int returnedDisplayNumber, int requestedVisibleNumber);

void applyResolvedDisplayNumber(QSpinBox* orderNumberSpinBox,
                                int returnedDisplayNumber,
                                int requestedVisibleNumber);

void applyNormalizedDraftFields(const OrderWorkspaceInput& normalizedInput,
                                QLineEdit* plate,
                                QLineEdit* vin,
                                QLineEdit* car,
                                QLineEdit* color,
                                QLineEdit* client);

void clearPendingSyncProperties(QObject* target);

} // namespace OrderDialogFlowController
