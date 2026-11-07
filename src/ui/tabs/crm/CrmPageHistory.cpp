#include "ui/tabs/crm/CrmPage.h"

#include "ui/tabs/orders/CurrentOrdersCoordinator.h"

#include <QDebug>
#include <QMessageBox>
#include <QRegularExpression>
#include <QTableWidget>
#include <QTableWidgetItem>

namespace {

constexpr int kCrmHistoryOrderNumberRole = Qt::UserRole + 1;

int parseOrderNumberText(const QString& text)
{
    bool ok = false;
    const int direct = text.trimmed().toInt(&ok);
    if (ok && direct > 0) return direct;

    const QRegularExpressionMatch match = QRegularExpression(QStringLiteral("(\\d+)")).match(text);
    if (!match.hasMatch()) return 0;
    const int parsed = match.captured(1).toInt(&ok);
    return ok && parsed > 0 ? parsed : 0;
}

} // namespace

QString CrmPage::historyOrderUidForRow(int row) const
{
    if (!m_history || row < 0 || row >= m_history->rowCount()) return QString();
    for (int column = 0; column < m_history->columnCount(); ++column) {
        if (const auto* item = m_history->item(row, column)) {
            const QString uid = item->data(Qt::UserRole).toString().trimmed();
            if (!uid.isEmpty()) return uid;
        }
    }
    return QString();
}

int CrmPage::historyOrderNumberForRow(int row) const
{
    if (!m_history || row < 0 || row >= m_history->rowCount()) return 0;
    for (int column = 0; column < m_history->columnCount(); ++column) {
        if (const auto* item = m_history->item(row, column)) {
            bool ok = false;
            const int roleNumber = item->data(kCrmHistoryOrderNumberRole).toInt(&ok);
            if (ok && roleNumber > 0) return roleNumber;
        }
    }
    return m_history->item(row, 0) ? parseOrderNumberText(m_history->item(row, 0)->text()) : 0;
}

void CrmPage::openHistoryOrderRow(int row)
{
    if (!m_history || row < 0 || row >= m_history->rowCount()) return;
    m_history->selectRow(row);

    const QString clientUidBefore = selectedClientUid();
    const QString orderUid = historyOrderUidForRow(row);
    const int orderNumber = historyOrderNumberForRow(row);
    const QString orderForLog = !orderUid.isEmpty()
        ? orderUid
        : (orderNumber > 0 ? QString::number(orderNumber) : QStringLiteral("<missing>"));

    qInfo().noquote() << QStringLiteral("CRM_HISTORY_DBLCLICK row=%1 order=%2").arg(row).arg(orderForLog);

    if (orderUid.isEmpty() && orderNumber <= 0) {
        const QString reason = QStringLiteral("missing_order_identity");
        qWarning().noquote() << QStringLiteral("CRM_HISTORY_OPEN_ORDER_FAIL order=%1 reason=%2").arg(orderForLog, reason);
        QMessageBox::warning(this, QString::fromUtf8("Наряд"),
                             QString::fromUtf8("Не вдалося визначити наряд для цього рядка."));
        return;
    }

    const auto result = CurrentOrdersCoordinator::openExistingOrderEditor(this, orderNumber, orderUid);
    if (!result.launched) {
        const QString reason = result.errorText.trimmed().isEmpty() ? QStringLiteral("open_failed") : result.errorText.trimmed();
        qWarning().noquote() << QStringLiteral("CRM_HISTORY_OPEN_ORDER_FAIL order=%1 reason=%2").arg(orderForLog, reason);
        QMessageBox::warning(this, QString::fromUtf8("Наряд"), reason);
        return;
    }

    qInfo().noquote() << QStringLiteral("CRM_HISTORY_OPEN_ORDER_OK order=%1").arg(result.orderNumber > 0 ? QString::number(result.orderNumber) : orderForLog);
    if (result.accepted && !clientUidBefore.isEmpty()) {
        selectClientByUid(clientUidBefore, SelectionReason::RefreshRestore, false);
    }
}
