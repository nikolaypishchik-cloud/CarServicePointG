#include "ui/common/dialogs/OrderDialogSubmitSupport.h"

#include "ui/common/dialogs/OrderDialog.h"
#include "ui/common/dialogs/OrderDialogFlowController.h"
#include "ui/tabs/orders/WorkPartsDialog.h"
#include "app/AppContext.h"
#include "runtime/AppRuntime.h"
#include "core/backup/BackupPerformanceGuard.h"

#include <QApplication>
#include <QLayout>
#include <QMessageBox>
#include <QObject>
#include <QSpinBox>
#include <QTimer>
#include <QWidget>

namespace OrderDialogSubmitSupport {

bool reinitializeWorkPartsPageAfterDraftCreation(WorkPartsDialog*& workPartsPage,
                                                 const std::function<bool(QString*)>& ensureWorkPartsPage,
                                                 QString* warningText)
{
    if (!workPartsPage) return true;

    if (auto* parentLayout = workPartsPage->parentWidget() ? workPartsPage->parentWidget()->layout() : nullptr) {
        parentLayout->removeWidget(workPartsPage);
    }
    workPartsPage->setParent(nullptr);
    workPartsPage->deleteLater();
    workPartsPage = nullptr;

    QString ensureError;
    if (!ensureWorkPartsPage(&ensureError)) {
        if (warningText) *warningText = ensureError;
        return false;
    }
    return true;
}

void applySubmitSuccessState(QObject* target,
                             QSpinBox* orderNumberSpinBox,
                             int& orderNumber,
                             bool& draftCreated,
                             int returnedOrderNumber,
                             int returnedDisplayNumber,
                             int requestedVisibleNumber)
{
    Q_UNUSED(target);
    orderNumber = returnedOrderNumber;
    draftCreated = false;
    OrderDialogFlowController::applyResolvedDisplayNumber(orderNumberSpinBox,
                                                       returnedDisplayNumber,
                                                       requestedVisibleNumber);
}

void handleSubmitSyncFeedback(QWidget* parent,
                              QObject* target,
                              const QString& pendingSyncFile,
                              const QString& syncWarning)
{
    if (!syncWarning.trimmed().isEmpty()) {
        QMessageBox::warning(parent,
                             QString::fromUtf8("Сінхронізація"),
                             QString::fromUtf8("Наряд збережено, але імпорт даних із мобільного пакета завершився з помилкою.\n%1").arg(syncWarning));
        return;
    }

    if (!pendingSyncFile.trimmed().isEmpty()) {
        OrderDialogFlowController::clearPendingSyncProperties(target);
    }
}

void scheduleOpenCreatedOrderEditor(QWidget* parentForEdit, int createdOrderNumber)
{
    QObject* context = parentForEdit ? static_cast<QObject*>(parentForEdit) : static_cast<QObject*>(qApp);
    const QString workspace = AppRuntime::activeDbFilePath();
    QTimer::singleShot(0, context, [parentForEdit, createdOrderNumber, workspace]() {
        // A queued editor belongs to the profile that created the order.
        if (AppRuntime::activeDbFilePath() != workspace) return;
        BackupPerformanceGuard::hold(AppContext::instance().backupManager(), 2500);
        auto* detailDlg = new OrderDialog(parentForEdit, createdOrderNumber);
        detailDlg->setAttribute(Qt::WA_DeleteOnClose, true);
        detailDlg->setWindowTitle(QString::fromUtf8("Редагування наряду"));
        detailDlg->openWorkPartsTab();
        detailDlg->open();
    });
}

} // namespace OrderDialogSubmitSupport
