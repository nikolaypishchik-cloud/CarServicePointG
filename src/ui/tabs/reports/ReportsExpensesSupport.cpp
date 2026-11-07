#include "ui/tabs/reports/ReportsExpensesSupport.h"

#include "core/services/settings/ReportsSettingsService.h"
#include "ui/tabs/reports/ReportsUiSupport.h"

#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <QtMath>

namespace {
using namespace ReportsUiSupport;

void setEnabledIfPresent(QWidget* widget, bool enabled)
{
    if (widget) widget->setEnabled(enabled);
}

void addExpenseRow(QWidget* parent,
                   QGridLayout* grid,
                   int& row,
                   const QString& label,
                   QLineEdit*& valueEdit,
                   QLineEdit*& noteEdit)
{
    auto* l = new QLabel(label, parent);
    valueEdit = makeWordField();
    noteEdit = makeWordField(QString::fromUtf8("примітка"));
    grid->addWidget(l, row, 0);
    grid->addWidget(valueEdit, row, 1);
    grid->addWidget(noteEdit, row, 3, 1, 2);
    ++row;
}

} // namespace

namespace ReportsExpensesSupport {

ExpensesWidgets buildExpensesPage(QWidget* parent)
{
    ExpensesWidgets w;
    w.page = new QWidget(parent);
    w.page->setObjectName(QStringLiteral("reportsExpensesPage"));
    w.page->setAttribute(Qt::WA_StyledBackground, true);
    w.page->setStyleSheet(QString::fromUtf8(R"(
        #reportsExpensesPage {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                                        stop:0 rgba(156,170,186,0.12),
                                        stop:0.52 rgba(172,186,202,0.18),
                                        stop:1 rgba(188,202,218,0.24));
            border: none;
        }
    )"));

    auto* expRoot = new QVBoxLayout(w.page);
    expRoot->setContentsMargins(12, 6, 12, 6);
    expRoot->setSpacing(8);

    auto* expTitle = new QLabel(QString::fromUtf8("Розрахунок витрат CarService PointG 2.1.2"), w.page);
    QFont expTitleFont = expTitle->font();
    expTitleFont.setPointSize(expTitleFont.pointSize() + 4);
    expTitleFont.setBold(true);
    expTitle->setFont(expTitleFont);
    expRoot->addWidget(expTitle);

    auto* grid = new QGridLayout();
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(8);
    grid->setColumnStretch(0, 0);
    grid->setColumnStretch(1, 1);
    grid->setColumnMinimumWidth(1, 250);
    grid->setColumnMinimumWidth(2, 20);
    grid->setColumnStretch(3, 0);
    grid->setColumnStretch(4, 1);
    grid->setColumnMinimumWidth(4, 170);

    int rr = 0;
    auto* p1 = new QLabel(QString::fromUtf8("Період"), w.page);
    grid->addWidget(p1, rr, 0);
    auto* p2 = new QLabel(QString::fromUtf8("з:"), w.page);
    grid->addWidget(p2, rr, 1, Qt::AlignLeft | Qt::AlignVCenter);
    auto* p3 = new QLabel(QString::fromUtf8("по:"), w.page);
    grid->addWidget(p3, rr, 3, Qt::AlignLeft | Qt::AlignVCenter);
    ++rr;

    auto* elecHeader = new QLabel(QString::fromUtf8("Ел.енергія"), w.page);
    QFont secf = elecHeader->font();
    secf.setBold(true);
    elecHeader->setFont(secf);
    grid->addWidget(elecHeader, rr, 0);
    ++rr;

    grid->addWidget(new QLabel(QString::fromUtf8("Лічильник поточний"), w.page), rr, 0);
    w.elecCurrEdit = makeWordField();
    grid->addWidget(w.elecCurrEdit, rr, 1);
    grid->addWidget(new QLabel(QString::fromUtf8("Лічильник минулий"), w.page), rr, 3);
    w.elecPrevEdit = makeWordField();
    w.elecPrevEdit->setMaximumWidth(170);
    grid->addWidget(w.elecPrevEdit, rr, 4, Qt::AlignLeft);
    ++rr;

    grid->addWidget(new QLabel(QString::fromUtf8("Споживання кВт"), w.page), rr, 0);
    w.elecKwhEdit = makeWordField();
    w.elecKwhEdit->setReadOnly(true);
    grid->addWidget(w.elecKwhEdit, rr, 1);
    grid->addWidget(new QLabel(QString::fromUtf8("Тариф (грн/кВт)"), w.page), rr, 3);
    w.elecTariffEdit = makeWordField();
    w.elecTariffEdit->setMaximumWidth(170);
    grid->addWidget(w.elecTariffEdit, rr, 4, Qt::AlignLeft);
    ++rr;

