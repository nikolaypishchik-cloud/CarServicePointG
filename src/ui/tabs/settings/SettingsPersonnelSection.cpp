#include "ui/tabs/settings/SettingsPersonnelSection.h"
#include "ui/tabs/orders/BayOccupancyPage.h"
#include <functional>
#include "core/services/settings/PersonnelSettingsService.h"
#include "core/services/settings/PersonnelEmploymentHistory.h"
#include <QJsonArray>
#include <QTimeZone>
#include <QPlainTextEdit>
#include "ui/common/dialogs/PersonnelDialogStyle.h"
#include "ui/tabs/settings/SettingsPageDataSupport.h"
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QDateEdit>
#include <QCheckBox>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTableWidget>
#include <QHeaderView>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QShowEvent>
#include <algorithm>

namespace {
class PersonnelPostsPage final : public QWidget {
public:
    explicit PersonnelPostsPage(QWidget* parent, const std::function<void()>& refresh) : QWidget(parent), refreshAll(refresh) {
        setObjectName("personnelPostsPage");
        auto* layout=new QVBoxLayout(this);
        auto* title=new QLabel(QString::fromUtf8("Працівники за постами"),this);
        layout->addWidget(title);
        auto* rosterActions=new QHBoxLayout;
        auto* add=new QPushButton(QString::fromUtf8("Додати працівника"),this);add->setObjectName("addPersonnelEmployee");
        auto* dismiss=new QPushButton(QString::fromUtf8("Звільнити працівника"),this);dismiss->setObjectName("dismissPersonnelEmployee");
        for(auto* button:{add,dismiss}) {button->setProperty("stay_in_detail_menu",true);rosterActions->addWidget(button);}
        archived=new QCheckBox(QString::fromUtf8("Показати звільнених"),this);archived->setObjectName("showDismissedPersonnel");
        rosterActions->addWidget(archived);rosterActions->addStretch();layout->addLayout(rosterActions);
        auto* detailsActions=new QHBoxLayout;
        auto* edit=new QPushButton(QString::fromUtf8("Редагувати дані"),this);edit->setObjectName("editPersonnelEmployee");
        auto* reinstate=new QPushButton(QString::fromUtf8("Поновити працівника"),this);reinstate->setObjectName("reinstatePersonnelEmployee");
        auto* history=new QPushButton(QString::fromUtf8("Історія роботи"),this);history->setObjectName("personnelEmploymentHistory");
        for(auto* button:{edit,reinstate,history}){button->setProperty("stay_in_detail_menu",true);detailsActions->addWidget(button);}
        detailsActions->addStretch();layout->addLayout(detailsActions);
        connect(edit,&QPushButton::clicked,this,[this]{editSelected(false);});
        connect(reinstate,&QPushButton::clicked,this,[this]{editSelected(true);});
        connect(history,&QPushButton::clicked,this,[this]{showHistory();});
        connect(add,&QPushButton::clicked,this,[this]{hire();});
        connect(dismiss,&QPushButton::clicked,this,[this]{dismissSelected();});
        connect(archived,&QCheckBox::toggled,this,[this]{
            if(dirty) {const QSignalBlocker blocker(archived);archived->setChecked(!archived->isChecked());pendingWarning();return;}
            reload();
        });
        table=new QTableWidget(0,6,this);
        table->setObjectName("personnelPostsTable");
        // Row deletion must go through the personnel lifecycle, never the generic table menu.
        table->setProperty("allowContextRowRemoval",false);
        table->setToolTip(QString::fromUtf8("Звільнення прибирає працівника з нових призначень і зберігає історію нарядів та звітів."));
        table->setHorizontalHeaderLabels({QString::fromUtf8("Працівник"),QString::fromUtf8("Посада"),QString::fromUtf8("Робочий пост"),QString::fromUtf8("Стан"),QString::fromUtf8("Прийнято"),QString::fromUtf8("Звільнено")});
        table->verticalHeader()->hide();
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->horizontalHeader()->setSectionResizeMode(0,QHeaderView::Stretch);
        table->horizontalHeader()->setSectionResizeMode(1,QHeaderView::ResizeToContents);
        table->horizontalHeader()->setSectionResizeMode(2,QHeaderView::Stretch);
        for(int col:{3,4,5})table->horizontalHeader()->setSectionResizeMode(col,QHeaderView::ResizeToContents);
        table->setSelectionMode(QAbstractItemView::SingleSelection);
        layout->addWidget(table,1);
        status=new QLabel(this);status->setObjectName("personnelPostsStatus");status->setWordWrap(true);
        layout->addWidget(status);
        auto* actions=new QHBoxLayout;
        save=new QPushButton(QString::fromUtf8("Зберегти"),this);
        save->setObjectName("savePersonnelPosts");save->setProperty("stay_in_detail_menu",true);
        auto* cancel=new QPushButton(QString::fromUtf8("Скасувати зміни"),this);
        cancel->setObjectName("cancelPersonnelPosts");
        actions->addWidget(save);actions->addWidget(cancel);actions->addStretch();layout->addLayout(actions);
        connect(cancel,&QPushButton::clicked,this,[this]{reload();});
        connect(save,&QPushButton::clicked,this,[this]{
            QMap<QString,int> assignments;
            for(int row=0;row<table->rowCount();++row)
                if(auto* choice=qobject_cast<QComboBox*>(table->cellWidget(row,2)))
                    assignments.insert(table->item(row,0)->data(Qt::UserRole).toString(),choice->currentData().toInt());
            QString error;
            const auto employees=PersonnelSettingsService::loadPersonnel(true);
            bool complete=assignments.size()==employees.size();
            for(const auto& employee:employees)complete=complete && assignments.contains(employee.id);
            if(!complete) {
                status->setText(QString::fromUtf8("Список працівників змінився або неповний. Натисніть «Скасувати зміни», щоб оновити його."));
                return;
            }
            if(!PersonnelSettingsService::saveWorkPostAssignments(assignments,&error)) {status->setText(error);return;}
            changed("staff_posts_save");reload();status->setText(QString::fromUtf8("Розподіл працівників збережено."));
        });
    }
protected:
    void showEvent(QShowEvent* event) override { QWidget::showEvent(event);reload(); }
private:
    MasterRow selectedEmployee() const {
        const auto* item=table->item(table->currentRow(),0);
        const QString id=item?item->data(Qt::UserRole).toString():QString();
        for(const auto& p:PersonnelSettingsService::loadPersonnel(false))if(p.id==id)return p;
        return {};
    }
    void editSelected(bool reinstate) {
        if(dirty){pendingWarning();return;}
        const auto person=selectedEmployee();
        if(person.id.isEmpty() || (reinstate && person.active)){status->setText(QString::fromUtf8("Оберіть працівника")+(reinstate?QString::fromUtf8(" зі списку звільнених."):QStringLiteral(".")));return;}
        QDialog dialog(this);dialog.setObjectName(reinstate?"reinstatePersonnelDialog":"editPersonnelDialog");
        dialog.setWindowTitle(reinstate?QString::fromUtf8("Поновлення працівника"):QString::fromUtf8("Редагування працівника"));
        PersonnelDialogStyle::apply(dialog);dialog.setMinimumWidth(560);auto* form=new QFormLayout(&dialog);
        auto* name=new QLineEdit(person.name,&dialog);name->setObjectName("employeeName");name->setReadOnly(reinstate);
        auto* role=new QComboBox(&dialog);role->setObjectName("employeeRole");role->addItem(QString::fromUtf8("Майстер"),"master");role->addItem(QString::fromUtf8("Адміністратор"),"admin");role->setCurrentIndex(role->findData(person.role));role->setEnabled(!reinstate);
        form->addRow(QString::fromUtf8("Повне ім’я:"),name);form->addRow(QString::fromUtf8("Посада:"),role);
        auto* date=new QDateEdit(QDate::currentDate(),&dialog);date->setObjectName("employeeReinstateDate");date->setDisplayFormat("dd.MM.yyyy");date->setCalendarPopup(true);date->setMaximumDate(QDate::currentDate());date->setVisible(reinstate);
        if(reinstate)form->addRow(QString::fromUtf8("Поновити з:"),date);
        QDateEdit* previous=nullptr;
        if(reinstate && person.activeTo.isEmpty()) {
            previous=new QDateEdit(&dialog);previous->setObjectName("previousDismissalDate");previous->setDisplayFormat("dd.MM.yyyy");previous->setCalendarPopup(true);
            previous->setMinimumDate(QDate(1900,1,1));previous->setMaximumDate(QDate::currentDate());previous->setSpecialValueText(QString::fromUtf8("Оберіть дату"));previous->setDate(previous->minimumDate());
            form->addRow(QString::fromUtf8("Попереднє звільнення:"),previous);
        }
        auto* error=new QLabel(&dialog);error->setWordWrap(true);form->addRow(error);
        auto* buttons=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel,&dialog);form->addRow(buttons);
        connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
        connect(buttons,&QDialogButtonBox::accepted,&dialog,[&]{
            if(previous && previous->date()==previous->minimumDate()){error->setText(QString::fromUtf8("Укажіть фактичну дату попереднього звільнення."));return;}
            QString message;
            const bool ok=reinstate?PersonnelEmploymentHistory::setActive(person.id,true,date->date().toString(Qt::ISODate),&message,previous?previous->date().toString(Qt::ISODate):QString())
                :PersonnelEmploymentHistory::edit(person,name->text(),role->currentData().toString(),&message);
            if(!ok){error->setText(message);return;}dialog.accept();
        });
        if(dialog.exec()==QDialog::Accepted){changed(reinstate?"staff_reinstate":"staff_edit");reload();}
    }
    void showHistory() {
        const auto person=selectedEmployee();if(person.id.isEmpty()){status->setText(QString::fromUtf8("Оберіть працівника."));return;}
        QString error;const auto history=PersonnelEmploymentHistory::load(person.id,&error);
        if(!error.isEmpty()){status->setText(error);return;}
        QDialog dialog(this);dialog.setObjectName("personnelHistoryDialog");dialog.setWindowTitle(QString::fromUtf8("Історія роботи: ")+person.name);PersonnelDialogStyle::apply(dialog);
        auto* layout=new QVBoxLayout(&dialog);auto* periods=new QTableWidget(0,4,&dialog);periods->setObjectName("employmentPeriods");
        periods->setHorizontalHeaderLabels({QString::fromUtf8("Прийнято / поновлено"),QString::fromUtf8("Звільнено"),QString::fromUtf8("Ім’я на початку періоду"),QString::fromUtf8("Посада")});
        periods->setEditTriggers(QAbstractItemView::NoEditTriggers);periods->setProperty("allowContextRowRemoval",false);periods->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);periods->horizontalHeader()->setStretchLastSection(true);
        for(const auto value:history["periods"].toArray()) {
            const auto p=value.toObject();const int r=periods->rowCount();periods->insertRow(r);
            const QStringList cells{p["from"].toString().isEmpty()?QString::fromUtf8("Не зафіксовано"):p["from"].toString(),p["to"].toString().isEmpty()?(person.active?QString::fromUtf8("Працює"):QString::fromUtf8("Не зафіксовано")):p["to"].toString(),p["name"].toString(),p["role"].toString()=="admin"?QString::fromUtf8("Адміністратор"):QString::fromUtf8("Майстер")};
            for(int c=0;c<cells.size();++c)periods->setItem(r,c,new QTableWidgetItem(cells[c]));
        }
        layout->addWidget(periods);auto* journal=new QPlainTextEdit(&dialog);journal->setReadOnly(true);journal->setObjectName("employmentChanges");
        QStringList lines;for(const auto value:history["changes"].toArray()) {
            const auto event=value.toObject();const auto before=event["before"].toObject(),after=event["after"].toObject();
            const QString operation=event["operation"].toString();
            const auto roleName=[](const QString& role){return role=="admin"?QString::fromUtf8("Адміністратор"):QString::fromUtf8("Майстер");};
            const QString time=QDateTime::fromString(event["at"].toString(),Qt::ISODate).toTimeZone(QTimeZone("Europe/Kyiv")).toString("dd.MM.yyyy HH:mm:ss");
            lines<<time+QString::fromUtf8(" (Київ) | ")+(operation=="edit"?QString::fromUtf8("Редагування"):operation=="reinstate"?QString::fromUtf8("Поновлення"):QString::fromUtf8("Звільнення"))+" | "+before["name"].toString()+" -> "+after["name"].toString()+" | "+roleName(before["role"].toString())+" -> "+roleName(after["role"].toString())+" | "+event["actor_name"].toString();
        }
        journal->setPlainText(lines.join('\n'));layout->addWidget(journal);
        auto* close=new QDialogButtonBox(QDialogButtonBox::Close,&dialog);connect(close,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);layout->addWidget(close);
        dialog.resize(850,500);dialog.exec();
    }
    void pendingWarning() {status->setText(QString::fromUtf8("Спочатку збережіть або скасуйте зміни розподілу постів."));}
    void changed(const QString& operation) {
        SettingsPageDataSupport::publishSettingsBackupEvent(operation,"personnel","settings.staff.posts",
            QString::fromUtf8("Оновлено склад працівників та розподіл постів"),{"masters","settings"});
        if(refreshAll)refreshAll();
    }
    void hire() {
        if(dirty){pendingWarning();return;}
        QDialog dialog(this);dialog.setObjectName("hirePersonnelDialog");dialog.setWindowTitle(QString::fromUtf8("Прийняття працівника"));
        PersonnelDialogStyle::apply(dialog);dialog.setMinimumWidth(520);
        auto* form=new QFormLayout(&dialog);
        auto* name=new QLineEdit(&dialog);name->setObjectName("employeeName");
        auto* role=new QComboBox(&dialog);role->setObjectName("employeeRole");role->addItem(QString::fromUtf8("Майстер"),"master");role->addItem(QString::fromUtf8("Адміністратор"),"admin");
        auto* date=new QDateEdit(QDate::currentDate(),&dialog);date->setCalendarPopup(true);date->setDisplayFormat("dd.MM.yyyy");date->setMaximumDate(QDate::currentDate());
        form->addRow(QString::fromUtf8("Повне ім’я:"),name);form->addRow(QString::fromUtf8("Посада:"),role);form->addRow(QString::fromUtf8("Прийнято з:"),date);
        auto* error=new QLabel(&dialog);error->setWordWrap(true);form->addRow(error);
        auto* buttons=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel,&dialog);form->addRow(buttons);
        connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
        connect(buttons,&QDialogButtonBox::accepted,&dialog,[&]{
            QString message;
            if(!PersonnelSettingsService::hireEmployee(name->text(),role->currentData().toString(),date->date().toString(Qt::ISODate),&message)){error->setText(message);return;}
            dialog.accept();
        });
        if(dialog.exec()==QDialog::Accepted){changed("staff_hire");reload();}
    }
    void dismissSelected() {
        if(dirty){pendingWarning();return;}
        const auto* item=table->item(table->currentRow(),0);
        const QString id=item?item->data(Qt::UserRole).toString():QString();
        MasterRow selected;for(const auto& person:PersonnelSettingsService::loadPersonnel(true))if(person.id==id)selected=person;
        if(selected.id.isEmpty()){status->setText(QString::fromUtf8("Оберіть працівника, який зараз працює."));return;}
        if(QMessageBox::question(this,QString::fromUtf8("Звільнення працівника"),
            QString::fromUtf8("Звільнити %1 сьогодні? Працівник зникне з нових призначень. Історичні наряди, відомості й оплати залишаться без змін.").arg(selected.name),
            QMessageBox::Yes|QMessageBox::Cancel,QMessageBox::Cancel)!=QMessageBox::Yes)return;
        QString error;
        if(!PersonnelSettingsService::setMasterActive(id,false,&error)){status->setText(error);return;}
        changed("staff_dismiss");reload();
    }
    void reload() {
        dirty=false;
        QString error;
        const auto assignments=PersonnelSettingsService::loadWorkPostAssignments(&error);
        table->clearSpans();table->setRowCount(0);save->setEnabled(error.isEmpty());
        if(!error.isEmpty()) {status->setText(error);return;}
        auto employees=PersonnelSettingsService::loadPersonnel(true);
        std::sort(employees.begin(),employees.end(),[](const MasterRow& a,const MasterRow& b){return QString::localeAwareCompare(a.name,b.name)<0;});
        const auto options=PersonnelSettingsService::workPostOptions();
        for(int post:{1,2,3,0}) {
            const int heading=table->rowCount();table->insertRow(heading);table->setSpan(heading,0,1,6);
            auto* label=new QTableWidgetItem(options.value(post));
            auto font=label->font();font.setBold(true);label->setFont(font);table->setItem(heading,0,label);
            for(const auto& employee:employees)if(assignments.value(employee.id,0)==post) {
                const int row=table->rowCount();table->insertRow(row);
                auto* name=new QTableWidgetItem(employee.name);name->setData(Qt::UserRole,employee.id);table->setItem(row,0,name);
                table->setItem(row,1,new QTableWidgetItem(employee.role=="admin"?QString::fromUtf8("Адміністратор"):QString::fromUtf8("Майстер")));
                table->setItem(row,3,new QTableWidgetItem(QString::fromUtf8("Працює")));
                table->setItem(row,4,new QTableWidgetItem(employee.activeFrom));
                table->setItem(row,5,new QTableWidgetItem());
                auto* choice=new QComboBox(table);choice->setObjectName("employeeWorkPost");
                for(auto it=options.begin();it!=options.end();++it)choice->addItem(it.value(),it.key());
                choice->setCurrentIndex(choice->findData(post));table->setCellWidget(row,2,choice);
                choice->ensurePolished();table->setRowHeight(row,qMax(table->verticalHeader()->defaultSectionSize(),choice->sizeHint().height()+8));
                connect(choice,&QComboBox::currentIndexChanged,this,[this]{dirty=true;status->setText(QString::fromUtf8("Є незбережені зміни."));});
            }
        }
        if(archived->isChecked()) {
            const int heading=table->rowCount();table->insertRow(heading);table->setSpan(heading,0,1,6);
            auto* label=new QTableWidgetItem(QString::fromUtf8("Звільнені працівники · історія збережена"));
            auto font=label->font();font.setBold(true);label->setFont(font);table->setItem(heading,0,label);
        }
        if(archived->isChecked())for(const auto& employee:PersonnelSettingsService::loadPersonnel(false))if(!employee.active) {
            const int row=table->rowCount();table->insertRow(row);
            auto* name=new QTableWidgetItem(employee.name);name->setData(Qt::UserRole,employee.id);table->setItem(row,0,name);
            table->setItem(row,1,new QTableWidgetItem(employee.role=="admin"?QString::fromUtf8("Адміністратор"):QString::fromUtf8("Майстер")));
            table->setItem(row,2,new QTableWidgetItem(options.value(assignments.value(employee.id,0))));
            table->setItem(row,3,new QTableWidgetItem(QString::fromUtf8("Звільнено")));
            table->setItem(row,4,new QTableWidgetItem(employee.activeFrom));table->setItem(row,5,new QTableWidgetItem(employee.activeTo));
        }
        status->setText(employees.isEmpty()?QString::fromUtf8("Немає активних працівників. Натисніть «Додати працівника»."):QString());
        save->setEnabled(!employees.isEmpty());
    }
    QTableWidget* table=nullptr;
    QLabel* status=nullptr;
    QPushButton* save=nullptr;
    QCheckBox* archived=nullptr;
    bool dirty=false;
    std::function<void()> refreshAll;
};
}

QWidget* SettingsPersonnelSection::build(const QString& id,
                                         QWidget* owner,
                                         const std::function<QWidget*()>& manageBuilder,
                                         const std::function<void()>& refreshAll)
{
    if(id==QStringLiteral("staff.posts")) return new PersonnelPostsPage(owner,refreshAll);
    if(id==QStringLiteral("staff.occupancy")) return new BayOccupancyPage(owner);
    if (id == QStringLiteral("staff.manage")) {
        return manageBuilder ? manageBuilder() : nullptr;
    }
    return nullptr;
}
