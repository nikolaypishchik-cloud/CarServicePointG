#pragma once

#include <QString>

class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QTableWidget;

namespace PersonnelSettingsService {
struct PersonnelSavePayload;
struct PersonnelSnapshot;
}

namespace SettingsPersonnelSectionSupport {

void applySnapshotToUi(QTableWidget* mastersTable,
                       QLineEdit* adminName,
                       QDoubleSpinBox* adminPercent,
                       QComboBox* adminSource,
                       const PersonnelSettingsService::PersonnelSnapshot& snapshot,
                       QString* currentAdminId);

PersonnelSettingsService::PersonnelSavePayload buildSavePayload(const QTableWidget* mastersTable,
                                                               const QLineEdit* adminName,
                                                               const QDoubleSpinBox* adminPercent,
                                                               const QComboBox* adminSource,
                                                               const QString& currentAdminId);

void appendEmptyMasterRow(QTableWidget* mastersTable);
void renumberMasterRows(QTableWidget* mastersTable);
void syncMastersTableHeight(QTableWidget* mastersTable);

} // namespace SettingsPersonnelSectionSupport