    grid->addWidget(new QLabel(QString::fromUtf8("Всього (грн)"), w.page), rr, 0);
    w.elecCostEdit = makeWordField();
    w.elecCostEdit->setReadOnly(true);
    grid->addWidget(w.elecCostEdit, rr, 1);
    ++rr;

    addExpenseRow(w.page, grid, rr, QString::fromUtf8("Витрати на миючі засоби"), w.cleaningEdit, w.cleaningNoteEdit);
    addExpenseRow(w.page, grid, rr, QString::fromUtf8("Витрати на ПЗ OpenDataBot"), w.softwareEdit, w.softwareNoteEdit);
    addExpenseRow(w.page, grid, rr, QString::fromUtf8("Опалення"), w.heatingEdit, w.heatingNoteEdit);
    addExpenseRow(w.page, grid, rr, QString::fromUtf8("Амортизація"), w.amortizationEdit, w.amortizationNoteEdit);
    addExpenseRow(w.page, grid, rr, QString::fromUtf8("Оренда"), w.rentEdit, w.rentNoteEdit);
    addExpenseRow(w.page, grid, rr, QString::fromUtf8("Інші витрати"), w.otherEdit, w.otherNoteEdit);

    auto* totalLabel = new QLabel(QString::fromUtf8("Всього витрат"), w.page);
    totalLabel->setFont(secf);
    grid->addWidget(totalLabel, rr, 0);
    w.expensesTotalEdit = makeWordField();
    w.expensesTotalEdit->setReadOnly(true);
    grid->addWidget(w.expensesTotalEdit, rr, 1);
    ++rr;

    expRoot->addLayout(grid);
    auto* expBtns = new QHBoxLayout();
    expBtns->addStretch(1);
    w.electricityPdfButton = new QPushButton(QString::fromUtf8("PDF ел.енергії"), w.page);
    w.expensesSaveButton = new QPushButton(QString::fromUtf8("Зберегти"), w.page);
    expBtns->addWidget(w.electricityPdfButton);
    expBtns->addWidget(w.expensesSaveButton);
    expRoot->addLayout(expBtns);

    return w;
}

void applySnapshot(const ExpensesSnapshot& snapshot, const ExpensesWidgets& widgets)
{
    const QSignalBlocker b1(widgets.elecPrevEdit), b2(widgets.elecCurrEdit), b3(widgets.elecTariffEdit), b4(widgets.cleaningEdit), b5(widgets.cleaningNoteEdit),
        b6(widgets.softwareEdit), b7(widgets.softwareNoteEdit), b8(widgets.heatingEdit), b9(widgets.heatingNoteEdit), b10(widgets.amortizationEdit),
        b11(widgets.amortizationNoteEdit), b12(widgets.rentEdit), b13(widgets.rentNoteEdit), b14(widgets.otherEdit), b15(widgets.otherNoteEdit),
        b16(widgets.elecKwhEdit), b17(widgets.elecCostEdit);

    widgets.elecPrevEdit->setText(money0(snapshot.elecPrev));
    widgets.elecCurrEdit->setText(snapshot.foundRow ? money0(snapshot.elecCurr) : QString());
    widgets.elecTariffEdit->setText(snapshot.elecTariff > 0.0 ? money0(snapshot.elecTariff) : QString());
    widgets.cleaningEdit->setText(snapshot.foundRow ? money0(snapshot.cleaning) : QString());
    widgets.cleaningNoteEdit->setText(snapshot.cleaningNote);
    widgets.softwareEdit->setText(snapshot.foundRow ? money0(snapshot.software) : QString());
    widgets.softwareNoteEdit->setText(snapshot.softwareNote);
    widgets.heatingEdit->setText(snapshot.foundRow ? money0(snapshot.heating) : QString());
    widgets.heatingNoteEdit->setText(snapshot.heatingNote);
    widgets.amortizationEdit->setText(snapshot.foundRow ? money0(snapshot.amortization) : QString());
    widgets.amortizationNoteEdit->setText(snapshot.amortizationNote);
    widgets.rentEdit->setText(snapshot.foundRow ? money0(snapshot.rent) : QString());
    widgets.rentNoteEdit->setText(snapshot.rentNote);
    widgets.otherEdit->setText(snapshot.foundRow ? money0(snapshot.other) : QString());
    widgets.otherNoteEdit->setText(snapshot.otherNote);

    for (QLineEdit* e : {widgets.elecPrevEdit, widgets.elecCurrEdit, widgets.elecTariffEdit,
                         widgets.cleaningEdit, widgets.cleaningNoteEdit, widgets.softwareEdit, widgets.softwareNoteEdit,
                         widgets.heatingEdit, widgets.heatingNoteEdit, widgets.amortizationEdit, widgets.amortizationNoteEdit,
                         widgets.rentEdit, widgets.rentNoteEdit, widgets.otherEdit, widgets.otherNoteEdit}) {
        setEnabledIfPresent(e, true);
    }
    setEnabledIfPresent(widgets.expensesSaveButton, true);
}

