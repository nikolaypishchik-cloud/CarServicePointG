#include "ui/tabs/reports/ReportsPageModulePrelude.h"
#include "core/services/AdditionalExpenseService.h"
#include <QButtonGroup>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QScrollArea>
#include <QScrollBar>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QInputDialog>

namespace {
QPair<QDate, QDate> expensePeriodRange(QDate start, QDate end, int months, int direction)
{
    if (months == 0) {
        if (end < start) return {};
        const qint64 offset = direction * (start.daysTo(end) + 1);
        return {start.addDays(offset), end.addDays(offset)};
    }
    const int firstMonth = 1 + ((start.month() - 1) / months) * months;
    const QDate first = QDate(start.year(), firstMonth, 1).addMonths(direction * months);
    const QDate lastMonth = first.addMonths(months - 1);
    return {first, QDate(lastMonth.year(), lastMonth.month(), lastMonth.daysInMonth())};
}
}

void ReportsPage::buildExpensePeriodControls(QWidget* page, QVBoxLayout* layout)
{
    auto* row = new QHBoxLayout;
    row->addWidget(new QLabel(QStringLiteral("Швидкий період"), page));
    auto* modes = new QButtonGroup(page);
    auto addMode = [&](const QString& label, int months) {
        auto* button = new QPushButton(label, page);
        button->setObjectName(QStringLiteral("totalExpensePeriod%1").arg(months));
        button->setCheckable(true);
        modes->addButton(button, months);
        row->addWidget(button);
    };
    addMode(QStringLiteral("Місяць"), 1);
    addMode(QStringLiteral("Квартал"), 3);
    addMode(QStringLiteral("Рік"), 12);
    addMode(QStringLiteral("Довільний"), 0);
    modes->button(0)->setChecked(true);
    row->addStretch(1);
    auto* previous = new QPushButton(QStringLiteral("‹"), page);
    auto* next = new QPushButton(QStringLiteral("›"), page);
    previous->setObjectName("totalExpensePreviousPeriod");
    next->setObjectName("totalExpenseNextPeriod");
    previous->setAccessibleName(QStringLiteral("Попередній період"));
    next->setAccessibleName(QStringLiteral("Наступний період"));
    previous->setToolTip(previous->accessibleName());
    next->setToolTip(next->accessibleName());
    row->addWidget(previous);
    row->addWidget(next);
    layout->insertLayout(2, row);

    auto available = [this](const QPair<QDate,QDate>& range) {
        return range.first.isValid() && range.second.isValid() && range.first >= m_from->minimumDate()
            && range.second <= m_to->maximumDate();
    };
    auto updateArrows = [this, available, previous, next, modes] {
        auto adjacent = [this, modes](int step) {
            return expensePeriodRange(m_from->date(), m_to->date(), modes->checkedId(), step);
        };
        previous->setEnabled(available(adjacent(-1)));
        next->setEnabled(available(adjacent(1)));
    };
    auto apply = [this, available, modes, updateArrows](int months, int step) {
        const auto range = expensePeriodRange(m_from->date(), m_to->date(), months, step);
        if (available(range)) {
            m_from->setDate(range.first);
            m_to->setDate(range.second);
            modes->button(months)->setChecked(true);
        }
        updateArrows();
    };
    auto manual = [=] { modes->button(0)->setChecked(true); updateArrows(); };
    connect(m_from, &QDateEdit::dateChanged, page, manual);
    connect(m_to, &QDateEdit::dateChanged, page, manual);
    connect(modes, &QButtonGroup::idClicked, page, [=](int months) { apply(months, 0); });
    connect(previous, &QPushButton::clicked, page, [=] { apply(modes->checkedId(), -1); });
    connect(next, &QPushButton::clicked, page, [=] { apply(modes->checkedId(), 1); });
    updateArrows();
}

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
    auto* filters = new QHBoxLayout();
    m_totalExpenseSearch = new QLineEdit(page);
    m_totalExpenseSearch->setObjectName("totalExpenseSearch");
    m_totalExpenseSearch->setPlaceholderText(QStringLiteral("Пошук в описі"));
    m_totalExpenseSearch->setClearButtonEnabled(true);
    m_totalExpenseCategory = new QComboBox(page);
    m_totalExpenseCategory->setObjectName("totalExpenseCategory");
    m_totalExpenseCategory->setAccessibleName(QStringLiteral("Категорія"));
    m_totalExpenseSource = new QComboBox(page);
    m_totalExpenseSource->setObjectName("totalExpenseSource");
    m_totalExpenseSource->setAccessibleName(QStringLiteral("Джерело"));
    m_totalExpenseSource->addItem(QStringLiteral("Усі джерела"));
    m_totalExpenseSource->addItem(QStringLiteral("Витрати за період"));
    m_totalExpenseSource->addItem(QStringLiteral("Додаткова витрата"));
    filters->addWidget(m_totalExpenseSearch, 2);
    filters->addWidget(m_totalExpenseCategory, 1);
    filters->addWidget(m_totalExpenseSource, 1);
    qobject_cast<QVBoxLayout*>(page->layout())->insertLayout(1, filters);
    buildExpensePeriodControls(page, qobject_cast<QVBoxLayout*>(page->layout()));
    connect(m_totalExpenseSearch, &QLineEdit::textChanged, this, &ReportsPage::filterTotalExpenses);
    connect(m_totalExpenseCategory, &QComboBox::currentIndexChanged, this, &ReportsPage::filterTotalExpenses);
    connect(m_totalExpenseSource, &QComboBox::currentIndexChanged, this, &ReportsPage::filterTotalExpenses);
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

