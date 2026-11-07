#include "ui/main/MainWindow.h"
#include "ui/main/ExternalDisplayController.h"

#include "core/services/AccessControlService.h"
#include "core/services/ActivityAuditService.h"
#include "ui/tabs/settings/StaffSessionDialog.h"
#include "ui/common/dialogs/PartRequestsDialog.h"
#include "ui/tabs/orders/CurrentOrdersPage.h"
#include "ui/tabs/parts/PartsPage.h"
#include "ui/tabs/history/HistoryPage.h"
#include "ui/tabs/schedule/PlanningPage.h"

#include <QAbstractButton>
#include <QDialog>
#include <QStackedWidget>
#include <QTimer>

namespace {

QString permissionForPage(int index)
{
    switch (index) {
    case 0: return QStringLiteral("nav.planning");
    case 1: return QStringLiteral("nav.current_orders");
    case 2: return QStringLiteral("nav.settings");
    case 3: return QStringLiteral("nav.history");
    case 4: return QStringLiteral("nav.parts");
    case 5: return QStringLiteral("nav.reports");
    case 6: return QStringLiteral("nav.statistics");
    case 7: return QStringLiteral("nav.catalog");
    case 8: return QStringLiteral("nav.settings");
    case 9: return QStringLiteral("nav.clients");
    case 10: return QStringLiteral("nav.settings");
    case 11: return QStringLiteral("nav.planning");
    case 12: return QStringLiteral("nav.intake");
    case 13: return QStringLiteral("nav.inventory");
    case 14: return QStringLiteral("mobile.pair");
    default: return {};
    }
}

} // namespace

bool MainWindow::hasPermission(const QString& permissionKey) const
{
    if (permissionKey.isEmpty()) return false;

    return AccessControlService::currentUserHasPermission( permissionKey);
}

void MainWindow::refreshAccessControl()
{
    if (m_externalDisplays) m_externalDisplays->stopAll();
    applyAccessControl();
    // Remove the preceding session's role-sensitive content immediately.
    if (m_pageCurrent) m_pageCurrent->refresh();
    if (m_pageParts) m_pageParts->refresh();
    if (m_pageHistory) m_pageHistory->refresh();
    if (m_pagePlanning) m_pagePlanning->resetSessionView();
    if (m_banner) updateUnpaidBanner();
    if (ActivityAuditService::currentActor().isValid()) {
        QTimer::singleShot(0, this, &MainWindow::checkPartRequestNotifications);
    }
}
void MainWindow::openPartRequests()
{
    if (!hasPermission(QStringLiteral("nav.settings"))) return;

    PartRequestsDialog dialog(this);
    connect(&dialog, &PartRequestsDialog::requestOrdered, this, &MainWindow::refreshAll);
    dialog.exec();
    refreshAll();
}
void MainWindow::switchUser()
{
    StaffSessionDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted) return;

    // The phone-pairing page contains role-sensitive data. Recreate it after
    // switching users so a worker never inherits an owner's already built view.
    if (m_stack && m_pagePhoneSync) {
        const bool wasCurrent = (m_stack->currentWidget() == m_pagePhoneSync);
        m_stack->removeWidget(m_pagePhoneSync);
        m_pagePhoneSync->deleteLater();
        m_pagePhoneSync = nullptr;
        m_placeholderPhoneSync = new QWidget(m_stack);
        m_placeholderPhoneSync->setAttribute(Qt::WA_StyledBackground, false);
        m_stack->insertWidget(14, m_placeholderPhoneSync);
        if (wasCurrent && m_stack->count() > 1) m_stack->setCurrentIndex(1);
    }

    refreshAccessControl();
    refreshAll();
}

bool MainWindow::canAccessPage(int idx) const
{
    if (AccessControlService::currentActorIsMaster()
        && !QList<int>{0, 1, 3, 4, 14}.contains(idx)) return false;
    return hasPermission(permissionForPage(idx));
}

void MainWindow::applyAccessControl()
{
    for (auto it = m_accessButtons.cbegin(); it != m_accessButtons.cend(); ++it) {
        if (!it.value()) continue;
        bool allowed = hasPermission(it.key());
        if (it.key().startsWith(QStringLiteral("orders.tab."))) {
            allowed = canAccessPage(it.key().mid(11).toInt());
        } else if (it.key() == QStringLiteral("nav.current_orders")) {
            allowed = canAccessPage(1) || canAccessPage(3) || canAccessPage(15);
        }
        it.value()->setVisible(allowed);
        it.value()->setEnabled(allowed);
    }

    // The sidebar reserves one slot for account-specific configuration.
    // Users allowed to open Settings use that slot; self phone pairing is
    // deliberately exposed only when Settings is unavailable to the user.
    auto* settingsButton = m_accessButtons.value(QStringLiteral("nav.settings"));
    auto* phoneSyncButton = m_accessButtons.value(QStringLiteral("mobile.pair"));
    const bool canOpenSettings = hasPermission(QStringLiteral("nav.settings"));
    const bool canPairPhone = hasPermission(QStringLiteral("mobile.pair"));
    if (settingsButton) {
        settingsButton->setVisible(canOpenSettings);
        settingsButton->setEnabled(canOpenSettings);
    }
    if (phoneSyncButton) {
        const bool showPhoneSync = !canOpenSettings && canPairPhone;
        phoneSyncButton->setVisible(showPhoneSync);
        phoneSyncButton->setEnabled(showPhoneSync);
    }
    if (!m_stack) return;
    if (canAccessPage(m_stack->currentIndex())) {
        m_stack->setVisible(true);
        return;
    }

    const QList<int> candidates = {1, 0, 3, 5, 9, 12, 13, 14, 4, 6, 7, 8};
    for (const int pageIndex : candidates) {
        if (!canAccessPage(pageIndex)) continue;
        m_stack->setCurrentIndex(pageIndex);
        updateTopTitleForPage(pageIndex);
        m_stack->setVisible(true);
        return;
    }
    m_stack->setVisible(false);
}
