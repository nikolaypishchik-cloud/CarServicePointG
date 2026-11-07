#pragma once

#include <functional>
#include <QString>

class QObject;
class QWidget;
class QSpinBox;
class WorkPartsDialog;

namespace OrderDialogSubmitSupport {

bool reinitializeWorkPartsPageAfterDraftCreation(WorkPartsDialog*& workPartsPage,
                                                 const std::function<bool(QString*)>& ensureWorkPartsPage,
                                                 QString* warningText = nullptr);

void applySubmitSuccessState(QObject* target,
                             QSpinBox* orderNumberSpinBox,
                             int& orderNumber,
                             bool& draftCreated,
                             int returnedOrderNumber,
                             int returnedDisplayNumber,
                             int requestedVisibleNumber);

void handleSubmitSyncFeedback(QWidget* parent,
                              QObject* target,
                              const QString& pendingSyncFile,
                              const QString& syncWarning);

void scheduleOpenCreatedOrderEditor(QWidget* parentForEdit, int createdOrderNumber);

} // namespace OrderDialogSubmitSupport
