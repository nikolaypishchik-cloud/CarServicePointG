#include "ui/tabs/settings/AccessControlPage.h"
#include "ui/tabs/settings/SettingsDetailUiSupport.h"

#include "core/services/AccessControlService.h"

#include <QCheckBox>
#include <QComboBox>
#include <QListView>
#include <QStyledItemDelegate>
#include <QStylePainter>
#include <QStyleOptionComboBox>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QInputDialog>
#include <QApplication>
#include <QStyle>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTabWidget>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include <algorithm>

namespace {

// Keep the selected role and arrow readable under both native Windows and
// application styles, including large fonts in table cells.
class AccessRoleComboBox final : public QComboBox {
public:
    explicit AccessRoleComboBox(QWidget* parent) : QComboBox(parent) {
        setSizeAdjustPolicy(QComboBox::AdjustToContents);
        setView(new QListView(this));
        setItemDelegate(new QStyledItemDelegate(this));
        view()->setStyleSheet("QListView {background:white;color:black;selection-background-color:#dfe8f5;selection-color:black;}"
                             "QListView::item {padding:5px 10px;}");
    }
    QSize sizeHint() const override {
        QSize size=QComboBox::sizeHint();
        int textWidth=0;
        for(int i=0;i<count();++i)textWidth=qMax(textWidth,fontMetrics().horizontalAdvance(itemText(i)));
        size.setWidth(qMax(size.width(),textWidth+60));
        return size;
    }
    QSize minimumSizeHint() const override { return sizeHint(); }
protected:
    void paintEvent(QPaintEvent*) override {
        QStylePainter painter(this);QStyleOptionComboBox option;initStyleOption(&option);
        painter.drawComplexControl(QStyle::CC_ComboBox,option);
        const QRect textRect=style()->subControlRect(QStyle::CC_ComboBox,&option,QStyle::SC_ComboBoxEditField,this);
        painter.setPen(isEnabled()?QColor(Qt::black):QColor("#505862"));
        painter.drawText(textRect,Qt::AlignLeft|Qt::AlignVCenter,fontMetrics().elidedText(currentText(),Qt::ElideRight,textRect.width()));
        const QRect arrow=style()->subControlRect(QStyle::CC_ComboBox,&option,QStyle::SC_ComboBoxArrow,this);
        const QPoint center=arrow.center();
        painter.setBrush(painter.pen().color());painter.setPen(Qt::NoPen);
        painter.drawPolygon(QPolygon({center+QPoint(-4,-2),center+QPoint(4,-2),center+QPoint(0,3)}));
    }
};

QString dateTimeText(const QDateTime& value)
{
    return value.isValid() ? value.toString(QStringLiteral("dd.MM.yy HH:mm")) : QString::fromUtf8("Ще не входив");
}

QTableWidgetItem* readOnlyItem(const QString& text, const QVariant& data = {})
{
    auto* item = new QTableWidgetItem(text);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    if (data.isValid()) item->setData(Qt::UserRole, data);
    return item;
}

class CenteredCellControl final : public QWidget
{
public:
    explicit CenteredCellControl(QWidget* control, QWidget* parent = nullptr)
        : QWidget(parent)
        , m_control(control)
    {
        m_control->setParent(this);
        setObjectName(QStringLiteral("accessCellContainer"));
        setStyleSheet(QStringLiteral("QWidget#accessCellContainer{background:transparent;border:0;padding:0;margin:0;}"));
        m_control->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(8, 6, 8, 6);
        layout->addWidget(m_control);
    }

    QSize sizeHint() const override
    {
        if (!m_control) return QWidget::sizeHint();

        const QSize controlHint = m_control->sizeHint().expandedTo(m_control->minimumSize());
        // Include the table item's theme padding as well as the inner layout margins.
        return controlHint + QSize(32, 28);
    }

