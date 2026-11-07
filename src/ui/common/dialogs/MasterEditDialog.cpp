#include "ui/common/dialogs/MasterEditDialog.h"
#include "ui/theme/ThemeManager.h"

#include <QFormLayout>
#include <QLineEdit>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QVBoxLayout>
#include <QUuid>

MasterEditDialog::MasterEditDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QString::fromUtf8("Майстер"));

    const bool darkUi = (ThemeManager::currentTheme() == QStringLiteral("graphite"));
    setStyleSheet(darkUi
        ? QStringLiteral(
            "QDialog{background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 rgba(34,44,56,0.98),stop:1 rgba(18,24,31,0.98));}"
            "QLabel,QCheckBox{color:#f7fbff;}"
            "QLineEdit,QComboBox,QDoubleSpinBox{background:#ffffff;color:#101418;border:1px solid rgba(22,28,35,0.22);border-radius:10px;padding:6px 10px;min-height:34px;}"
            "QLineEdit::placeholder{color:#566171;}"
            "QComboBox QAbstractItemView{background:#ffffff;color:#101418;selection-background-color:#dfe8f5;selection-color:#101418;}"
            "QDialogButtonBox QPushButton{min-height:36px;border-radius:10px;padding:0 16px;background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 rgba(113,129,149,0.98),stop:1 rgba(25,32,40,0.99));color:#f8fbff;border:1px solid rgba(228,236,246,0.34);font-weight:700;}"
            "QDialogButtonBox QPushButton:disabled{background:rgba(160,172,186,0.32);color:#dbe4ef;border:1px solid rgba(228,236,246,0.22);}"
        )
        : QStringLiteral(
            "QDialog{background:#ffffff;}"
            "QLabel,QCheckBox{color:#101418;}"
            "QLineEdit,QComboBox,QDoubleSpinBox{background:#ffffff;color:#101418;border:1px solid #c7ccd4;border-radius:10px;padding:6px 10px;min-height:34px;}"
            "QLineEdit::placeholder{color:#566171;}"
            "QComboBox QAbstractItemView{background:#ffffff;color:#101418;selection-background-color:#dfe8f5;selection-color:#101418;}"
            "QDialogButtonBox QPushButton{min-height:36px;border-radius:10px;padding:0 16px;background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #ffffff,stop:1 #e6e6e6);color:#101418;border:1px solid #bfc3c7;font-weight:700;}"
            "QDialogButtonBox QPushButton:disabled{color:#4b5563;}"
        ));

    auto* root = new QVBoxLayout(this);
    auto* form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignRight);

    m_name = new QLineEdit(this);
    m_role = new QComboBox(this);
    m_role->addItem(QString::fromUtf8("Майстер"), "master");
    m_role->addItem(QString::fromUtf8("Адмін"), "admin");

    m_payrollPercent = new QDoubleSpinBox(this);
    m_payrollPercent->setRange(0, 100);
    m_payrollPercent->setDecimals(2);
    m_payrollPercent->setSuffix(QString::fromUtf8(" %"));

    m_adminPercent = new QDoubleSpinBox(this);
    m_adminPercent->setRange(0, 100);
    m_adminPercent->setDecimals(2);
    m_adminPercent->setSuffix(QString::fromUtf8(" %"));

    m_active = new QCheckBox(QString::fromUtf8("Активний"), this);
    m_active->setChecked(true);

    form->addRow(QString::fromUtf8("Ім'я:"), m_name);
    form->addRow(QString::fromUtf8("Роль:"), m_role);
    form->addRow(QString::fromUtf8("З/п %:"), m_payrollPercent);
    form->addRow(QString::fromUtf8("Адмін %:"), m_adminPercent);
    form->addRow(QString(), m_active);

    root->addLayout(form);

    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    root->addWidget(bb);

    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_role, &QComboBox::currentIndexChanged, this, &MasterEditDialog::onRoleChanged);

    // default
    m_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    onRoleChanged();
}

void MasterEditDialog::setData(const MasterRow& m, double payrollPercent)
{
    m_id = m.id;
    m_name->setText(m.name);
    int idx = m_role->findData(m.role);
    if (idx >= 0) m_role->setCurrentIndex(idx);
    m_adminPercent->setValue(m.adminPercent);
    m_payrollPercent->setValue(payrollPercent);
    m_active->setChecked(m.active);
    onRoleChanged();
}

MasterRow MasterEditDialog::data() const
{
    MasterRow m;
    m.id = m_id;
    m.name = m_name->text().trimmed();
    m.role = m_role->currentData().toString();
    m.adminPercent = m_adminPercent->value();
    m.active = m_active->isChecked();
    return m;
}

double MasterEditDialog::payrollPercent() const
{
    return m_payrollPercent->value();
}

void MasterEditDialog::onRoleChanged()
{
    const QString role = m_role->currentData().toString();
    const bool isAdmin = (role == "admin");
    m_adminPercent->setEnabled(isAdmin);
}