ExpensesRecalcResult recalcForm(const ExpensesWidgets& widgets, bool persistTariffSetting)
{
    ExpensesRecalcResult result;
    const double prev = parseUaNumber(widgets.elecPrevEdit->text());
    const double curr = parseUaNumber(widgets.elecCurrEdit->text());
    const double tariff = parseUaNumber(widgets.elecTariffEdit->text());
    if (persistTariffSetting && tariff > 0.0) {
        ReportsSettingsService::saveElectricityTariff(tariff);
    }

    result.kwh = qMax(0.0, curr - prev);
    result.electricityCost = qRound64(result.kwh * tariff);
    widgets.elecKwhEdit->setText(money0(result.kwh));
    widgets.elecCostEdit->setText(money0(result.electricityCost));

    result.total = result.electricityCost
        + parseUaNumber(widgets.cleaningEdit->text())
        + parseUaNumber(widgets.softwareEdit->text())
        + parseUaNumber(widgets.heatingEdit->text())
        + parseUaNumber(widgets.amortizationEdit->text())
        + parseUaNumber(widgets.rentEdit->text())
        + parseUaNumber(widgets.otherEdit->text());

    widgets.expensesTotalEdit->setText(money0(result.total));
    result.stats = {
        QString::fromUtf8("Ел.енергія:  %1  грн.").arg(money0(result.electricityCost)),
        QString::fromUtf8("Всього витрат:  %1  грн.").arg(money0(result.total))
    };
    return result;
}

ReportsSyncSupport::ExpensesSavePayload buildSavePayload(const ExpensesWidgets& widgets)
{
    ReportsSyncSupport::ExpensesSavePayload payload;
    payload.elecPrev = parseUaNumber(widgets.elecPrevEdit->text());
    payload.elecCurr = parseUaNumber(widgets.elecCurrEdit->text());
    payload.elecTariff = parseUaNumber(widgets.elecTariffEdit->text());
    payload.cleaning = parseUaNumber(widgets.cleaningEdit->text());
    payload.cleaningNote = widgets.cleaningNoteEdit->text();
    payload.software = parseUaNumber(widgets.softwareEdit->text());
    payload.softwareNote = widgets.softwareNoteEdit->text();
    payload.heating = parseUaNumber(widgets.heatingEdit->text());
    payload.heatingNote = widgets.heatingNoteEdit->text();
    payload.amortization = parseUaNumber(widgets.amortizationEdit->text());
    payload.amortizationNote = widgets.amortizationNoteEdit->text();
    payload.rent = parseUaNumber(widgets.rentEdit->text());
    payload.rentNote = widgets.rentNoteEdit->text();
    payload.other = parseUaNumber(widgets.otherEdit->text());
    payload.otherNote = widgets.otherNoteEdit->text();
    payload.elecKwh = qMax(0.0, payload.elecCurr - payload.elecPrev);
    payload.elecCost = qRound64(payload.elecKwh * payload.elecTariff);
    return payload;
}

ReportsPdfRenderer::ExpensesFormData buildPdfData(const ExpensesWidgets& widgets)
{
    ReportsPdfRenderer::ExpensesFormData data;
    data.elecPrev = parseUaNumber(widgets.elecPrevEdit->text());
    data.elecCurr = parseUaNumber(widgets.elecCurrEdit->text());
    data.elecTariff = parseUaNumber(widgets.elecTariffEdit->text());
    data.elecKwh = parseUaNumber(widgets.elecKwhEdit->text());
    data.elecCost = parseUaNumber(widgets.elecCostEdit->text());
    data.cleaning = parseUaNumber(widgets.cleaningEdit->text());
    data.cleaningNote = widgets.cleaningNoteEdit->text();
    data.software = parseUaNumber(widgets.softwareEdit->text());
    data.softwareNote = widgets.softwareNoteEdit->text();
    data.heating = parseUaNumber(widgets.heatingEdit->text());
    data.heatingNote = widgets.heatingNoteEdit->text();
    data.amortization = parseUaNumber(widgets.amortizationEdit->text());
    data.amortizationNote = widgets.amortizationNoteEdit->text();
    data.rent = parseUaNumber(widgets.rentEdit->text());
    data.rentNote = widgets.rentNoteEdit->text();
    data.other = parseUaNumber(widgets.otherEdit->text());
    data.otherNote = widgets.otherNoteEdit->text();
    data.total = parseUaNumber(widgets.expensesTotalEdit->text());
    return data;
}

} // namespace ReportsExpensesSupport