    QSize minimumSizeHint() const override
    {
        return sizeHint();
    }

protected:
    void resizeEvent(QResizeEvent* event) override
    {
        QWidget::resizeEvent(event);
        if (layout()) layout()->activate();
    }

private:
    QWidget* m_control = nullptr;
};

} // namespace

AccessControlPage::AccessControlPage(QWidget* parent)
    : QWidget(parent)
{
    buildUi();
    reloadAll();
}

void AccessControlPage::buildUi()
{
    setObjectName(QStringLiteral("accessControlPage"));
    setProperty("settings_detail_inline_title", QString::fromUtf8("Користувачі, ролі й права доступу"));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setStyleSheet(QStringLiteral(
        "#accessControlPage{background:transparent;}"
        "#accessControlPage QLabel{color:#f3f7fb;}"
        "#accessControlPage QLineEdit,#accessControlPage QComboBox{background:#ffffff;color:#14202a;border:1px solid #b8c7d4;border-radius:4px;padding:6px 8px;}"
        "#accessControlPage QPushButton{background:#253f56;color:#ffffff;border:1px solid #adc1d1;border-radius:4px;padding:7px 13px;font-weight:700;}"
        "#accessControlPage QPushButton:hover{background:#315673;}"
        "#accessControlPage QPushButton:disabled{background:#5c6975;color:#d1d9df;border-color:#788590;}"
        "#accessControlPage QTabWidget::pane{border:none;background:transparent;}"
        "#accessControlPage QTabBar::tab{color:#f5f8fb;padding:9px 15px;}"
        "#accessControlPage QTabBar::tab:selected{color:#17212b;background:#eef4f9;border:1px solid #ffffff;font-weight:800;}"
        "#accessControlPage QFrame#accessSection{background:#2a3b4b;border:1px solid #71889a;border-radius:5px;}"));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(18, 16, 18, 16);
    root->setSpacing(8);


    auto* hint = new QLabel(
        QString::fromUtf8("Модель доступу працює як у Windows: обліковий запис отримує роль, а роль має конкретний набір дозволів. "
                          "Персональні PIN не відображаються та не зберігаються у відкритому вигляді; власник може лише скинути PIN."),
        this);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color:#d2e0eb;font-size:10pt;"));
    root->addWidget(hint);

    auto* tabs = new QTabWidget(this);
    tabs->setDocumentMode(true);
    tabs->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    auto* usersTab = new QWidget(tabs);
    auto* usersLayout = new QVBoxLayout(usersTab);
    usersLayout->setContentsMargins(0, 10, 0, 0);
    usersLayout->setSpacing(8);
    auto* usersInfo = new QLabel(
        QString::fromUtf8("Обліковими записами працівників керуйте на цій вкладці. "
                          "Після скидання PIN він встановлює новий при наступному вході."),
        usersTab);
    usersInfo->setWordWrap(true);
    usersInfo->setStyleSheet(QStringLiteral("color:#d7e4ee;"));
    usersLayout->addWidget(usersInfo);

    m_accountsTable = new QTableWidget(usersTab);
    m_accountsTable->setObjectName(QStringLiteral("accessAccountsTable"));
    m_accountsTable->setColumnCount(5);
    m_accountsTable->setHorizontalHeaderLabels({QString::fromUtf8("Працівник"), QString::fromUtf8("Роль"),
                                                 QStringLiteral("PIN"), QString::fromUtf8("Останній вхід"), QString::fromUtf8("Дії")});
    m_accountsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_accountsTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_accountsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_accountsTable->verticalHeader()->setVisible(false);
    m_accountsTable->verticalHeader()->setDefaultSectionSize(38);
    m_accountsTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_accountsTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_accountsTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_accountsTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_accountsTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_accountsTable->setStyleSheet(QStringLiteral(
        "QTableWidget{background:#fbfdff;alternate-background-color:#edf3f8;color:#14202a;border:1px solid #b9c8d5;gridline-color:#d3dde7;}"
        "QHeaderView::section{background:#dce7f0;color:#172431;border:none;border-right:1px solid #bdcad5;padding:7px;font-weight:700;}"));
    usersLayout->addWidget(m_accountsTable, 1);

    auto* userActions = new QHBoxLayout();
    auto* addAccount=new QPushButton(QString::fromUtf8("Додати працівника"),usersTab);
    addAccount->setObjectName("addStaffAccount");userActions->addWidget(addAccount);
    auto* removeAccount=new QPushButton(QString::fromUtf8("Видалити працівника"),usersTab);
    removeAccount->setObjectName("removeStaffAccount");userActions->addWidget(removeAccount);
    connect(addAccount,&QPushButton::clicked,this,[this] {
        if(!m_canManage)return;
        QInputDialog dialog(this);
        dialog.setWindowTitle(QString::fromUtf8("Новий працівник"));
        dialog.setLabelText(QString::fromUtf8("Ім’я"));
        dialog.setTextEchoMode(QLineEdit::Normal);
        dialog.ensurePolished();
        // ThemeManager supplies the shared 90 mm dialog width and native title.
        // An editor sized for the old application-name suffix would overflow it.
        if(dialog.exec()!=QDialog::Accepted)return;
        const auto name=dialog.textValue();QString error;
        if(!AccessControlService::saveAccount({},name,"master",&error)){setStatus(error,true);return;}
        reloadAccounts();setStatus(QString::fromUtf8("Працівника додано. Оберіть його роль та збережіть."));
    });
    connect(removeAccount,&QPushButton::clicked,this,[this] {
        const auto row=m_accountsTable->currentRow();if(!m_canManage || row<0 || row>=m_accounts.size())return;
        const auto account=m_accounts.at(row);
        if(QMessageBox::question(this,QString::fromUtf8("Видалити працівника"),QString::fromUtf8("Видалити «%1» з активного обліку? Історія робіт збережеться.").arg(account.displayName))!=QMessageBox::Yes)return;
        QString error;if(!AccessControlService::deactivateAccount(account.id,&error)){setStatus(error,true);return;}
        reloadAccounts();setStatus(QString::fromUtf8("Працівника видалено з активного обліку."));
    });
    userActions->addStretch(1);
    m_saveAccountsButton = new QPushButton(QString::fromUtf8("Зберегти працівників"), usersTab);
    userActions->addWidget(m_saveAccountsButton);
    usersLayout->addLayout(userActions);
    tabs->addTab(usersTab, QString::fromUtf8("Користувачі"));

    auto* rolesTab = new QWidget(tabs);
    rolesTab->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto* rolesLayout = new QVBoxLayout(rolesTab);
    rolesLayout->setContentsMargins(0, 10, 0, 0);
    rolesLayout->setSpacing(6);

    auto* roleHead = new QFrame(rolesTab);
    roleHead->setObjectName(QStringLiteral("accessSection"));
    auto* roleHeadLayout = new QVBoxLayout(roleHead);
    roleHeadLayout->setContentsMargins(12, 10, 12, 10);
    roleHeadLayout->setSpacing(0);
    auto* roleSelectorRow = new QHBoxLayout();
    roleSelectorRow->addWidget(new QLabel(QString::fromUtf8("Роль:"), roleHead));
    m_roleSelector = new QComboBox(roleHead);
    m_roleSelector->setMinimumWidth(250);
    roleSelectorRow->addWidget(m_roleSelector);
    m_deleteRoleButton = new QPushButton(QString::fromUtf8("Видалити роль"), roleHead);
    m_deleteRoleButton->setProperty("access_role_compact_action", true);
    roleSelectorRow->addWidget(m_deleteRoleButton);
    roleSelectorRow->addSpacing(30);
    m_roleDescription = new QLabel(roleHead);
    m_roleDescription->setWordWrap(false);
    m_roleDescription->setMinimumWidth(0);
    m_roleDescription->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_roleDescription->setStyleSheet(QStringLiteral("color:#d5e3ed;"));
    roleSelectorRow->addWidget(m_roleDescription, 1);
    roleHeadLayout->addLayout(roleSelectorRow);
    rolesLayout->addWidget(roleHead);

    auto* customRole = new QFrame(rolesTab);
    customRole->setObjectName(QStringLiteral("accessSection"));
    auto* customForm = new QFormLayout(customRole);
    customForm->setContentsMargins(12, 8, 12, 8);
    customForm->setHorizontalSpacing(10);
    m_newRoleName = new QLineEdit(customRole);
    m_newRoleName->setPlaceholderText(QString::fromUtf8("Наприклад: Приймальник"));
    m_newRoleDescription = new QLineEdit(customRole);
    m_newRoleDescription->setPlaceholderText(QString::fromUtf8("Короткий опис ролі"));
    auto* createRoleButton = new QPushButton(QString::fromUtf8("Створити роль"), customRole);
    createRoleButton->setProperty("access_role_compact_action", true);
    customForm->addRow(QString::fromUtf8("Нова роль:"), m_newRoleName);
    customForm->addRow(QString::fromUtf8("Опис:"), m_newRoleDescription);
    customForm->addRow(QString(), createRoleButton);
    rolesLayout->addWidget(customRole);

    m_permissionsTree = new QTreeWidget(rolesTab);
    m_permissionsTree->setColumnCount(2);
    m_permissionsTree->setHeaderLabels({QString::fromUtf8("Функція застосунку"), QString::fromUtf8("Дозволено")});
    m_permissionsTree->setRootIsDecorated(true);
    m_permissionsTree->setIndentation(18);
    m_permissionsTree->setUniformRowHeights(false);
    m_permissionsTree->setSelectionMode(QAbstractItemView::NoSelection);
    m_permissionsTree->setMinimumHeight(540);
    m_permissionsTree->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_permissionsTree->header()->setMinimumHeight(32);
    m_permissionsTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_permissionsTree->header()->setSectionResizeMode(1, QHeaderView::Fixed);
    m_permissionsTree->header()->resizeSection(1, 150);
    m_permissionsTree->setStyleSheet(QStringLiteral(
        "QTreeWidget{background:#fbfdff;color:#14202a;border:1px solid #b9c8d5;}"
        "QTreeWidget::item{min-height:34px;padding:3px 10px;border-bottom:1px solid #e3eaf0;}"
        "QTreeWidget::item:hover{background:#edf3f8;}"
        "QTreeWidget QCheckBox{color:#14202a;padding:2px 5px;font-weight:700;}"
        "QTreeWidget QCheckBox::indicator{width:19px;height:19px;border:2px solid #315d7e;border-radius:3px;background:#ffffff;}"
        "QTreeWidget QCheckBox::indicator:checked{background:#23834c;border-color:#176638;}"
        "QTreeWidget QCheckBox::indicator:disabled{background:#eef3f6;border-color:#91a7b8;}"
        "QTreeWidget QCheckBox::indicator:checked:disabled{background:#48a96c;border-color:#28734a;}"
        "QHeaderView::section{background:#dce7f0;color:#172431;border:none;padding:7px;font-weight:700;}"));
    rolesLayout->addWidget(m_permissionsTree, 1);

    auto* roleActions = new QHBoxLayout();
    roleActions->addStretch(1);
    m_savePermissionsButton = new QPushButton(QString::fromUtf8("Зберегти права ролі"), rolesTab);
    roleActions->addWidget(m_savePermissionsButton);
    rolesLayout->addLayout(roleActions);
    rolesLayout->setStretch(2, 1);
    tabs->addTab(rolesTab, QString::fromUtf8("Ролі та права"));

    root->addWidget(tabs, 1);
    m_status = new QLabel(this);
    m_status->setMinimumHeight(22);
    m_status->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    m_status->setWordWrap(true);
    root->addWidget(m_status);

    connect(m_saveAccountsButton, &QPushButton::clicked, this, &AccessControlPage::saveAccountRoles);
    connect(m_roleSelector, qOverload<int>(&QComboBox::currentIndexChanged), this, &AccessControlPage::loadRolePermissions);
    connect(createRoleButton, &QPushButton::clicked, this, &AccessControlPage::createRole);
    connect(m_deleteRoleButton, &QPushButton::clicked, this, &AccessControlPage::deleteSelectedRole);
    connect(m_savePermissionsButton, &QPushButton::clicked, this, &AccessControlPage::saveRolePermissions);
}

