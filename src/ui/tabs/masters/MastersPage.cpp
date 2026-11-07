#include "ui/tabs/masters/MastersPage.h"

#include "core/services/settings/PersonnelSettingsService.h"
#include "ui/common/dialogs/MasterEditDialog.h"
#include "core/backup/BackupCommitPublisher.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QToolButton>
#include <QLineEdit>
#include <QTableWidget>
#include <QHeaderView>
#include <QMessageBox>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QLabel>

static void setCell(QTableWidget* t, int r, int c, const QString& text, bool bold=false)
{
    auto* it = new QTableWidgetItem(text);
    if (bold) {
        QFont f = it->font();
        f.setBold(true);
        it->setFont(f);
    }
    t->setItem(r, c, it);
}

static void publishMasterBackupEvent(const QString& operationType,
                                   const QString& entityId,
                                   const QString& description)
{
    BackupCommitPublisher::publish(
        BackupCommitPublisher::makeEvent(QStringLiteral("MastersPage"),
                                         QStringLiteral("masters"),
                                         QStringLiteral("master"),
                                         entityId,
                                         QString(),
                                         0,
                                         operationType,
                                         description,
                                         {QStringLiteral("masters"), QStringLiteral("settings")}),
        nullptr);
}

MastersPage::MastersPage(QWidget* parent)
    : QWidget(parent)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0,0,0,0);
    root->setSpacing(10);

    // Toolbar
    auto* bar = new QHBoxLayout();
    bar->setContentsMargins(0,0,0,0);

    auto* btnAdd = new QToolButton(this);
    btnAdd->setText(QString::fromUtf8("➕ Додати"));
    auto* btnEdit = new QToolButton(this);
    btnEdit->setText(QString::fromUtf8("✏️ Редагувати"));
    auto* btnActive = new QToolButton(this);
    btnActive->setText(QString::fromUtf8("🗄️ Архів/Відновити"));
    auto* btnDelete = new QToolButton(this);
    btnDelete->setText(QString::fromUtf8("🗑️ Видалити"));

    m_showArchived = new QCheckBox(QString::fromUtf8("Показати архів"), this);

    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(QString::fromUtf8("Пошук: ім'я…"));
    m_search->setClearButtonEnabled(true);
    m_searchDebounce = new QTimer(this);
    m_searchDebounce->setSingleShot(true);
    m_searchDebounce->setInterval(250);

    m_refreshAllDebounce = new QTimer(this);
    m_refreshAllDebounce->setSingleShot(true);
    connect(m_refreshAllDebounce, &QTimer::timeout, this, [this]() { emit requestRefreshAll(); });

    bar->addWidget(btnAdd);
    bar->addWidget(btnEdit);
    bar->addWidget(btnActive);
    bar->addWidget(btnDelete);
    bar->addSpacing(10);
    bar->addWidget(m_showArchived);
    bar->addStretch(1);
    bar->addWidget(m_search);

    root->addLayout(bar);

    // Table
    m_table = new QTableWidget(this);
    m_table->setColumnCount(5);
    m_table->setHorizontalHeaderLabels({
        QString::fromUtf8("Ім'я"),
        QString::fromUtf8("Роль"),
        QString::fromUtf8("З/п %"),
        QString::fromUtf8("Адмін %"),
        QString::fromUtf8("Активний")
    });
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);

    root->addWidget(m_table, 1);

    connect(btnAdd, &QToolButton::clicked, this, &MastersPage::addMaster);
    connect(btnEdit, &QToolButton::clicked, this, &MastersPage::editMaster);
    connect(btnActive, &QToolButton::clicked, this, &MastersPage::toggleActive);
    connect(btnDelete, &QToolButton::clicked, this, &MastersPage::deleteMaster);

    connect(m_search, &QLineEdit::textChanged, this, [this](const QString& text) {
        if (!m_searchDebounce) {
            refresh();
            return;
        }
        if (text.trimmed().isEmpty()) {
            m_searchDebounce->stop();
            refresh();
        } else {
            m_searchDebounce->start();
        }
    });
    connect(m_search, &QLineEdit::returnPressed, this, [this]() {
        if (m_searchDebounce) m_searchDebounce->stop();
        refresh();
    });
    connect(m_searchDebounce, &QTimer::timeout, this, [this]() { refresh(); });
    connect(m_showArchived, &QCheckBox::toggled, this, [this]() {
        if (m_searchDebounce) m_searchDebounce->stop();
        refresh();
    });

    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int, int) { editMaster(); });

    // Initial load is driven by MainWindow when the page is first shown.
    // Avoid a duplicate first refresh from the constructor.
}

