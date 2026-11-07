#include "ui/tabs/schedule/PlanningDesk.h"
#include "ui/tabs/schedule/widgets/PlanningDayCell.h"
#include "ui/tabs/schedule/PlanningPageSupport.h"
#include "core/services/planning/PlanningRepository.h"
#include "core/services/StaffAccountService.h"
#include "core/services/AccessControlService.h"
#include "core/util/PlateUtils.h"
#include "core/services/WorkshopSpareStockService.h"
#include "ui/common/dialogs/OrderDialog.h"
#include <QtWidgets>
#include <algorithm>

using PlanningDispatch::Entry;
namespace {
QString timeAt(int minute){return QTime(minute/60,minute%60).toString("HH:mm");}
QString description(const Entry& e){return QStringList{e.vehicle,e.plate,e.work}.join(" ").simplified();}
QString resource(const Entry& e,bool mechanic){return mechanic?e.mechanic:(e.bay?QStringLiteral("Пост %1").arg(e.bay):QString());}
QString interval(const Entry& e){return e.minutes?timeAt(e.start)+"–"+timeAt(e.start+e.minutes):timeAt(e.start);}
}
PlanningDesk::PlanningDesk(QWidget* parent,std::function<void(QDate)> openDayEditor):QWidget(parent),openDayEditor_(std::move(openDayEditor)){
    setObjectName("planningDesk");auto* root=new QVBoxLayout(this);root->setContentsMargins(0,0,0,0);
    auto* toolbar=new QHBoxLayout;root->addLayout(toolbar);
    auto* prev=new QPushButton("‹");auto* next=new QPushButton("›");day=new QDateEdit(QDate::currentDate());day->setCalendarPopup(true);day->setDisplayFormat("dd.MM.yy");
    toolbar->addWidget(prev);toolbar->addWidget(day);toolbar->addWidget(next);
    const QStringList labels={"День","Тиждень","Місяць"};
    for(int i=0;i<3;++i){modes[i]=new QPushButton(labels[i]);modes[i]->setCheckable(true);toolbar->addWidget(modes[i]);connect(modes[i],&QPushButton::clicked,this,[this,i]{selectPeriod(i);});}
    toolbar->addStretch();search=new QLineEdit;search->setPlaceholderText("Авто, держномер або робота");toolbar->addWidget(search);
    auto* add=new QPushButton("+ Записати авто");toolbar->addWidget(add);
    connect(add,&QPushButton::clicked,this,[this]{Entry e;e.day=day->date();e.start=540;e.minutes=60;e.bay=1;edit(e);});
    for(auto* b:{prev,next})connect(b,&QPushButton::clicked,this,[this,b,next]{int step=b==next?1:-1;day->setDate(period==2?day->date().addMonths(step):day->date().addDays(step*(period==1?7:1)));});
    auto* cards=new QHBoxLayout;root->addLayout(cards);
    const QStringList captions={"Записів за день","Завантаження постів","У черзі","Конфлікти"};
    for(int i=0;i<4;++i){auto* card=new QFrame;card->setObjectName("planningMetricCard");auto* col=new QVBoxLayout(card);col->setContentsMargins(12,5,12,5);col->setSpacing(1);
        auto* label=new QLabel(captions[i]);label->setObjectName("metricCaption");col->addWidget(label);metrics[i]=new QLabel;metrics[i]->setObjectName("metricValue");col->addWidget(metrics[i]);cards->addWidget(card,1);}
    setStyleSheet("QFrame#planningMetricCard{background:#ffffff;border:1px solid #aab6c1;border-radius:6px;} QLabel#metricCaption{background:#fff;color:#334350;font:10pt 'Segoe UI';} QLabel#metricValue{background:#fff;color:#14212d;font:bold 16pt 'Segoe UI';} QLabel#dispatchInfo{background:white;color:#172735;padding:10px;border-radius:4px;} QPushButton:checked{background:#b8d8e4;color:#132331;}");
    auto* bar=new QHBoxLayout;root->addLayout(bar);title=new QLabel;title->setStyleSheet("color:white;font-weight:bold");bar->addWidget(title,1);
    group=new QComboBox;group->addItems({"За постами","За механіками"});bar->addWidget(group);
    auto* center=new QHBoxLayout;root->addLayout(center,1);grid=new QTableWidget;grid->setObjectName("dispatchSchedule");
    grid->horizontalHeader()->setMinimumSectionSize(16);grid->verticalHeader()->hide();grid->setEditTriggers(QAbstractItemView::NoEditTriggers);grid->setSelectionMode(QAbstractItemView::NoSelection);
    grid->setStyleSheet("QTableWidget{background:white;color:#172735;gridline-color:#d3dde5;} QHeaderView::section{background:#e5edf3;color:#172735;padding:2px;}");
    center->addWidget(grid,4);
    auto* detail=new QVBoxLayout;center->addLayout(detail,1);details=new QLabel;details->setObjectName("dispatchInfo");details->setWordWrap(true);details->setTextFormat(Qt::PlainText);details->setAlignment(Qt::AlignTop);detail->addWidget(details,1);
    modify=new QPushButton("Змінити запис");detail->addWidget(modify);
    editDay=new QPushButton("Записи дня");detail->addWidget(editDay);
    connect(editDay,&QPushButton::clicked,this,[this]{openDayEditor_(selected>=0?entries[selected].day:day->date());});
    connect(modify,&QPushButton::clicked,this,[this]{if(selected<0)return;const auto& e=entries[selected];
        if((e.raw.contains('|') || e.raw.contains("[Online]",Qt::CaseInsensitive)) && !e.raw.contains(QStringLiteral(" | Запис СТО | ")))openDayEditor_(e.day);
        else edit(e);});
    openOrder=new QPushButton("Останній наряд авто");detail->addWidget(openOrder);
    auto selectedPlate=[this]{return selected<0?QString():PlanningPageSupport::extractPlanningPlateNorm(entries[selected].plate.isEmpty()?entries[selected].raw:entries[selected].plate);};
    connect(openOrder,&QPushButton::clicked,this,[this,selectedPlate]{
        const int number=PlanningPageSupport::findLatestVehicleOrderNumber(selectedPlate());
        if(number<=0){QMessageBox::information(this,"Наряд","Для цього держномера немає доступного наряду.");return;}
        OrderDialog dlg(this,number);dlg.setReadOnly(true);dlg.exec();
    });
    spares=new QPushButton("Резерви запчастин авто");detail->addWidget(spares);
    connect(spares,&QPushButton::clicked,this,[this,selectedPlate]{
        const auto plate=selectedPlate();if(plate.isEmpty())return;
        QString error;QStringList lines;
        for(const auto& part:WorkshopSpareStockService::orders(&error))
            if(PlateUtils::normalizePlate(part.plate)==plate && (part.quantity>part.received || part.received>part.issued))
                lines<<QStringLiteral("%1 · очікується %2 · у резерві %3").arg(part.name).arg(part.quantity-part.received).arg(part.received-part.issued);
        QMessageBox::information(this,"Запчастини під авто",!error.isEmpty()?error:lines.isEmpty()?QStringLiteral("Відкритих резервів для цього держномера немає."):lines.join("\n"));
    });
    auto* bottom=new QHBoxLayout;root->addLayout(bottom);
    for(const auto& name:QStringList{"Черга без часу","Потребує уваги"}){auto* col=new QVBoxLayout;bottom->addLayout(col,1);auto* caption=new QLabel(name);caption->setStyleSheet("color:white;font-weight:bold");col->addWidget(caption);
        auto* list=new QListWidget;list->setStyleSheet("QListWidget{background:#f7f9fb;color:#1a2b38;border:1px solid #aab9c6;}");list->setMaximumHeight(88);col->addWidget(list);if(name.startsWith("Черга"))waiting=list;else attention=list;}
    connect(waiting,&QListWidget::itemActivated,this,[this](auto* item){edit(queued[item->data(Qt::UserRole).toInt()]);});
    connect(attention,&QListWidget::itemClicked,this,[this](auto* item){showEntry(item->data(Qt::UserRole).toInt());});
    connect(day,&QDateEdit::dateChanged,this,[this]{refresh();});connect(group,&QComboBox::currentIndexChanged,this,[this]{render();});
    connect(search,&QLineEdit::textChanged,this,[this]{render();});
    connect(grid,&QTableWidget::cellClicked,this,[this](int r,int c){if(auto* item=grid->item(r,c)){
        if(period==2 && item->data(Qt::UserRole).toDate().isValid())selectDate(item->data(Qt::UserRole).toDate());
        else if(period==0 && item->data(Qt::UserRole).isValid())showEntry(item->data(Qt::UserRole).toInt());}});
    selectPeriod(1);
}
void PlanningDesk::selectDate(QDate d){QSignalBlocker guard(day);day->setDate(d);selectPeriod(0);}
void PlanningDesk::selectPeriod(int i){period=i;for(int k=0;k<3;++k)modes[k]->setChecked(i==k);group->setVisible(i==0);refresh();}
void PlanningDesk::refresh(){
    const auto d=day->date();first=period==1?d.addDays(1-d.dayOfWeek()):period==2?QDate(d.year(),d.month(),1):d;
    last=period==1?first.addDays(6):period==2?first.addMonths(1).addDays(-1):first;
    const auto previous=selected>=0?entries[selected]:Entry{};
    selected=-1;entries.clear();PlanningRepository repo;
    for(QDate month(first.year(),first.month(),1);month<=last;month=month.addMonths(1)){
        const auto maps=repo.visibleMonthMaps(month.year(),month.month());
        for(auto dt=maps.cbegin();dt!=maps.cend();++dt){if(dt.key()<first||dt.key()>last)continue;
            for(auto slot=dt->cbegin();slot!=dt->cend();++slot)for(const auto& raw:slot.value().split(" || ",Qt::SkipEmptyParts)){
                auto entry=PlanningDispatch::decode(dt.key(),slot.key(),raw);if(entry.cancelled)continue;
                if(previous.raw==raw && previous.day==entry.day && previous.hour==entry.hour)selected=entries.size();entries.append(entry);}}
    }
    QString error;queued=PlanningDispatch::queue(&error);render();if(!error.isEmpty())details->setText(error);
}
bool PlanningDesk::eventFilter(QObject* object,QEvent* event){
    if((event->type()==QEvent::MouseButtonRelease && static_cast<QMouseEvent*>(event)->button()==Qt::LeftButton) || (event->type()==QEvent::KeyPress && static_cast<QKeyEvent*>(event)->key()==Qt::Key_Return)){if(auto* cell=qobject_cast<PlanningDayCell*>(object)){
        const auto d=cell->date();QTimer::singleShot(0,this,[this,d]{selectDate(d);});return true;}}
    return QWidget::eventFilter(object,event);
}
void PlanningDesk::showEntry(int i){selected=i;modify->setEnabled(i>=0);
    const bool hasPlate=i>=0 && !PlanningPageSupport::extractPlanningPlateNorm(entries[i].plate.isEmpty()?entries[i].raw:entries[i].plate).isEmpty();
    openOrder->setEnabled(hasPlate && AccessControlService::currentUserHasPermission("nav.current_orders"));
    spares->setEnabled(hasPlate && AccessControlService::currentUserHasPermission("nav.inventory"));
    if(i<0){details->setText("Картка запису\n\nОберіть запис у розкладі.");return;}
    const auto& e=entries[i];details->setText(QStringLiteral("Картка запису\n\n%1\n%2\n\n%3 · %4\n%5\nМеханік: %6\n\nКлієнт: %7\nРоботи: %8")
        .arg(e.vehicle,e.plate,e.day.toString("dd.MM.yy"),interval(e),e.bay?resource(e,false):"Пост не вказано",e.mechanic.isEmpty()?"не вказано":e.mechanic,e.client.isEmpty()?"не вказано":e.client,e.work));}
