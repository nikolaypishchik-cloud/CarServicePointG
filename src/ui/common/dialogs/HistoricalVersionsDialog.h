#pragma once
#include "core/services/HistoricalVersionsService.h"
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QVBoxLayout>
#include <memory>

namespace HistoricalVersionsDialog {
inline QDialog* create(QWidget* parent,const QString& owner,QString* error)
{
    QList<HistoricalVersionsService::Version> versions;
    if(!HistoricalVersionsService::versions(owner,&versions,error))return nullptr;
    auto* dialog=new QDialog(parent);dialog->setObjectName("historicalVersionsDialog");dialog->resize(1050,650);
    dialog->setWindowTitle(owner.isEmpty()?QString::fromUtf8("Історичні версії джерела"):QString::fromUtf8("Історичні версії наряду"));
    auto* layout=new QVBoxLayout(dialog);
    auto* explanation=new QLabel(QString::fromUtf8("Рахунки використовують поточну версію. Історичні знімки доступні для читання з указаного джерела."),dialog);explanation->setWordWrap(true);layout->addWidget(explanation);
    auto* source=new QComboBox(dialog);source->setObjectName("historySource");layout->addWidget(source);
    for(const auto& version:versions)source->addItem(version.id=="current" ? version.origin : QFileInfo(version.origin).fileName()+" — "+version.recordedAt+" — "+version.id.left(12),version.id);
    auto* provenance=new QLabel(dialog);provenance->setObjectName("historyProvenance");provenance->setTextFormat(Qt::PlainText);provenance->setWordWrap(true);provenance->setTextInteractionFlags(Qt::TextSelectableByMouse);layout->addWidget(provenance);
    auto* tables=new QComboBox(dialog);tables->setObjectName("historyTable");layout->addWidget(tables);
    auto* filterRow=new QHBoxLayout;auto* uid=new QLineEdit(dialog);uid->setObjectName("historyRecordUid");uid->setPlaceholderText(QString::fromUtf8("Стабільний UID запису (необов'язково)"));filterRow->addWidget(uid);
    auto* find=new QPushButton(QString::fromUtf8("Показати"),dialog);find->setObjectName("historyFind");filterRow->addWidget(find);layout->addLayout(filterRow);
    auto* grid=new QTableWidget(dialog);grid->setObjectName("historyRows");grid->setEditTriggers(QAbstractItemView::NoEditTriggers);grid->setSelectionBehavior(QAbstractItemView::SelectRows);grid->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);grid->horizontalHeader()->setStretchLastSection(true);layout->addWidget(grid,1);
    auto* status=new QLabel(dialog);status->setObjectName("historyStatus");status->setTextFormat(Qt::PlainText);layout->addWidget(status);
    auto* pages=new QHBoxLayout;auto* previous=new QPushButton(QString::fromUtf8("Попередні"),dialog);auto* next=new QPushButton(QString::fromUtf8("Наступні"),dialog);pages->addWidget(previous);pages->addWidget(next);pages->addStretch();layout->addLayout(pages);
    auto* close=new QDialogButtonBox(QDialogButtonBox::Close,dialog);QObject::connect(close,&QDialogButtonBox::rejected,dialog,&QDialog::reject);layout->addWidget(close);
    auto offset=std::make_shared<int>(0);
    auto load=[=]() {
        grid->clear();grid->setRowCount(0);grid->setColumnCount(0);next->setEnabled(false);previous->setEnabled(*offset>0);
        const auto table=tables->currentData().toString();const bool hasUid=QStringList{"work_orders","work_order_services","work_order_parts"}.contains(table);uid->setEnabled(hasUid);
        if(table.isEmpty()){status->setText(QString::fromUtf8("У цій версії немає доступних записів"));return;}
        HistoricalVersionsService::Page page;QString detail;
        if(!HistoricalVersionsService::read(owner,source->currentData().toString(),table,*offset,100,&page,&detail,hasUid?uid->text().trimmed():QString())){status->setText(detail);return;}
        grid->setColumnCount(page.columns.size());grid->setHorizontalHeaderLabels(page.columns);grid->setRowCount(page.rows.size());
        for(int r=0;r<page.rows.size();++r)for(int c=0;c<page.columns.size();++c) {
            const auto value=page.rows[r][c];const auto type=page.storageTypes[r][c];QString text;
            if(type=="null")text="NULL";else if(type=="blob"){const auto bytes=value.toByteArray();text=QString::fromLatin1(bytes.left(256).toHex(' '));if(bytes.size()>256)text+=QString::fromUtf8(" … (%1 байтів)").arg(bytes.size());}else text=value.toString();
            auto* item=new QTableWidgetItem(text);item->setData(Qt::UserRole,value);item->setData(Qt::UserRole+1,type);grid->setItem(r,c,item);
        }
        next->setEnabled(page.rows.size()==100);status->setText(QString::fromUtf8("Записів на сторінці: %1; початок: %2. Подвійне клацання відкриває повне поле.").arg(page.rows.size()).arg(*offset+1));
    };
    auto chooseTable=[=](){*offset=0;uid->clear();load();};
    auto chooseSource=[=]() {
        const auto id=source->currentData().toString();
        for(const auto& version:versions)if(version.id==id)provenance->setText(id=="current" ? version.origin : version.origin+"\nSHA-256: "+version.digest+"\n"+version.recordedAt);
        QStringList names;QString detail;const QSignalBlocker block(tables);tables->clear();
        if(!HistoricalVersionsService::tables(owner,id,&names,&detail)){grid->clear();grid->setRowCount(0);status->setText(detail);return;}
        for(const auto& name:names)tables->addItem(name,name);chooseTable();
    };
    QObject::connect(source,qOverload<int>(&QComboBox::currentIndexChanged),dialog,[=](int){chooseSource();});
    QObject::connect(tables,qOverload<int>(&QComboBox::currentIndexChanged),dialog,[=](int){chooseTable();});
    QObject::connect(find,&QPushButton::clicked,dialog,[=](){*offset=0;load();});QObject::connect(uid,&QLineEdit::returnPressed,dialog,[=](){*offset=0;load();});
    QObject::connect(previous,&QPushButton::clicked,dialog,[=](){*offset=qMax(0,*offset-100);load();});QObject::connect(next,&QPushButton::clicked,dialog,[=](){*offset+=100;load();});
    QObject::connect(grid,&QTableWidget::cellDoubleClicked,dialog,[=](int r,int c){
        auto* item=grid->item(r,c);if(!item)return;QDialog field(dialog);field.resize(700,450);field.setWindowTitle(grid->horizontalHeaderItem(c)->text());
        auto* body=new QVBoxLayout(&field);auto* text=new QPlainTextEdit(&field);text->setReadOnly(true);
        const auto type=item->data(Qt::UserRole+1).toString();const auto value=item->data(Qt::UserRole);
        text->setPlainText(type=="null" ? QString("NULL") : type=="blob" ? QString::fromLatin1(value.toByteArray().toHex(' ')) : value.toString());body->addWidget(text);
        auto* buttons=new QDialogButtonBox(QDialogButtonBox::Close,&field);QObject::connect(buttons,&QDialogButtonBox::rejected,&field,&QDialog::reject);body->addWidget(buttons);field.exec();
    });
    chooseSource();return dialog;
}
inline void open(QWidget* parent,const QString& owner)
{
    QString error;std::unique_ptr<QDialog> dialog(create(parent,owner,&error));
    if(!dialog){QMessageBox::warning(parent,QString::fromUtf8("Історичні версії"),error);return;}dialog->exec();
}
}