void MastersPage::queueRefreshAll(int delayMs)
{
    if (!m_refreshAllDebounce) {
        emit requestRefreshAll();
        return;
    }
    if (delayMs <= 0) {
        m_refreshAllDebounce->stop();
        m_refreshAllDebounce->start(0);
        return;
    }
    if (!m_refreshAllDebounce->isActive() || m_refreshAllDebounce->remainingTime() > delayMs) {
        m_refreshAllDebounce->start(delayMs);
    }
}

QString MastersPage::selectedMasterId() const
{
    if (!m_table) return QString();
    auto sel = m_table->selectionModel()->selectedRows();
    if (sel.isEmpty()) return QString();
    const int r = sel.first().row();
    auto* it = m_table->item(r, 0);
    if (!it) return QString();
    return it->data(Qt::UserRole + 1).toString();
}

void MastersPage::refresh()
{
    if (!m_table) return;
    m_loading = true;

    const bool showArchived = m_showArchived->isChecked();
    const QString filter = m_search->text().trimmed().toLower();

    const auto masters = PersonnelSettingsService::loadPersonnel(!showArchived);

    m_table->setRowCount(0);

    for (const auto& m : masters) {
        if (!filter.isEmpty() && !m.name.toLower().contains(filter)) continue;

        const int r = m_table->rowCount();
        m_table->insertRow(r);

        auto* nameIt = new QTableWidgetItem(m.name);
        nameIt->setData(Qt::UserRole + 1, m.id);
        if (!m.active) {
            QFont f = nameIt->font();
            f.setStrikeOut(true);
            nameIt->setFont(f);
        }
        m_table->setItem(r, 0, nameIt);

        const QString roleLabel = (m.role == "admin") ? QString::fromUtf8("Адмін") : QString::fromUtf8("Майстер");
        setCell(m_table, r, 1, roleLabel);

        auto* sp = new QDoubleSpinBox(m_table);
        sp->setRange(0, 100);
        sp->setDecimals(2);
        sp->setSuffix(QString::fromUtf8(" %"));
        sp->setValue(PersonnelSettingsService::loadEffectivePayrollPercent(m.id, m.name));
        sp->setEnabled(m.active);
        m_table->setCellWidget(r, 2, sp);
        connect(sp, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this, id=m.id](double v) {
            if (m_loading) return;
            QString errorText;
            const auto personnel = PersonnelSettingsService::loadPersonnel(false);
            for (const auto& row : personnel) {
                if (row.id != id) continue;
                MasterRow updated = row;
                PersonnelSettingsService::saveMaster(updated, v, &errorText);
                break;
            }
        });
        connect(sp, &QDoubleSpinBox::editingFinished, this, [sp, id=m.id, name=m.name]() {
            publishMasterBackupEvent(QStringLiteral("master_payroll_percent"),
                                     id,
                                     QString::fromUtf8("Змінено відсоток З/П для майстра: %1 (%2%)")
                                         .arg(name, QString::number(sp->value(), 'f', 2)));
        });

        setCell(m_table, r, 3, QString::number(m.adminPercent, 'f', 2));
        setCell(m_table, r, 4, m.active ? QString::fromUtf8("Так") : QString::fromUtf8("Ні"));
    }

    m_loading = false;
}

void MastersPage::addMaster()
{
    MasterEditDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted) return;

    const auto m = dlg.data();
    if (m.name.isEmpty()) {
        QMessageBox::warning(this, QString::fromUtf8("Помилка"), QString::fromUtf8("Ім'я не може бути порожнім."));
        return;
    }

    QString errorText;
    if (!PersonnelSettingsService::saveMaster(m, dlg.payrollPercent(), &errorText)) {
        QMessageBox::critical(this, QString::fromUtf8("Помилка"), errorText.isEmpty() ? QString::fromUtf8("Не вдалося зберегти майстра.") : errorText);
        return;
    }
    publishMasterBackupEvent(QStringLiteral("master_create"), m.id,
                             QString::fromUtf8("Створено майстра: %1").arg(m.name));
    // The page is already the active source of the mutation; let the queued global
    // refresh repaint it once together with dependent pages to avoid duplicate reloads.
    queueRefreshAll(0);
}