void AccessControlPage::reloadAll(const QString& preferredRoleId)
{
    QString errorText;
    m_canManage = AccessControlService::currentUserHasPermission(QStringLiteral("access.manage"));
    if (!m_canManage) {
        setStatus(QString::fromUtf8("Недостатньо прав: керувати ролями й доступом може лише власник."), true);
    }
    reloadRoles(preferredRoleId);
    reloadAccounts();
    m_saveAccountsButton->setEnabled(m_canManage);
    findChild<QPushButton*>("addStaffAccount")->setEnabled(m_canManage);
    findChild<QPushButton*>("removeStaffAccount")->setEnabled(m_canManage);
    // reloadRoles/loadRolePermissions also account for built-in roles and read errors.
    m_newRoleName->setEnabled(m_canManage);
    m_newRoleDescription->setEnabled(m_canManage);
    Q_UNUSED(errorText);
}

void AccessControlPage::reloadAccounts()
{
    QString errorText;
    m_accounts = AccessControlService::accounts(&errorText);
    if (!errorText.isEmpty()) {
        m_accountsTable->setRowCount(0);
        setStatus(errorText, true);
        return;
    }

    QSignalBlocker blocker(m_accountsTable);
    m_accountsTable->setRowCount(m_accounts.size());
    for (int row = 0; row < m_accounts.size(); ++row) {
        const AccessAccountRow& account = m_accounts.at(row);
        m_accountsTable->setItem(row, 0, readOnlyItem(QString(), account.id));
        auto* name=new QLineEdit(account.displayName,m_accountsTable);name->setEnabled(m_canManage);
        m_accountsTable->setCellWidget(row,0,new CenteredCellControl(name,m_accountsTable));
        auto* role = new AccessRoleComboBox(m_accountsTable);
        for (const AccessRoleDefinition& definition : m_roles) {
            role->addItem(definition.title, definition.id);
        }
        const int roleIndex = role->findData(account.roleId);
        role->setCurrentIndex(roleIndex >= 0 ? roleIndex : 0);
        role->setEnabled(m_canManage && account.id != QStringLiteral("admin"));
        m_accountsTable->setCellWidget(row, 1, new CenteredCellControl(role, m_accountsTable));
        const QString pinState = account.pinConfigured
            ? QString::fromUtf8("Встановлено")
            : QString::fromUtf8("Очікує створення");
        m_accountsTable->setItem(row, 2, readOnlyItem(pinState));
        m_accountsTable->setItem(row, 3, readOnlyItem(dateTimeText(account.lastLoginAt)));
        auto* reset = new QPushButton(QString::fromUtf8("Скинути PIN"), m_accountsTable);
        reset->setProperty("access_role_compact_action", true);
        reset->setEnabled(m_canManage);
        connect(reset, &QPushButton::clicked, this, [this, row]() { resetAccountPin(row); });
        m_accountsTable->setCellWidget(row, 4, new CenteredCellControl(reset, m_accountsTable));
    }

    // Reload creates new cell controls; apply the same theme as on first display.
    SettingsDetailUiSupport::normalizeDetailButtons(m_accountsTable);
    for (int row = 0; row < m_accountsTable->rowCount(); ++row) {
        int height = m_accountsTable->verticalHeader()->defaultSectionSize();
        for (int column : {0, 1, 4}) {
            QWidget* cell = m_accountsTable->cellWidget(row, column);
            cell->ensurePolished();
            height = qMax(height, cell->sizeHint().height());
        }
        m_accountsTable->setRowHeight(row, height);
    }
}

