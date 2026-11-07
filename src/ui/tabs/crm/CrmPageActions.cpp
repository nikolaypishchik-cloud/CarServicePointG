#include "ui/tabs/crm/CrmPage.h"
#include "ui/main/MainWindow.h"
#include "core/crm/CrmTaskService.h"
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QDateTime>
#include <QMetaObject>
#include <QVariant>
#include <QUuid>
#include <QScopeGuard>

void CrmPage::addClientTask()
{
    if (selectedClientUid().isEmpty()) return;
    if (property("creatingClientTask").toBool()) return;
    setProperty("creatingClientTask", true);
    const auto release = qScopeGuard([this] { setProperty("creatingClientTask", false); });
    const auto requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    bool ok = false;
    const QString title = QInputDialog::getText(this, QString::fromUtf8("CRM задача"), QString::fromUtf8("Назва задачі:"), QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok || title.isEmpty()) return;
    const QString dueAt = QInputDialog::getText(this,
                                                QString::fromUtf8("CRM задача"),
                                                QString::fromUtf8("Термін ДД.ММ.РР ГГ:ХХ (можна залишити порожнім):"),
                                                QLineEdit::Normal,
                                                QDateTime::currentDateTime().addDays(1).toString(QStringLiteral("dd.MM.yy HH:mm")),
                                                &ok).trimmed();
    if (!ok) return;
    QString err;
    if (!CrmTaskService::createTaskWithTimeline(selectedClientUid(), selectedVehicleUid(), title, dueAt, &err, {}, requestId)) {
        QMessageBox::warning(this, QString::fromUtf8("CRM"), err);
        return;
    }
    loadClientDetails(selectedClientUid());
}

void CrmPage::markSelectedTaskCompleted()
{
    const QString taskUid = selectedTaskUid();
    if (taskUid.isEmpty()) return;
    QString err;
    if (!CrmTaskService::markCompleted({}, taskUid, &err)) {
        QMessageBox::warning(this, QString::fromUtf8("CRM"), err);
        return;
    }
    loadClientDetails(selectedClientUid());
}

void CrmPage::requestNewOrder()
{
    if (selectedVehicleUid().isEmpty()) {
        QMessageBox::information(this, QString::fromUtf8("CRM"), QString::fromUtf8("Спочатку виберіть автомобіль."));
        return;
    }
    if (auto* main = qobject_cast<MainWindow*>(window()))
        main->requestOpenNewOrder(0, selectedClientUid(), selectedVehicleUid());
}

void CrmPage::requestRepairBookingTask()
{
    if (selectedClientUid().isEmpty()) {
        QMessageBox::information(this, QString::fromUtf8("CRM"), QString::fromUtf8("Спочатку виберіть клієнта."));
        return;
    }
    QMetaObject::invokeMethod(window(), "gotoPage", Qt::QueuedConnection, Q_ARG(int, 0));
}

