#include "ui/tabs/reports/ReportsPageModulePrelude.h"
#include "core/services/AdditionalExpenseService.h"
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QScrollArea>
#include <QScrollBar>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QInputDialog>

void ReportsPage::buildAdditionalExpensesForm(QWidget* page)
{
    auto* form = new QDialog(this);
    form->setWindowTitle(QString::fromUtf8("Додати витрату"));
    form->setObjectName("additionalExpensesForm");
    form->setStyleSheet("QWidget#additionalExpensesForm {background:transparent;border:none;} QWidget#additionalExpensesForm QLabel {background:transparent;}");
    auto* layout = new QFormLayout(form);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(8);
    layout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_expenseDate = new QDateEdit(QDate::currentDate(), form);
    m_expenseDate->setObjectName("additionalExpenseDate");
    m_expenseDate->setDisplayFormat("dd.MM.yy");
    m_expenseDate->setCalendarPopup(true);
    m_expenseDate->setMinimumWidth(100);
    m_expenseCategory = new QComboBox(form);
    m_expenseCategory->setObjectName("additionalExpenseCategory");
    m_expenseCategory->setEditable(false);
    m_expenseCategory->setInsertPolicy(QComboBox::NoInsert);
    m_expenseCategory->setMinimumWidth(150);
    m_expenseDescription = new QLineEdit(form);
    m_expenseDescription->setObjectName("additionalExpenseDescription");
    m_expenseDescription->setMaxLength(1000);
    m_expenseDescription->setMinimumWidth(170);
    m_expenseAmount = new QDoubleSpinBox(form);
    m_expenseAmount->setObjectName("additionalExpenseAmount");
    m_expenseAmount->setDecimals(2);
    m_expenseAmount->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_expenseAmount->setRange(0, 999999999.99);
    m_expenseAmount->setMinimumWidth(110);
    m_expenseAmount->setSuffix(QString::fromUtf8(" грн"));
    m_expenseAmount->setSpecialValueText(" ");
    auto* controls = new QWidget(page);
    controls->setObjectName("additionalExpensesFormScroll");
    controls->setStyleSheet("#additionalExpensesFormScroll {background:transparent;border:none;}");
    auto* buttons = new QHBoxLayout(controls);
    buttons->setContentsMargins(0,0,0,0);
    m_addExpenseButton = new QPushButton(QString::fromUtf8("Додати витрату"), controls);
    m_addExpenseButton->setObjectName("addAdditionalExpense");
    m_deleteExpenseButton = new QPushButton(QString::fromUtf8("Видалити вибране"), controls);
    m_deleteExpenseButton->setObjectName("deleteAdditionalExpense");
    const QStringList labels = {QString::fromUtf8("Дата"), QString::fromUtf8("Категорія"),
        QString::fromUtf8("Опис витрати"), QString::fromUtf8("Сума, грн")};
    const QList<QWidget*> fields = {m_expenseDate, m_expenseCategory, m_expenseDescription,
                                   m_expenseAmount};
    for (int i = 0; i < fields.size(); ++i) {
        fields[i]->setMinimumHeight(AdaptiveUi::px(form, 34));
        layout->addRow(labels[i], fields[i]);
    }
    auto* categoryButton = new QPushButton(QString::fromUtf8("Додати категорію"), form);
    categoryButton->setObjectName("addExpenseCategory");
    layout->addRow(QString(), categoryButton);
    connect(categoryButton, &QPushButton::clicked, form, [this, form]() {
        bool ok = false;
        const QString category = QInputDialog::getText(form, QString::fromUtf8("Додати категорію"),
            QString::fromUtf8("Назва категорії"), QLineEdit::Normal, {}, &ok).trimmed();
        if (!ok || category.isEmpty()) return;
        if (category.size() > 120) {
            QMessageBox::warning(form, QString::fromUtf8("Категорія"), QString::fromUtf8("Назва має містити не більше 120 символів."));
            return;
        }
        int index = m_expenseCategory->findText(category, Qt::MatchFixedString);
        if (index < 0) { m_expenseCategory->addItem(category); index = m_expenseCategory->count()-1; }
        m_expenseCategory->setCurrentIndex(index);
    });
    auto* dialogButtons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, form);
    dialogButtons->button(QDialogButtonBox::Save)->setText(QString::fromUtf8("Зберегти"));
    dialogButtons->button(QDialogButtonBox::Cancel)->setText(QString::fromUtf8("Скасувати"));
    layout->addRow(dialogButtons);
    connect(dialogButtons, &QDialogButtonBox::accepted, this, &ReportsPage::addAdditionalExpense);
    connect(dialogButtons, &QDialogButtonBox::rejected, form, &QDialog::reject);
    buttons->addStretch(1);
    m_addExpenseButton->setFixedHeight(qRound(7.0*logicalDpiY()/25.4));
    m_deleteExpenseButton->setFixedHeight(qRound(7.0*logicalDpiY()/25.4));
    buttons->addWidget(m_addExpenseButton);
    buttons->addWidget(m_deleteExpenseButton);
    controls->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    qobject_cast<QVBoxLayout*>(page->layout())->insertWidget(0, controls);
    m_totalExpensesTable->setObjectName("additionalExpensesTable");
    connect(m_addExpenseButton, &QPushButton::clicked, form, [this, form]() {
        updateExpenseCategories();
        m_expenseDate->setDate(QDate::currentDate());
        m_expenseDescription->clear();
        m_expenseAmount->setValue(0);
        form->resize(qMax(480, form->sizeHint().width()), form->sizeHint().height());
        form->exec();
    });
    connect(m_deleteExpenseButton, &QPushButton::clicked, this, &ReportsPage::deleteAdditionalExpense);
    auto updateDelete = [this]() {
        const int row = m_totalExpensesTable->currentIndex().row();
        m_deleteExpenseButton->setEnabled(AdditionalExpenseService::canWrite( true)
            && m_totalExpensesModel->cellUserValue(row, 0).toLongLong() > 0);
    };
    connect(m_totalExpensesTable->selectionModel(), &QItemSelectionModel::currentChanged,
            this, updateDelete);
    connect(m_totalExpensesModel, &QAbstractItemModel::modelReset, this, updateDelete);
    m_deleteExpenseButton->setEnabled(false);
}