void PlanningDesk::render(){
    const auto query=search->text().trimmed();auto visible=[&](const Entry& e){return description(e).contains(query,Qt::CaseInsensitive);};
    grid->clearSpans();grid->clear();grid->setRowCount(0);waiting->clear();attention->clear();
    for(int i=0;i<queued.size();++i)if(visible(queued[i])){auto* item=new QListWidgetItem(description(queued[i]),waiting);item->setData(Qt::UserRole,i);}
    int conflicts=0;QSet<int> flagged;QMap<QPair<QDate,int>,QList<QPair<int,int>>> allocations;
    for(int i=0;i<entries.size();++i){auto& a=entries[i];if(a.bay && a.minutes)allocations[{a.day,a.bay}].append({a.start,a.start+a.minutes});
        for(int j=0;j<i;++j)if(PlanningDispatch::overlaps(a,entries[j])){++conflicts;flagged.insert(i);flagged.insert(j);auto* item=new QListWidgetItem(interval(a)+" · "+a.vehicle+" / "+entries[j].vehicle,attention);item->setData(Qt::UserRole,i);}}
    int busy=0;for(auto ranges:allocations){std::sort(ranges.begin(),ranges.end());int end=480;for(auto r:ranges){busy+=qMax(0,qMin(1080,r.second)-qMax(end,r.first));end=qMax(end,r.second);}}
    if(!waiting->count()){auto* empty=new QListWidgetItem("Черга порожня",waiting);empty->setFlags(Qt::NoItemFlags);}
    if(!attention->count()){auto* empty=new QListWidgetItem("Немає виявлених конфліктів",attention);empty->setFlags(Qt::NoItemFlags);}
    metrics[0]->parentWidget()->findChild<QLabel*>("metricCaption")->setText(period==0?"Записів за день":period==1?"Записів за тиждень":"Записів за місяць");
    metrics[0]->setText(QString::number(entries.size()));metrics[1]->setText(QString::number(qRound(busy*100.0/(1800*(first.daysTo(last)+1))))+" %");
    if(!entries.isEmpty() && allocations.isEmpty())metrics[1]->setText("—");
    metrics[1]->setToolTip("Плановий час із призначеним постом і тривалістю / 3 пости × 08:00–18:00 за дні періоду.");
    metrics[2]->setText(QString::number(queued.size()));metrics[3]->setText(QString::number(conflicts));
    title->setText(QStringLiteral("Розклад майстерні · %1").arg(first.toString("dd.MM.yy"))+(first==last?"":" — "+last.toString("dd.MM.yy")));
    const QStringList weekdays={"Пн","Вт","Ср","Чт","Пт","Сб","Нд"};
    grid->horizontalHeader()->setVisible(period!=0);
    if(period!=0){
        const bool sunday=period==2 || std::any_of(entries.cbegin(),entries.cend(),[](const auto& e){return e.day.dayOfWeek()==7;});
        grid->setMaximumHeight(QWIDGETSIZE_MAX);grid->setColumnCount(sunday?7:6);grid->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);grid->setHorizontalHeaderLabels(weekdays.mid(0,grid->columnCount()));
        const int offset=first.dayOfWeek()-1;grid->setRowCount((offset+first.daysTo(last)+7)/7);
        const auto bells=PlanningRepository().getMonthReminderActiveDayKeys(first.year(),first.month())|PlanningRepository().getMonthReminderActiveDayKeys(last.year(),last.month());
        for(auto d=first;d<=last;d=d.addDays(1)){if(!sunday && d.dayOfWeek()==7)continue;QStringList lines;
            for(const auto& e:entries)if(e.day==d && visible(e))lines<<interval(e)+" "+description(e);
            const int pos=offset+first.daysTo(d),r=pos/7,c=pos%7;
            if(period==1){auto* cell=new PlanningDayCell(grid);cell->setDate(d);cell->setCurrentDate(d==QDate::currentDate());cell->setMinimumSize(70,150);cell->setPreviewText(lines.join("\n"));cell->setToolTip(lines.join("\n"));cell->setReminderActive(bells.contains(d.toString(Qt::ISODate)));cell->installEventFilter(this);cell->setFocusPolicy(Qt::StrongFocus);
                connect(cell,&PlanningDayCell::reminderBellToggled,this,[](QDate date,bool on){PlanningRepository().setDayReminderActive(date,on);});grid->setCellWidget(r,c,cell);}
            else{auto* item=new QTableWidgetItem(QString::number(d.day())+"\n"+QStringLiteral("Записів: %1").arg(lines.size()));item->setToolTip(lines.join("\n"));item->setData(Qt::UserRole,d);grid->setItem(r,c,item);}
            grid->setRowHeight(r,period==1?qMax(grid->rowHeight(r),qMax(156,grid->cellWidget(r,c)->heightForWidth(grid->columnWidth(c)))):66);
        }
    }else{
        bool byMaster=group->currentIndex()==1;QStringList resources;if(!byMaster)resources={"Пост 1","Пост 2","Пост 3"};
        else for(const auto& master:StaffAccountService::orderingMasters())resources<<master.second;
        for(const auto& e:entries){auto key=resource(e,byMaster);if(key.isEmpty())key="Без призначення";if(!resources.contains(key))resources<<key;}
        int begin=480,end=1080;for(const auto& e:entries){begin=qMin(begin,e.start/60*60);end=qMax(end,e.start+qMax(e.minutes,60));}
        int slotsCount=(end-begin+14)/15;grid->setColumnCount(slotsCount+1);grid->horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);
        grid->setColumnWidth(0,118);grid->insertRow(0);grid->setRowHeight(0,30);grid->setItem(0,0,new QTableWidgetItem("Ресурс"));
        for(int i=0;i<slotsCount;++i){
            grid->setColumnWidth(i+1,qMax(22,(grid->viewport()->width()-120)/slotsCount));
            if(i%4==0){int span=qMin(4,slotsCount-i);if(span>1)grid->setSpan(0,i+1,1,span);
                auto* tick=new QTableWidgetItem(timeAt(begin+i*15));tick->setTextAlignment(Qt::AlignCenter);grid->setItem(0,i+1,tick);}
        }
        for(const auto& key:resources){int firstRow=grid->rowCount();QList<int> ends;grid->insertRow(firstRow);ends.append(-1);
            for(int i=0;i<entries.size();++i){const auto& e=entries[i];QString name=resource(e,byMaster);if(name.isEmpty())name="Без призначення";if(name!=key||!visible(e))continue;
                int col=(e.start-begin)/15+1,span=qMax(1,(qMax(e.minutes,60*(e.minutes==0))+14)/15);int lane=0;while(lane<ends.size() && ends[lane]>col)++lane;
                if(lane==ends.size()){ends.append(-1);grid->insertRow(firstRow+lane);}ends[lane]=col+span;
                auto* item=new QTableWidgetItem(interval(e)+"\n"+description(e));item->setToolTip(description(e));item->setBackground(QColor(flagged.contains(i)?"#f9ded8":"#d9ebf4"));item->setData(Qt::UserRole,i);grid->setItem(firstRow+lane,col,item);if(span>1)grid->setSpan(firstRow+lane,col,1,span);
                auto* card=new QPushButton(interval(e)+"\n"+(e.vehicle.isEmpty()?e.work:e.vehicle)+"\n"+(byMaster?resource(e,false):e.mechanic));
                card->setToolTip(description(e));card->setAccessibleName(description(e));card->setObjectName("dispatchAppointment");
                card->setStyleSheet(QStringLiteral("QPushButton{background:%1;color:#182c39;border:1px solid #a3b7c3;border-left:3px solid %2;border-radius:5px;text-align:left;padding:3px;font:10pt 'Segoe UI';} QPushButton:focus{border:2px solid #177a98;}")
                    .arg(flagged.contains(i)?"#fbe4df":"#e0edf3",flagged.contains(i)?"#b64e40":"#477e96"));
                connect(card,&QPushButton::clicked,this,[this,i]{showEntry(i);});grid->setCellWidget(firstRow+lane,col,card);}
            grid->setItem(firstRow,0,new QTableWidgetItem(key));if(ends.size()>1)grid->setSpan(firstRow,0,ends.size(),1);for(int j=0;j<ends.size();++j)grid->setRowHeight(firstRow+j,75);
        }
    }
    int active=selected>=0&&visible(entries[selected])?selected:-1;
    if(active<0 && period==0)for(int i=0;i<entries.size();++i)if(visible(entries[i])){active=i;break;}
    showEntry(active);
}
void PlanningDesk::edit(Entry original){
    QDialog dlg(this);dlg.setWindowTitle(original.raw.isEmpty()?"Записати авто":"Змінити запис");
    QFormLayout form(&dlg);
    auto* car=new QLineEdit(original.vehicle);auto* plate=new QLineEdit(original.plate);auto* work=new QLineEdit(original.work);auto* client=new QLineEdit(original.client);
    auto* queuedOnly=new QCheckBox("До черги без часу");queuedOnly->setChecked(!original.queueId.isEmpty());
    auto* when=new QDateEdit(original.day.isValid()?original.day:day->date());when->setCalendarPopup(true);when->setDisplayFormat("dd.MM.yyyy");
    auto* at=new QTimeEdit(QTime(original.start/60,original.start%60));at->setDisplayFormat("HH:mm");
    auto* duration=new QSpinBox;duration->setRange(15,600);duration->setSingleStep(15);duration->setValue(qMax(15,original.minutes));duration->setSuffix(" хв");
    auto* bay=new QComboBox;bay->addItems({"Пост 1","Пост 2","Пост 3"});bay->setCurrentIndex(qBound(0,original.bay-1,2));
    auto* master=new QComboBox;const auto mine=AccessControlService::currentMasterId();const bool limited=AccessControlService::currentActorIsMaster();
    if(!limited)master->addItem("Не призначено",QString());
    for(const auto& m:StaffAccountService::orderingMasters())if(!limited||m.first==mine)master->addItem(m.second,m.first);
    if(!original.mechanicId.isEmpty())master->setCurrentIndex(master->findData(original.mechanicId));
    form.addRow("Автомобіль",car);form.addRow("Держномер",plate);form.addRow("Клієнт",client);form.addRow("Робота",work);
    form.addRow(queuedOnly);form.addRow("Дата",when);form.addRow("Початок",at);form.addRow("Тривалість",duration);form.addRow("Пост",bay);form.addRow("Механік",master);
    auto scheduleFields=[=](bool queue){when->setDisabled(queue);at->setDisabled(queue);};scheduleFields(queuedOnly->isChecked());connect(queuedOnly,&QCheckBox::toggled,&dlg,scheduleFields);
    QDialogButtonBox buttons(QDialogButtonBox::Save|QDialogButtonBox::Cancel);buttons.button(QDialogButtonBox::Save)->setText("Зберегти");buttons.button(QDialogButtonBox::Cancel)->setText("Скасувати");form.addRow(&buttons);
    connect(&buttons,&QDialogButtonBox::rejected,&dlg,&QDialog::reject);
    connect(&buttons,&QDialogButtonBox::accepted,&dlg,[&]{
        Entry e;e.day=queuedOnly->isChecked()?QDate():when->date();e.start=queuedOnly->isChecked()?480:at->time().hour()*60+at->time().minute();
        e.work=PlanningPageSupport::normalizePlanningSlotText(work->text(),true);e.vehicle=car->text().trimmed();e.client=client->text().trimmed();e.plate=PlateUtils::formatPlateDisplay(plate->text().trimmed());
        e.minutes=duration->value();e.bay=bay->currentIndex()+1;e.mechanicId=master->currentData().toString();e.mechanic=e.mechanicId.isEmpty()?QString():master->currentText();
        QString error;if(!PlanningDispatch::save(original,e,&error)){QMessageBox::warning(&dlg,"Планування",error);return;}
        dlg.accept();if(e.day.isValid()){selectDate(e.day);for(int i=0;i<entries.size();++i)if(entries[i].raw==PlanningDispatch::encode(e)){showEntry(i);break;}}else refresh();
    });dlg.exec();
}