void AccessControlPage::reloadRoles(const QString& preferredRoleId)
{
    QString errorText;
    m_roles = AccessControlService::roles(&errorText);
    if (!errorText.isEmpty()) {
        const QSignalBlocker blocker(m_roleSelector);
        m_roleSelector->clear();
        m_permissionsTree->clear();
        m_savePermissionsButton->setEnabled(false);
        m_deleteRoleButton->setEnabled(false);
        setStatus(errorText, true);
        return;
    }

    const QString current = preferredRoleId.isEmpty() ? m_roleSelector->currentData().toString() : preferredRoleId;
    QSignalBlocker blocker(m_roleSelector);
    m_roleSelector->clear();
    for (const AccessRoleDefinition& role : m_roles) {
        m_roleSelector->addItem(role.title, role.id);
    }
    const int selected = m_roleSelector->findData(current);
    m_roleSelector->setCurrentIndex(selected >= 0 ? selected : 0);
    loadRolePermissions();
}

void AccessControlPage::loadRolePermissions()
{
    const QString roleId = m_roleSelector->currentData().toString();
    const auto roleIt = std::find_if(m_roles.cbegin(), m_roles.cend(), [&roleId](const AccessRoleDefinition& role) {
        return role.id == roleId;
    });
    const bool ownerRole = roleId == QStringLiteral("owner");
    const bool customRole = roleIt != m_roles.cend() && !roleIt->builtIn;
    const QString roleDescription = roleIt == m_roles.cend() ? QString() : roleIt->description;
    m_roleDescription->setText(roleDescription);
    m_roleDescription->setToolTip(roleDescription);
    m_deleteRoleButton->setEnabled(m_canManage && customRole);

    QString errorText;
    const QSet<QString> granted = AccessControlService::grantedPermissions(roleId, &errorText);
    if (!errorText.isEmpty()) {
        m_permissionsTree->clear();
        m_savePermissionsButton->setEnabled(false);
        setStatus(errorText, true);
        return;
    }

    const QVector<AccessPermissionDefinition> definitions = AccessControlService::permissionDefinitions();
    QSignalBlocker blocker(m_permissionsTree);
    m_permissionsTree->clear();

    QHash<QString, QTreeWidgetItem*> categories;
    for (const AccessPermissionDefinition& definition : definitions) {
        QTreeWidgetItem* category = categories.value(definition.category);
        if (!category) {
            category = new QTreeWidgetItem(m_permissionsTree, {definition.category});
            category->setFirstColumnSpanned(true);
            category->setFlags(Qt::ItemIsEnabled);
            QFont categoryFont = category->font(0);
            categoryFont.setBold(true);
            category->setFont(0, categoryFont);
            categories.insert(definition.category, category);
        }

        auto* permission = new QTreeWidgetItem(category, {definition.title});
        permission->setData(0, Qt::UserRole, definition.key);
        permission->setFlags(Qt::ItemIsEnabled);

        auto* checkboxCell = new QWidget(m_permissionsTree);
        auto* checkboxLayout = new QHBoxLayout(checkboxCell);
        checkboxLayout->setContentsMargins(0, 0, 0, 0);
        checkboxLayout->setAlignment(Qt::AlignCenter);
        auto* checkbox = new QCheckBox(checkboxCell);
        checkbox->setChecked(ownerRole || granted.contains(definition.key));
        checkbox->setEnabled(m_canManage && !ownerRole);
        checkbox->setToolTip(QString::fromUtf8("Дозволити цю функцію для обраної ролі"));
        checkboxLayout->addWidget(checkbox);
        m_permissionsTree->setItemWidget(permission, 1, checkboxCell);
    }
    m_permissionsTree->expandAll();
    m_savePermissionsButton->setEnabled(m_canManage && !ownerRole);
}