void ReportsPage::updateExpenseCategories()
{
    const QString current = m_expenseCategory->currentText();
    QStringList categories = {QString::fromUtf8("Нова пошта"), QString::fromUtf8("Кур'єрська доставка"),
                             QString::fromUtf8("Поїздки"), QString::fromUtf8("Інше")};
    categories.append(AdditionalExpenseService::categories());
    categories.removeDuplicates();
    const QSignalBlocker blocker(m_expenseCategory);
    m_expenseCategory->clear();
    m_expenseCategory->addItems(categories);
    m_expenseCategory->setCurrentText(current);
    m_addExpenseButton->setEnabled(AdditionalExpenseService::canWrite());
}

void ReportsPage::addAdditionalExpense()
{
    m_expenseAmount->interpretText();
    QString error;
    if (!AdditionalExpenseService::add( m_expenseDate->date(),
            m_expenseCategory->currentText(), m_expenseDescription->text(), m_expenseAmount->value(),
            nullptr, &error)) {
        QMessageBox::warning(this, QString::fromUtf8("Витрати"), error);
        return;
    }
    // Keep the newly saved entry visible even when it lies outside the previous filter.
    if (m_expenseDate->date() < m_from->date()) m_from->setDate(m_expenseDate->date());
    if (m_expenseDate->date() > m_to->date()) m_to->setDate(m_expenseDate->date());
    m_expenseDescription->clear();
    m_expenseAmount->setValue(0);
    if (auto* dialog=qobject_cast<QDialog*>(m_expenseDate->window())) dialog->accept();
    markDirty();
    scheduleRefresh(0);
    queueRefreshAll(0);
}

void ReportsPage::deleteAdditionalExpense()
{
    const int row = m_totalExpensesTable->currentIndex().row();
    const qint64 id = m_totalExpensesModel->cellUserValue(row, 0).toLongLong();
    if (id <= 0) return;
    if (QMessageBox::question(this, QString::fromUtf8("Видалити витрату"),
            QString::fromUtf8("Видалити витрату «%1» на суму %2 грн?")
                .arg(m_totalExpensesModel->cellText(row, 2), m_totalExpensesModel->cellText(row, 4)),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
    QString error;
    if (!AdditionalExpenseService::remove( id, &error)) {
        QMessageBox::warning(this, QString::fromUtf8("Витрати"), error);
        return;
    }
    markDirty();
    scheduleRefresh(0);
    queueRefreshAll(0);
}
