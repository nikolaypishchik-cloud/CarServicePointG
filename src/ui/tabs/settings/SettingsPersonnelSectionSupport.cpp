#include "ui/tabs/settings/SettingsPersonnelSectionSupport.h"

#include "core/services/settings/PersonnelSettingsService.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHeaderView>
#include <QLineEdit>
#include <QScrollBar>
#include <QTableWidget>
#include <QUuid>

#include <algorithm>

namespace SettingsPersonnelSectionSupport {

void syncMastersTableHeight(QTableWidget* mastersTable)
{
    if (!mastersTable) {
        return;
    }
    const int rowCount = mastersTable->rowCount();
    int targetHeight = mastersTable->frameWidth() * 2;
    if (QHeaderView* hh = mastersTable->horizontalHeader()) {
        targetHeight += hh->isHidden() ? 0 : hh->height();
    }
    if (rowCount <= 0) {
        targetHeight += mastersTable->verticalHeader()->defaultSectionSize() * 2;
    } else {
        for (int row = 0; row < rowCount; ++row) {
            targetHeight += mastersTable->rowHeight(row);
        }
    }
    if (mastersTable->horizontalScrollBar() && mastersTable->horizontalScrollBar()->isVisible()) {
        targetHeight += mastersTable->horizontalScrollBar()->sizeHint().height();
    }
    mastersTable->setFixedHeight(std::max(targetHeight + 2, 120));
}

void renumberMasterRows(QTableWidget* mastersTable)
{
    if (!mastersTable) {
        return;
    }
    for (int r = 0; r < mastersTable->rowCount(); ++r) {
        if (auto* item = mastersTable->item(r, 0)) {
            item->setText(QString::number(r + 1));
        }
    }
}

void appendEmptyMasterRow(QTableWidget* mastersTable)
{
    if (!mastersTable) {
        return;
    }
    const int r = mastersTable->rowCount();
    const QString rowId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    mastersTable->insertRow(r);

    auto* idIt = new QTableWidgetItem(QString::number(r + 1));
    idIt->setData(Qt::UserRole, rowId);
    idIt->setFlags(idIt->flags() & ~Qt::ItemIsEditable);
    mastersTable->setItem(r, 0, idIt);

    auto* nameIt = new QTableWidgetItem(QString());
    nameIt->setData(Qt::UserRole, rowId);
    mastersTable->setItem(r, 1, nameIt);

    auto* pctIt = new QTableWidgetItem(QStringLiteral("0"));
    pctIt->setData(Qt::UserRole, rowId);
    mastersTable->setItem(r, 2, pctIt);

    syncMastersTableHeight(mastersTable);
}

void applySnapshotToUi(QTableWidget* mastersTable,
                       QLineEdit* adminName,
                       QDoubleSpinBox* adminPercent,
                       QComboBox* adminSource,
                       const PersonnelSettingsService::PersonnelSnapshot& snapshot,
                       QString* currentAdminId)
{
    if (!mastersTable || !adminName || !adminPercent || !adminSource || !currentAdminId) {
        return;
    }

    mastersTable->setRowCount(0);
    int visualId = 1;
    for (const auto& row : snapshot.masters) {
        const int r = mastersTable->rowCount();
        mastersTable->insertRow(r);

        auto* idIt = new QTableWidgetItem(QString::number(visualId++));
        idIt->setData(Qt::UserRole, row.id);
        idIt->setFlags(idIt->flags() & ~Qt::ItemIsEditable);
        mastersTable->setItem(r, 0, idIt);

        auto* nameIt = new QTableWidgetItem(row.name);
        nameIt->setData(Qt::UserRole, row.id);
        mastersTable->setItem(r, 1, nameIt);

        auto* pctIt = new QTableWidgetItem(QString::number(row.payrollPercent, 'f', 2));
        pctIt->setData(Qt::UserRole, row.id);
        mastersTable->setItem(r, 2, pctIt);
    }

    *currentAdminId = snapshot.adminId;
    adminName->setText(snapshot.adminName);
    adminPercent->setValue(snapshot.adminPercent);
    const int idx = adminSource->findData(snapshot.adminSource);
    adminSource->setCurrentIndex(idx >= 0 ? idx : 0);
    syncMastersTableHeight(mastersTable);
}

PersonnelSettingsService::PersonnelSavePayload buildSavePayload(const QTableWidget* mastersTable,
                                                               const QLineEdit* adminName,
                                                               const QDoubleSpinBox* adminPercent,
                                                               const QComboBox* adminSource,
                                                               const QString& currentAdminId)
{
    PersonnelSettingsService::PersonnelSavePayload payload;
    if (!mastersTable || !adminName || !adminPercent || !adminSource) {
        return payload;
    }

    for (int r = 0; r < mastersTable->rowCount(); ++r) {
        PersonnelSettingsService::MasterInput input;
        if (const auto* idItem = mastersTable->item(r, 0)) {
            input.id = idItem->data(Qt::UserRole).toString().trimmed();
        }
        if (input.id.isEmpty()) {
            if (const auto* nameItem = mastersTable->item(r, 1)) {
                input.id = nameItem->data(Qt::UserRole).toString().trimmed();
            }
        }
        if (input.id.isEmpty()) {
            if (const auto* pctItem = mastersTable->item(r, 2)) {
                input.id = pctItem->data(Qt::UserRole).toString().trimmed();
            }
        }
        input.name = mastersTable->item(r, 1) ? mastersTable->item(r, 1)->text().trimmed() : QString();
        QString pctText = mastersTable->item(r, 2) ? mastersTable->item(r, 2)->text().trimmed() : QString();
        pctText.replace(',', '.');
        bool okPct = false;
        input.payrollPercent = pctText.toDouble(&okPct);
        if (!okPct) {
            input.payrollPercent = 0.0;
        }
        if (!input.name.isEmpty()) {
            payload.masters.push_back(input);
        }
    }

    payload.admin.currentId = currentAdminId.trimmed();
    payload.admin.name = adminName->text().trimmed();
    payload.admin.adminPercent = adminPercent->value();
    payload.admin.source = adminSource->currentData().toString();
    return payload;
}

} // namespace SettingsPersonnelSectionSupport