void MastersPage::editMaster()
{
    const QString id = selectedMasterId();
    if (id.isEmpty()) return;

    const auto masters = PersonnelSettingsService::loadPersonnel(false);
    MasterRow cur;
    bool found = false;
    for (const auto& m : masters) {
        if (m.id == id) { cur = m; found = true; break; }
    }
    if (!found) return;

    const double pct = PersonnelSettingsService::loadEffectivePayrollPercent(id, cur.name);

    MasterEditDialog dlg(this);
    dlg.setData(cur, pct);

    if (dlg.exec() != QDialog::Accepted) return;

    const auto upd = dlg.data();
    if (upd.name.isEmpty()) {
        QMessageBox::warning(this, QString::fromUtf8("Помилка"), QString::fromUtf8("Ім'я не може бути порожнім."));
        return;
    }

    QString errorText;
    if (!PersonnelSettingsService::saveMaster(upd, dlg.payrollPercent(), &errorText)) {
        QMessageBox::critical(this, QString::fromUtf8("Помилка"), errorText.isEmpty() ? QString::fromUtf8("Не вдалося зберегти майстра.") : errorText);
        return;
    }
    publishMasterBackupEvent(QStringLiteral("master_save"), upd.id,
                             QString::fromUtf8("Збережено майстра: %1").arg(upd.name));
    // The page is already the active source of the mutation; let the queued global
    // refresh repaint it once together with dependent pages to avoid duplicate reloads.
    queueRefreshAll(0);
}

void MastersPage::toggleActive()
{
    const QString id = selectedMasterId();
    if (id.isEmpty()) return;

    const auto masters = PersonnelSettingsService::loadPersonnel(false);
    for (const auto& m : masters) {
        if (m.id != id) continue;
        QString errorText;
        if (!PersonnelSettingsService::setMasterActive(id, !m.active, &errorText)) {
            QMessageBox::critical(this, QString::fromUtf8("Помилка"), errorText.isEmpty() ? QString::fromUtf8("Не вдалося змінити стан майстра.") : errorText);
            return;
        }
        publishMasterBackupEvent(m.active ? QStringLiteral("master_archive") : QStringLiteral("master_restore"),
                                 id,
                                 QString::fromUtf8("%1 майстра: %2")
                                     .arg(m.active ? QString::fromUtf8("Архівовано") : QString::fromUtf8("Відновлено"),
                                          m.name));
        break;
    }
    // Avoid a local full rebuild followed by a second global one.
    queueRefreshAll(0);
}

void MastersPage::deleteMaster()
{
    const QString id = selectedMasterId();
    if (id.isEmpty()) return;

    auto res = QMessageBox::question(
        this,
        QString::fromUtf8("Підтвердження"),
        QString::fromUtf8("Видалити майстра?\nЯкщо майстер використовується у нарядах — буде лише заархівований."),
        QMessageBox::Yes | QMessageBox::No
    );
    if (res != QMessageBox::Yes) return;

    QString masterName;
    const auto masters = PersonnelSettingsService::loadPersonnel(false);
    for (const auto& m : masters) {
        if (m.id == id) {
            masterName = m.name;
            break;
        }
    }

    QString errorText;
    if (!PersonnelSettingsService::deleteMaster(id, &errorText)) {
        QMessageBox::critical(this, QString::fromUtf8("Помилка"), errorText.isEmpty() ? QString::fromUtf8("Не вдалося видалити/архівувати майстра.") : errorText);
        return;
    }

    publishMasterBackupEvent(QStringLiteral("master_delete"), id,
                             QString::fromUtf8("Видалено/архівовано майстра: %1").arg(masterName));

    // Avoid a local full rebuild followed by a second global one.
    queueRefreshAll(0);
}
