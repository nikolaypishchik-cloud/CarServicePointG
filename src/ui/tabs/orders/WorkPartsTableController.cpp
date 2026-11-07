// P2 architecture migration: implementation module converted from legacy .ipp implementation-fragment decomposition.
// Compiled as a separate P2 implementation module; include-order dependencies are isolated in the module prelude.

// extracted from WorkPartsDialog.cpp as part of p.4 decomposition
#include "ui/tabs/orders/WorkPartsDialogModulePrelude.h"

void WorkPartsDialog::repositionInputRows()
{
    if (m_servicesTable) {
        const QSignalBlocker blocker(m_servicesTable);
        removeViewportFillRows(m_servicesTable);
        for (int row = m_servicesTable->rowCount() - 1; row >= 0; --row) {
            if (rowHasHintMarker(m_servicesTable, row)) m_servicesTable->removeRow(row);
        }
        const int insertRow = m_servicesTable->rowCount();
        insertEmptyServiceRow(insertRow);
        renumberServiceItems();
        fillTableViewportWithRows(m_servicesTable, true);
        renumberServiceItems();
    }

    if (m_partsTable) {
        const QSignalBlocker blocker(m_partsTable);
        removeViewportFillRows(m_partsTable);
        for (int row = m_partsTable->rowCount() - 1; row >= 0; --row) {
            if (rowHasHintMarker(m_partsTable, row)) m_partsTable->removeRow(row);
        }
        int insertRow = 0;
        for (int row = 0; row < m_partsTable->rowCount(); ++row) {
            if (rowHasMeaningfulPartData(m_partsTable, row)) insertRow = row + 1;
        }
        insertEmptyPartRow(insertRow);
        renumberPartItems();
        fillTableViewportWithRows(m_partsTable, true);
        renumberPartItems();
        updateAllMainPartsRowHeights(m_partsTable);
    }
}
