#include "ui/common/dialogs/OrderDialogFlowController.h"

#include <QDateEdit>
#include <QLineEdit>
#include <QMessageBox>
#include <QSpinBox>
#include <QTabWidget>
#include <QWidget>
#include <QObject>

namespace OrderDialogFlowController {

bool showValidationErrorAndFocus(QWidget* parent,
                                 const OrderDialogValidationService::ValidationResult& validation,
                                 QTabWidget* tabs,
                                 QLineEdit* plate,
                                 QLineEdit* vin,
                                 QSpinBox* odometer,
                                 QDateEdit* openedDate,
                                 const QString& masterMissingMessage)
{
    if (!validation.missingFields.isEmpty()) {
        QMessageBox::warning(parent,
                             QString::fromUtf8("Обов'язкові поля не заповнені"),
                             OrderDialogValidationService::requiredFieldsWarning(validation.missingFields));
        if (tabs) tabs->setCurrentIndex(0);
        if (plate && plate->text().trimmed().isEmpty()) plate->setFocus();
        else if (vin && vin->text().trimmed().isEmpty()) vin->setFocus();
        else if (odometer && odometer->value() <= 0) odometer->setFocus();
        else if (openedDate) openedDate->setFocus();
        return true;
    }

    if (validation.masterMissing) {
        QMessageBox::warning(parent, QString::fromUtf8("Помилка"), masterMissingMessage);
        return true;
    }

    return false;
}

int resolveDisplayNumber(int returnedDisplayNumber, int requestedVisibleNumber)
{
    return returnedDisplayNumber > 0 ? returnedDisplayNumber : requestedVisibleNumber;
}

void applyResolvedDisplayNumber(QSpinBox* orderNumberSpinBox,
                                int returnedDisplayNumber,
                                int requestedVisibleNumber)
{
    if (!orderNumberSpinBox) return;
    const int displayNumber = resolveDisplayNumber(returnedDisplayNumber, requestedVisibleNumber);
    orderNumberSpinBox->setValue(displayNumber > 0 ? displayNumber : requestedVisibleNumber);
    orderNumberSpinBox->setEnabled(false);
}

void applyNormalizedDraftFields(const OrderWorkspaceInput& normalizedInput,
                                QLineEdit* plate,
                                QLineEdit* vin,
                                QLineEdit* car,
                                QLineEdit* color,
                                QLineEdit* client)
{
    if (plate) plate->setText(normalizedInput.plate);
    if (vin) vin->setText(normalizedInput.vin);
    if (car) car->setText(normalizedInput.car);
    if (color) color->setText(normalizedInput.color);
    if (client) client->setText(normalizedInput.client);
}

void clearPendingSyncProperties(QObject* target)
{
    if (!target) return;
    target->setProperty("sync.pending_source_file", QString());
    target->setProperty("sync.pending_parts_count", 0);
    target->setProperty("sync.pending_has_payload", false);
}

} // namespace OrderDialogFlowController