void ReportsPage::filterTotalExpenses()
{
    if (!m_totalExpensesSnapshot) return;
    const auto& all = *m_totalExpensesSnapshot;
    TotalExpensesSnapshot visible;
    visible.error = all.error;
    const QString term = m_totalExpenseSearch->text().trimmed();
    const QString category = m_totalExpenseCategory->currentData().toString();
    for (const auto& row : all.rows) {
        if (!row.description.contains(term, Qt::CaseInsensitive)
            || (!category.isEmpty() && row.category != category)
            || (m_totalExpenseSource->currentIndex() > 0 && row.source != m_totalExpenseSource->currentText())) continue;
        visible.rows.append(row);
        if (row.expenseId > 0) {
            visible.additionalCents += row.amountCents;
            if (row.category.simplified().compare(QStringLiteral("Нова пошта"), Qt::CaseInsensitive) == 0)
                visible.novaPoshtaCents += row.amountCents;
        } else {
            visible.periodicCents += row.amountCents;
        }
    }
    ReportsTableSupport::applyTotalExpensesSnapshot(m_totalExpensesTable, m_totalExpensesModel, visible);
    if (!all.error.isEmpty()) {
        m_totalExpenseMetrics.clear();
        setTabStats(ReportTab::TotalExpenses, all.stats);
        return;
    }
    if (!all.rows.isEmpty() && visible.rows.isEmpty())
        m_totalExpensesModel->setMessageRow(QStringLiteral("За вибраними фільтрами витрат немає"));
    const auto hryvnias = [](qint64 cents) { return FormatUtils::money(cents / 100.0, 2) + QStringLiteral(" грн"); };
    m_totalExpenseMetrics = {hryvnias(visible.periodicCents + visible.additionalCents),
                             QString::number(visible.rows.size()), hryvnias(visible.novaPoshtaCents)};
    setTabStats(ReportTab::TotalExpenses, {QStringLiteral("Періодичні: ") + hryvnias(visible.periodicCents),
                                          QStringLiteral("Додаткові: ") + hryvnias(visible.additionalCents),
                                          QStringLiteral("Разом: ") + m_totalExpenseMetrics.first()});
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
