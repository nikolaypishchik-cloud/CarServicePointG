#include "ui/main/MainWindow.h"
#include "core/crm/CrmPageReadService.h"
#include <QComboBox>
#include <QDateEdit>
#include <QDialog>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTabWidget>

void MainWindow::openHeaderStatements()
{
    if (!canAccessPage(9)) return;
    if (auto* existing = findChild<QDialog*>("headerStatementsWindow")) {
        if (existing->isMinimized()) existing->showNormal();
        existing->show();
        existing->raise();
        existing->activateWindow();
        return;
    }
    auto* dialog = new QDialog(this);
    dialog->setObjectName("headerStatementsWindow");
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(tr("Виписки"));
    auto* layout = new QVBoxLayout(dialog);
    auto* form = new QFormLayout;
    layout->addLayout(form);
    auto* search = new QLineEdit(dialog);
    auto* client = new QComboBox(dialog);
    auto* car = new QComboBox(dialog);
    form->addRow(tr("Пошук клієнта"), search);
    form->addRow(tr("Клієнт"), client);
    form->addRow(tr("Автомобіль"), car);
    const auto today = QDate::currentDate();
    auto* from = new QDateEdit(QDate(today.year(),today.month(),1), dialog);
    auto* through = new QDateEdit(today, dialog);
    for (auto* date : {from, through}) date->setCalendarPopup(true);
    form->addRow(tr("Від"), from);
    form->addRow(tr("До"), through);
    auto* generate = new QPushButton(tr("Сформувати"), dialog);
    layout->addWidget(generate);
    auto* tabs = new QTabWidget(dialog);
    layout->addWidget(tabs);
    for (const auto& title : {tr("Розрахунки"),tr("Запчастини")}) {
        int index = tabs->addTab(new QWidget(tabs), title);
        tabs->setTabEnabled(index,false);
    }
    auto* table = new QTableWidget(0,5,tabs);
    tabs->addTab(table,tr("Погодження"));
    tabs->setCurrentWidget(table);
    table->setHorizontalHeaderLabels({tr("Дата й час (UTC)"),tr("Статус"),tr("Коментар"),tr("Виконавець"),tr("UID наряду")});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->horizontalHeader()->setStretchLastSection(true);
    auto* message = new QLabel(dialog);
    layout->addWidget(message);
    connect(generate,&QPushButton::clicked,dialog,[=] {
        table->setRowCount(0);
        if (!canAccessPage(9) || client->currentIndex()<0) return;
        QVariantList rows;
        QString error;
        if (!CrmPageReadService::statementDecisions(client->currentData().toString(),
                car->currentData().toString(),from->date(),through->date(),rows,&error)) {
            message->setText(error);
            return;
        }
        message->setText(rows.isEmpty()?tr("За обраними параметрами погоджень немає."):QString());
        for (const auto& record : rows) {
            const auto row = record.toMap();
            const int index = table->rowCount();
            table->insertRow(index);
            int column = 0;
            for (const auto* key : {"decision_at","status","decision_comment","actor_name","order_uid"}) {
                auto value = row.value(key).toString();
                if (value=="approved" && column==1) value=tr("Погоджено");
                if (value=="declined" && column==1) value=tr("Відхилено");
                table->setItem(index,column++,new QTableWidgetItem(value.isEmpty()?tr("Немає даних"):value));
            }
        }
    });
    auto invalidate = [=] { table->setRowCount(0); message->clear(); };
    connect(car,&QComboBox::currentIndexChanged,dialog,invalidate);
    connect(from,&QDateEdit::dateChanged,dialog,invalidate);
    connect(through,&QDateEdit::dateChanged,dialog,invalidate);
    connect(client,&QComboBox::currentIndexChanged,dialog,[=] {
        invalidate();
        car->clear();
        car->addItem(tr("Усі автомобілі"));
        generate->setEnabled(client->currentIndex()>=0);
        if (client->currentIndex()<0) return;
        QString error;
        const auto vehicles = CrmPageReadService::statementVehicles(client->currentData().toString(), &error);
        for (const auto& vehicle : vehicles)
            car->addItem(vehicle.value(1).toString()+" "+vehicle.value(2).toString(),vehicle.value(0));
        message->setText(error);
    });
    auto load = [=] {
        client->clear();
        const auto result = CrmPageReadService::clients(search->text());
        for (const auto& person : result.clients)
            client->addItem(person.value(1).toString()+" "+person.value(2).toString(), person.value(0));
        if (!result.error.isEmpty()) message->setText(result.error);
        generate->setEnabled(client->currentIndex()>=0);
    };
    connect(search,&QLineEdit::returnPressed,dialog,load);
    search->setPlaceholderText(tr("Ім’я, телефон або авто — натисніть Enter"));
    generate->setAutoDefault(false);
    load();
    dialog->resize(850,560);
    dialog->show();
}