void AccessControlPage::saveAccountRoles()
{
    QVector<AccessControlService::AccountEdit> edits;
    for(int row=0;row<m_accounts.size();++row) {
        auto* roleCell=m_accountsTable->cellWidget(row,1);
        auto* nameCell=m_accountsTable->cellWidget(row,0);
        auto* role=roleCell?roleCell->findChild<QComboBox*>():nullptr;
        auto* name=nameCell?nameCell->findChild<QLineEdit*>():nullptr;
        if(!role || !name) {setStatus(QString::fromUtf8("Оновіть форму працівників перед збереженням."),true);return;}
        edits.append({m_accounts.at(row),name->text(),role->currentData().toString()});
    }
    bool changed=false;
    QString errorText;
    if(!AccessControlService::saveAccounts(edits,&changed,&errorText)) {setStatus(errorText,true);return;}
    reloadAll();
    setStatus(changed ? QString::fromUtf8("Працівників та ролі збережено.") : QString::fromUtf8("Змін немає."));
}

void AccessControlPage::resetAccountPin(int row)
{
    if (row < 0 || row >= m_accounts.size()) return;
    const AccessAccountRow account = m_accounts.at(row);
    QString errorText;
    if (!AccessControlService::resetAccountPin(account.id, &errorText)) {
        setStatus(errorText, true);
        return;
    }
    reloadAccounts();
    setStatus(QString::fromUtf8("PIN для «%1» скинуто. Працівник встановить новий PIN при наступному вході.").arg(account.displayName));
}

