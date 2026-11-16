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

#include <QPrintPreviewWidget>
#include <QPrintDialog>
#include <QPrinter>
#include <QPdfWriter>
#include <QSaveFile>
#include <QFileDialog>
#include <QTextDocument>
#include <QMessageBox>
#include <memory>

namespace {
void previewStatement(QWidget* parent, const QString& title, const QString& context, const QTableWidget& table) {
    auto* preview = new QDialog(parent);
    preview->setAttribute(Qt::WA_DeleteOnClose);
    preview->setWindowTitle(QObject::tr("Попередній перегляд виписки"));
    auto* layout = new QVBoxLayout(preview);
    auto* document = new QTextDocument(preview);
    QString html = QStringLiteral("<h1>%1</h1><p>%2</p><table border='1' cellspacing='0' cellpadding='4'><thead><tr>")
        .arg(title.toHtmlEscaped(),context.toHtmlEscaped());
    for (int c=0; c<table.columnCount(); ++c)
        html += "<th>" + table.horizontalHeaderItem(c)->text().toHtmlEscaped() + "</th>";
    html += "</tr></thead>";
    for (int r=0; r<table.rowCount(); ++r) {
        html += "<tr>";
        for (int c=0; c<table.columnCount(); ++c)
            html += "<td>" + table.item(r,c)->text().toHtmlEscaped().replace('\n',"<br>") + "</td>";
        html += "</tr>";
    }
    html += "</table>";
    if (!table.rowCount()) html += QObject::tr("<p>За обраними параметрами записів немає.</p>");
    document->setHtml(html);
    auto printer = std::make_shared<QPrinter>(QPrinter::HighResolution);
    printer->setPageSize(QPageSize(QPageSize::A4));
    auto* pages = new QPrintPreviewWidget(printer.get(),preview);
    layout->addWidget(pages);
    QObject::connect(pages,&QPrintPreviewWidget::paintRequested,pages,[document,printer](QPrinter* device) {
        document->print(device);
    });
    auto* actions = new QHBoxLayout;
    layout->addLayout(actions);
    auto* print = new QPushButton(QObject::tr("Друк"),preview);
    auto* save = new QPushButton(QObject::tr("Зберегти PDF"),preview);
    actions->addWidget(print);
    actions->addWidget(save);
    QObject::connect(print,&QPushButton::clicked,preview,[preview,document] {
        QPrinter device(QPrinter::HighResolution);
        device.setPageSize(QPageSize(QPageSize::A4));
        QPrintDialog settings(&device,preview);
        if (settings.exec()==QDialog::Accepted) document->print(&device);
    });
    QObject::connect(save,&QPushButton::clicked,preview,[preview,document] {
        auto* files = new QFileDialog(preview,QObject::tr("Зберегти виписку"));
        files->setAttribute(Qt::WA_DeleteOnClose);
        files->setAcceptMode(QFileDialog::AcceptSave);
        files->setNameFilter("PDF (*.pdf)");
        files->setDefaultSuffix("pdf");
        files->selectFile("Виписка.pdf");
        QObject::connect(files,&QFileDialog::fileSelected,preview,[preview,document](const QString& path) {
            QSaveFile file(path);
            if (file.open(QIODevice::WriteOnly)) {
                {
                    QPdfWriter pdf(&file);
                    pdf.setPageSize(QPageSize(QPageSize::A4));
                    document->print(&pdf);
                }
                if (file.size()>0 && file.commit()) return;
            }
            QMessageBox::warning(preview,QObject::tr("Виписка"),QObject::tr("Не вдалося зберегти PDF. %1").arg(file.errorString()));
        });
        files->open();
    });
    preview->resize(900,650);
    preview->open();
}
}

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
    tabs->addTab(new QWidget(tabs),tr("Розрахунки"));
    tabs->setTabEnabled(0,false);
    auto* issues = new QTableWidget(0,10,tabs);
    tabs->addTab(issues,tr("Запчастини"));
    issues->setHorizontalHeaderLabels({tr("Дата події"),tr("Запчастина"),tr("Кількість, шт."),tr("Виконавець"),
        tr("UID наряду"),tr("UID позиції"),tr("Первинний запис"),tr("Автомобіль"),tr("№ наряду"),tr("Подія")});
    issues->setEditTriggers(QAbstractItemView::NoEditTriggers);
    issues->setSelectionBehavior(QAbstractItemView::SelectRows);
    issues->setSelectionMode(QAbstractItemView::SingleSelection);
    auto* table = new QTableWidget(0,5,tabs);
    table->setObjectName("statementApprovals");
    issues->setObjectName("statementIssues");
    tabs->addTab(table,tr("Погодження"));
    tabs->setCurrentWidget(table);
    table->setHorizontalHeaderLabels({tr("Дата й час (UTC)"),tr("Статус"),tr("Коментар"),tr("Виконавець"),tr("UID наряду")});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->horizontalHeader()->setStretchLastSection(true);
    auto* message = new QLabel(dialog);
    layout->addWidget(message);
    auto* pdf = new QPushButton(tr("PDF"),dialog);
    layout->addWidget(pdf);
    pdf->setEnabled(false);
    connect(pdf,&QPushButton::clicked,dialog,[=] {
        if (!canAccessPage(9)) return;
        const auto context = tr("Клієнт: %1 • Автомобіль: %2 • Період: %3 — %4")
            .arg(client->currentText(),car->currentText(),from->date().toString("dd.MM.yyyy"),through->date().toString("dd.MM.yyyy"));
        const bool parts = tabs->currentWidget()==issues;
        previewStatement(dialog,parts?tr("Виписка — рух запчастин"):tr("Виписка — погодження"),context,parts?*issues:*table);
    });
    connect(generate,&QPushButton::clicked,dialog,[=] {
        pdf->setEnabled(false);
        table->setRowCount(0);
        if (!canAccessPage(9) || client->currentIndex()<0) return;
        QString error;
        if (tabs->currentWidget()==issues) {
            issues->setRowCount(0);
            const auto events = CrmPageReadService::statementPartHistory(client->currentData().toString(),
                    car->currentData().toString(),from->date(),through->date(),&error);
            if (!error.isEmpty()) { message->setText(error); return; }
            for (const auto& event : events) {
                const int row = issues->rowCount();
                issues->insertRow(row);
                const bool issued = event[9].toString()=="issue";
                for (int c=0;c<9;++c) {
                    const auto text=c==6 ? (issued?tr("Складська видача №%1"):tr("Позиція наряду №%1")).arg(event[c].toString()) : event[c].toString();
                    issues->setItem(row,c,new QTableWidgetItem(text.isEmpty()?tr("Немає даних"):text));
                }
                if (issued) issues->item(row,6)->setData(Qt::UserRole,event[6]);
                issues->item(row,4)->setData(Qt::UserRole,event[4]);
                issues->setItem(row,9,new QTableWidgetItem(issued?tr("Видано зі складу"):tr("Встановлено")));
            }
            message->setText(events.isEmpty()?tr("Збережених подій за цей період немає."):QString());
            pdf->setEnabled(true);
            return;
        }
        QVariantList rows;
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
            table->item(index,4)->setData(Qt::UserRole,row.value("order_uid"));
        }
        pdf->setEnabled(true);
    });
    auto* openOrder = new QPushButton(tr("Переглянути наряд"),dialog);
    layout->addWidget(openOrder);
    openOrder->setEnabled(false);
    connect(table,&QTableWidget::itemSelectionChanged,dialog,[=] {
        openOrder->setEnabled(!table->selectedItems().isEmpty());
    });
    connect(issues,&QTableWidget::itemSelectionChanged,dialog,[=] {
        openOrder->setEnabled(!issues->selectedItems().isEmpty());
    });
    auto showOrder = [=] {
        auto* active = tabs->currentWidget()==issues?issues:table;
        if (!canAccessPage(9) || active->selectedItems().isEmpty()) return;
        const auto* source = active->item(active->currentRow(),4);
        if (!source) return;
        QString error;
        const auto order = CrmPageReadService::statementOrder(client->currentData().toString(),
                source->data(Qt::UserRole).toString(), &error);
        if (order.isEmpty()) { message->setText(error); return; }
        auto* card = new QDialog(dialog);
        card->setAttribute(Qt::WA_DeleteOnClose);
        card->setObjectName("statementOrderCard");
        card->setWindowTitle(tr("Наряд №%1").arg(order.value("display_number").toString()));
        auto* fields = new QFormLayout(card);
        auto field = [&](const QString& label, const QString& key) {
            auto* value = new QLabel(card);
            value->setTextFormat(Qt::PlainText);
            value->setTextInteractionFlags(Qt::TextSelectableByMouse);
            const auto text = order.value(key).toString();
            value->setText(text.isEmpty()?tr("Немає даних"):text);
            fields->addRow(label,value);
        };
        field(tr("UID наряду"),"order_uid");
        field(tr("Автомобіль"),"car");
        field(tr("Державний номер"),"plate");
        field(tr("Статус"),"status");
        field(tr("Відкрито"),"opened_at");
        field(tr("Закрито"),"closed_at");
        card->open();
    };
    connect(openOrder,&QPushButton::clicked,dialog,showOrder);
    connect(table,&QTableWidget::cellActivated,dialog,showOrder);
    auto* sourceButton = new QPushButton(tr("Первинна видача"),dialog);
    layout->addWidget(sourceButton);
    sourceButton->setVisible(false);
    connect(issues,&QTableWidget::itemSelectionChanged,dialog,[=] {
        const auto* source=issues->item(issues->currentRow(),6);
        sourceButton->setVisible(tabs->currentWidget()==issues && !issues->selectedItems().isEmpty()
                                 && source && source->data(Qt::UserRole).isValid());
    });
    connect(sourceButton,&QPushButton::clicked,dialog,[=] {
        const int row=issues->currentRow();
        if (!canAccessPage(9) || tabs->currentWidget()!=issues || row<0) return;
        const auto source=CrmPageReadService::statementIssueSource(client->currentData().toString(),issues->item(row,4)->text(),
            issues->item(row,5)->text(),issues->item(row,6)->data(Qt::UserRole).toLongLong());
        if (!source.error.isEmpty()) { message->setText(source.error); return; }
        auto* card=new QDialog(dialog);
        card->setAttribute(Qt::WA_DeleteOnClose);
        card->setObjectName("statementIssueSource");
        card->setWindowTitle(tr("Складська видача №%1").arg(source.issue.value("issue_id").toString()));
        auto* body=new QVBoxLayout(card);
        auto* fields=new QFormLayout;
        body->addLayout(fields);
        auto field=[&](const QString& label,const QString& key) {
            auto* value=new QLabel(card);
            value->setTextFormat(Qt::PlainText);
            const auto text=source.issue.value(key).toString();
            value->setText(text.isEmpty()?tr("Немає даних"):text);
            fields->addRow(label,value);
        };
        field(tr("Запчастина"),"name");
        field(tr("Резерв №"),"reserve_id");
        field(tr("Замовлено"),"ordered_on");
        field(tr("Очікуване надходження (план)"),"expected_on");
        field(tr("Видано за нарядом"),"moved_on");
        field(tr("Кількість видачі"),"issued");
        field(tr("Видав"),"actor");
        body->addWidget(new QLabel(tr("Фактичні надходження всього резерву"),card));
        auto* receipts=new QTableWidget(source.receipts.size(),4,card);
        receipts->setEditTriggers(QAbstractItemView::NoEditTriggers);
        receipts->setHorizontalHeaderLabels({tr("ID надходження"),tr("Прийнято"),tr("Кількість"),tr("Виконавець")});
        for (int r=0;r<source.receipts.size();++r) {
            for (int c=0;c<4;++c) {
                const auto text=source.receipts[r][c].toString();
                receipts->setItem(r,c,new QTableWidgetItem(text.isEmpty()?tr("Немає даних"):text));
            }
        }
        body->addWidget(receipts);
        if (source.receipts.isEmpty()) body->addWidget(new QLabel(tr("Надходжень у журналі немає."),card));
        card->resize(610,480);
        card->open();
    });
    connect(issues,&QTableWidget::cellActivated,dialog,showOrder);
    auto invalidate = [=] { pdf->setEnabled(false); table->setRowCount(0); issues->setRowCount(0); openOrder->setEnabled(false); message->clear(); };
    connect(tabs,&QTabWidget::currentChanged,dialog,invalidate);
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
