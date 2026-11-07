#include "ui/tabs/crm/CrmPage.h"

#include <QTableWidget>
#include <QTableWidgetItem>

QString CrmPage::selectedClientUid() const
{
    return m_currentClientUid.trimmed();
}

QString CrmPage::selectedVehicleUid() const
{
    return m_currentVehicleUid.trimmed();
}

QString CrmPage::selectedTaskUid() const
{
    if (!m_tasks || m_tasks->currentRow() < 0 || !m_tasks->item(m_tasks->currentRow(), 0)) return QString();
    return m_tasks->item(m_tasks->currentRow(), 0)->data(Qt::UserRole).toString().trimmed();
}

QString CrmPage::clientUidForItem(const QTableWidgetItem* item) const
{
    if (!item) return QString();
    return item->data(Qt::UserRole).toString().trimmed();
}

QString CrmPage::clientUidForRow(int row) const
{
    if (!m_clients || row < 0 || row >= m_clients->rowCount()) return QString();
    return clientUidForItem(m_clients->item(row, 0));
}