void AccessControlPage::createRole()
{
    QString roleId;
    QString errorText;
    if (!AccessControlService::createRole(m_newRoleName->text(), m_newRoleDescription->text(), &roleId, &errorText)) {
        setStatus(errorText, true);
        return;
    }
    m_newRoleName->clear();
    m_newRoleDescription->clear();
    reloadAll(roleId);
    setStatus(QString::fromUtf8("Створено нову роль. Налаштуйте її права та збережіть."));
}

void AccessControlPage::deleteSelectedRole()
{
    const QString roleId = m_roleSelector->currentData().toString();
    QString errorText;
    if (!AccessControlService::deleteCustomRole(roleId, &errorText)) {
        setStatus(errorText, true);
        return;
    }
    reloadAll();
    setStatus(QString::fromUtf8("Користувацьку роль видалено."));
}

void AccessControlPage::saveRolePermissions()
{
    const QString roleId = m_roleSelector->currentData().toString();
    QSet<QString> granted;
    for (int categoryIndex = 0; categoryIndex < m_permissionsTree->topLevelItemCount(); ++categoryIndex) {
        QTreeWidgetItem* category = m_permissionsTree->topLevelItem(categoryIndex);
        if (!category) continue;
        for (int permissionIndex = 0; permissionIndex < category->childCount(); ++permissionIndex) {
            QTreeWidgetItem* permission = category->child(permissionIndex);
            QWidget* checkboxCell = permission ? m_permissionsTree->itemWidget(permission, 1) : nullptr;
            auto* checkbox = checkboxCell ? checkboxCell->findChild<QCheckBox*>() : nullptr;
            if (permission && checkbox && checkbox->isChecked()) {
                granted.insert(permission->data(0, Qt::UserRole).toString());
            }
        }
    }
    QString errorText;
    if (!AccessControlService::setRolePermissions(roleId, granted, &errorText)) {
        setStatus(errorText, true);
        return;
    }
    setStatus(QString::fromUtf8("Права ролі збережено. Нові обмеження застосуються при наступному вході працівника."));
}

void AccessControlPage::setStatus(const QString& text, bool error)
{
    m_status->setText(text);
    m_status->setStyleSheet(error
        ? QStringLiteral("color:#ffb8b2;font-weight:700;")
        : QStringLiteral("color:#c8e7cf;font-weight:700;"));
}
