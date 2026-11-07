#pragma once

#include <QDialog>
#include "core/model/MasterRow.h"

class QLineEdit;
class QComboBox;
class QDoubleSpinBox;
class QCheckBox;

class MasterEditDialog : public QDialog
{
    Q_OBJECT
public:
    explicit MasterEditDialog(QWidget* parent=nullptr);

    void setData(const MasterRow& m, double payrollPercent);
    MasterRow data() const;
    double payrollPercent() const;

private slots:
    void onRoleChanged();

private:
    QLineEdit* m_name = nullptr;
    QComboBox* m_role = nullptr;
    QDoubleSpinBox* m_adminPercent = nullptr;
    QDoubleSpinBox* m_payrollPercent = nullptr;
    QCheckBox* m_active = nullptr;

    QString m_id;
};
