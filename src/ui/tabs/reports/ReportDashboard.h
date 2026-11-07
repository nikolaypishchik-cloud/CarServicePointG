#pragma once

#include <QDateEdit>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QPainter>
#include <QPainterPath>
#include <QSignalBlocker>
#include <QRegularExpression>
#include <QStyle>
#include <QVBoxLayout>
#include <functional>

// Presentation-only header. Dates and search stay synchronized with report inputs.
class ReportDashboard final : public QWidget {
public:
    explicit ReportDashboard(QWidget* parent=nullptr) : QWidget(parent) {
        setObjectName("reportDashboard");
        auto* root=new QVBoxLayout(this);
        root->setContentsMargins(4,qRound(logicalDpiY()/25.4),4,qRound(logicalDpiY()/25.4)); root->setSpacing(0);
        m_row=new QHBoxLayout; m_row->setSpacing(6); root->addLayout(m_row);root->setAlignment(Qt::AlignTop);
        auto* dates=tile("reportDateTile");
        m_datesLayout=new QGridLayout(dates); auto* dl=m_datesLayout;
        dl->setContentsMargins(8,8,8,8); dl->setSpacing(2);
        m_dateTitle=new QLabel(QString::fromUtf8("Період"),dates);
        m_dateIcon=new QLabel(dates);m_dateIcon->setFixedSize(32,40);m_dateIcon->hide();
        m_dateRule=new QFrame(dates);m_dateRule->setFrameShape(QFrame::HLine);m_dateRule->hide();
        m_fromLabel=new QLabel(QString::fromUtf8("З:"),dates);
        m_toLabel=new QLabel(QString::fromUtf8("По:"),dates);
        from=new QDateEdit(dates); to=new QDateEdit(dates);
        for(auto* date:{from,to}) {
            date->setDisplayFormat("dd.MM.yy"); date->setCalendarPopup(true);
            date->setKeyboardTracking(false);
            date->setDateRange(QDate(1900,1,1),QDate(9999,12,31));
            date->setFixedHeight(qRound(7.0*logicalDpiY()/25.4));
            date->setFixedWidth(qMax(110,date->fontMetrics().horizontalAdvance("00.00.00")+44));
        }
        from->setObjectName("dashboardFrom"); to->setObjectName("dashboardTo");
        dl->addWidget(m_fromLabel,0,0);dl->addWidget(from,0,1);dl->addWidget(m_toLabel,0,2);dl->addWidget(to,0,3);
        m_dateTitle->hide();
        m_row->addWidget(dates);
        m_metrics=new QWidget(this);m_metrics->setObjectName("reportMetricsHost");m_metrics->setStyleSheet("#reportMetricsHost {background:transparent;border:none;}"); m_metricRow=new QHBoxLayout(m_metrics);
        m_metricRow->setContentsMargins(0,0,0,0); m_metricRow->setSpacing(6);
        for(int i=0;i<4;++i) {
            auto* card=tile(QString("reportMetric%1").arg(i));
            auto* line=new QGridLayout(card);m_metricLayouts[i]=line; line->setContentsMargins(8,8,8,8); line->setSpacing(6);line->setAlignment(Qt::AlignVCenter);
            auto* icon=new QLabel(card);m_icons[i]=icon;
            icon->setFixedSize(32,40);
            line->addWidget(icon,0,0,2,1);
            auto* rule=new QFrame(card);m_rules[i]=rule;rule->setFrameShape(QFrame::VLine);rule->setStyleSheet("background:#a4cbd3;max-width:1px;");line->addWidget(rule,0,1,2,1);
            m_titles[i]=new QLabel(card);m_titles[i]->setWordWrap(true);m_titles[i]->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);
            m_values[i]=new QLabel(card);m_values[i]->setWordWrap(true);m_values[i]->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);
            auto font=m_values[i]->font();font.setPointSizeF(qMax(12.0,font.pointSizeF()+3));font.setBold(true);m_values[i]->setFont(font);
            m_values[i]->setObjectName(QString("reportMetricValue%1").arg(i));
            m_values[i]->setProperty("reportValue",true);
            m_values[i]->setAlignment(Qt::AlignCenter);
            line->addWidget(m_titles[i],0,2);line->addWidget(m_values[i],1,2);line->setColumnStretch(2,1);
            m_cards[i]=card;m_metricRow->addWidget(card,1);
        }
        m_row->addWidget(m_metrics,4);
        m_parts=new QWidget(this);
        m_parts->setObjectName("reportPartsHost");m_parts->setStyleSheet("#reportPartsHost {background:transparent;border:none;}");
        auto* partsRow=new QHBoxLayout(m_parts);partsRow->setContentsMargins(0,0,0,0);partsRow->setSpacing(6);
        for(int i=0;i<4;++i){
            auto* card=tile(QString("reportPartsTile%1").arg(i));
            auto* column=new QVBoxLayout(card);column->setContentsMargins(10,6,10,6);column->setSpacing(4);
            auto* heading=new QHBoxLayout;
            m_partsIcons[i]=new QLabel(card);m_partsIcons[i]->setFixedSize(32,40);heading->addWidget(m_partsIcons[i]);
            m_partsTitles[i]=new QLabel(card);m_partsTitles[i]->setWordWrap(true);heading->addWidget(m_partsTitles[i],1);
            column->addLayout(heading);
            auto* rule=new QFrame(card);rule->setFrameShape(QFrame::HLine);column->addWidget(rule);
            m_partsBodies[i]=new QLabel(card);m_partsBodies[i]->setWordWrap(true);m_partsBodies[i]->setTextFormat(Qt::RichText);
            m_partsBodies[i]->setAlignment(Qt::AlignCenter);column->addWidget(m_partsBodies[i],1);
            if(i<2)card->setFixedWidth(qRound(60.0*logicalDpiX()/25.4));
            else card->setMinimumWidth(240);
            partsRow->addWidget(card,i<2?0:1);
        }
        m_row->addWidget(m_parts,4);m_parts->hide();
        m_actions=new QWidget(this);m_actions->setObjectName("reportActionsHost");m_actions->setStyleSheet("#reportActionsHost {background:transparent;border:none;}");auto* actions=new QHBoxLayout(m_actions);actions->setContentsMargins(0,0,0,0);actions->setSpacing(6);
        const QStringList titles={QString::fromUtf8("Сума"),QString::fromUtf8("Наряди"),QString::fromUtf8("Дії")};
        const QList<QStringList> captions={{QString::fromUtf8("Сьогодні"),QString::fromUtf8("Тиждень"),QString::fromUtf8("Місяць")},
            {QString::fromUtf8("Усі"),QString::fromUtf8("Відкриті"),QString::fromUtf8("Закриті")},
            {QString::fromUtf8("Оновити"),QString::fromUtf8("Скинути")}};
        for(int group=0;group<3;++group) {
            auto* card=tile(QString("reportAction%1").arg(group));auto* outer=new QVBoxLayout(card);outer->setContentsMargins(8,8,8,8);outer->setSpacing(6);outer->setAlignment(Qt::AlignVCenter);
            auto* heading=new QHBoxLayout;auto* icon=new QLabel(card);m_actionIcons[group]=icon;icon->setFixedSize(32,40);heading->addWidget(icon);heading->addWidget(new QLabel(titles[group],card),1);outer->addLayout(heading);
            auto* rule=new QFrame(card);rule->setFrameShape(QFrame::HLine);outer->addWidget(rule);
            auto* buttons=new QHBoxLayout;buttons->setSpacing(4);outer->addLayout(buttons);
            if(group==0) {
                m_clientReceived=new QLabel(QString::fromUtf8("—"),card);
                m_clientReceived->setObjectName("reportClientReceived");
                m_clientReceived->setAlignment(Qt::AlignCenter);
                auto font=m_clientReceived->font();font.setPointSize(18);font.setBold(true);m_clientReceived->setFont(font);
                m_clientReceived->setToolTip(QString::fromUtf8("Зареєстровані оплати клієнтів за відібраними нарядами, з урахуванням пошуку та статусу. Дата платежу може бути поза періодом. Історичні оплати без записів не враховуються."));
                buttons->addWidget(m_clientReceived);
            }
            for(int i=0;i<captions[group].size();++i) {
                if(group==0)break;
                auto* button=new QPushButton(captions[group][i],card);buttons->addWidget(button);
                button->setStyleSheet("QPushButton {background:#435564;border:1px solid #a4bbc8;border-radius:6px;padding:2px 5px;color:white;min-width:0px;} QPushButton:checked {background:#78c9ed;color:black;}");
                button->setFixedHeight(qRound(7.0*logicalDpiY()/25.4));
                button->setObjectName(QString("reportAction%1_%2").arg(group).arg(i));
                if(group==1){button->setCheckable(true);m_statusButtons[i]=button;}
                connect(button,&QPushButton::clicked,this,[this,group,i]{if(action)action(group,i);});
            }
            actions->addWidget(card,1);
        }
        m_row->addWidget(m_actions,4);
        m_searchTile=tile("reportSearchTile");auto* sl=new QVBoxLayout(m_searchTile);sl->setContentsMargins(8,8,8,8);sl->setAlignment(Qt::AlignVCenter);sl->setSpacing(6);
        auto* searchHeading=new QHBoxLayout;m_searchIcon=new QLabel(m_searchTile);m_searchIcon->setFixedSize(32,40);m_searchIcon->hide();searchHeading->addWidget(m_searchIcon);searchHeading->addWidget(new QLabel(QString::fromUtf8("Пошук"),m_searchTile),1);sl->addLayout(searchHeading);
        m_searchRule=new QFrame(m_searchTile);m_searchRule->setFrameShape(QFrame::HLine);sl->addWidget(m_searchRule);
        search=new QLineEdit(m_searchTile);search->setObjectName("dashboardSearch");search->setClearButtonEnabled(true);
        search->setMinimumWidth(100);search->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Fixed);search->setFixedHeight(qRound(7.0*logicalDpiY()/25.4));sl->addWidget(search);
        m_row->insertWidget(1,m_searchTile,1);
        controlsRow=new QHBoxLayout;controlsRow->setContentsMargins(0,0,0,0);root->addLayout(controlsRow);
    }
    void setInvoiceControls(QWidget* host) {
        const QStringList names={"invoiceStatusFilter","invoiceSort","invoiceRefresh"};
        const QStringList titles={QString::fromUtf8("Показати"),QString::fromUtf8("Сортування"),QString::fromUtf8("Дії")};
        for(int i=0;i<3;++i){
            auto* field=host->findChild<QWidget*>(names[i]);
            auto* card=tile(QString("reportInvoiceControl%1").arg(i));
            auto* layout=new QVBoxLayout(card);layout->setContentsMargins(8,8,8,8);layout->setSpacing(6);layout->setAlignment(Qt::AlignVCenter);
            auto* heading=new QHBoxLayout;m_invoiceIcons[i]=new QLabel(card);m_invoiceIcons[i]->setFixedSize(32,40);heading->addWidget(m_invoiceIcons[i]);heading->addWidget(new QLabel(titles[i],card),1);layout->addLayout(heading);
            auto* rule=new QFrame(card);rule->setFrameShape(QFrame::HLine);layout->addWidget(rule);layout->addWidget(field);
            field->setMinimumWidth(0);field->setMaximumWidth(QWIDGETSIZE_MAX);
            field->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Fixed);
            field->setFixedHeight(qRound(7.0*logicalDpiY()/25.4));
            const QString sample=i==0?QString::fromUtf8("Усі накладні"):i==1?QString::fromUtf8("За датою ↓"):QString::fromUtf8("Оновити");
            card->setMinimumWidth(field->fontMetrics().horizontalAdvance(sample)+60);
            m_invoiceCards[i]=card;m_row->addWidget(card,1);card->hide();
        }
    }
    int compactHeight() const {return tileHeight()+2*qRound(logicalDpiY()/25.4);}
    void setExpenseControls(QWidget* controls) {
        const auto buttons=controls->findChildren<QPushButton*>();
        delete controls->layout();
        controls->setStyleSheet({});
        controls->setProperty("reportTile",true);
        controls->setAttribute(Qt::WA_StyledBackground);
        auto* layout=new QVBoxLayout(controls);
        layout->setContentsMargins(8,4,8,4);layout->setSpacing(4);
        auto* heading=new QHBoxLayout;m_expenseIcon=new QLabel(controls);m_expenseIcon->setFixedSize(32,24);heading->addWidget(m_expenseIcon);heading->addWidget(new QLabel(QString::fromUtf8("Дії"),controls),1);layout->addLayout(heading);
        auto* rule=new QFrame(controls);rule->setFrameShape(QFrame::HLine);layout->addWidget(rule);
        auto* buttonRow=new QHBoxLayout;buttonRow->setSpacing(4);layout->addLayout(buttonRow);
        for(auto* button:buttons){
            const bool add=button->objectName()=="addAdditionalExpense";
            button->setIcon(QIcon(symbol(add?11:12,QColor(add?"#8bdfee":"#ef766d"))));
            button->setIconSize(QSize(24,24));
            button->setMinimumWidth(button->fontMetrics().horizontalAdvance(button->text())+54);
            button->setFixedHeight(qRound(7.0*logicalDpiY()/25.4));
            buttonRow->addWidget(button,1);
        }
        layout->addStretch();
        m_row->addWidget(controls,2);
    }
    static bool supports(int tab){return tab>=0&&tab<=7;}
    void parts(const QStringList& titles,const QStringList& bodies){
        const QStringList defaults={QString::fromUtf8("Олива моторна"),QString::fromUtf8("Антифриз"),QString::fromUtf8("Запчастини · постачання"),QString::fromUtf8("Запчастини · використання")};
        for(int i=0;i<4;++i){
            m_partsTitles[i]->setText(titles.value(i,defaults[i]));
            auto* label=m_partsBodies[i];
            label->setStyleSheet("font-family:'Times New Roman';font-size:11pt;padding:0px;");
            label->ensurePolished();
            const qreal size=label->font().pointSizeF()>0?label->font().pointSizeF():11.0;
            QString body=bodies.value(i,QString::fromUtf8("Немає даних"));
            body.replace("REPORT_TILE_VALUE_GAP",QString::number(qMax(1,qRound(5.0*logicalDpiX()/25.4))));
            body.replace("<b>",QString("<span style='font-size:%1pt;font-weight:700;'>").arg(size+1.0));
            body.replace("</b>","</span>");
            const QString paragraph="<p style='margin:0;line-height:90%;'>";
            if(body.contains("<table")){
                body.replace(QRegularExpression("(<td[^>]*>)"),"\\1"+paragraph);
                body.replace("</td>","</p></td>");
            }else body=paragraph+body+"</p>";
            label->setText(body);
        }
    }
    void configure(int tab,bool light) {
        m_tab=tab;
        for(auto* field:QList<QWidget*>{m_dateTitle,m_fromLabel,from,m_toLabel,to,m_dateIcon,m_dateRule})m_datesLayout->removeWidget(field);
        m_dateTitle->show();
        m_datesLayout->setAlignment(Qt::AlignVCenter);
        m_datesLayout->setVerticalSpacing(6);
        m_fromLabel->hide();m_toLabel->hide();
        m_dateIcon->show();m_dateRule->show();
        m_datesLayout->addWidget(m_dateIcon,0,0);
        m_datesLayout->addWidget(m_dateTitle,0,1,1,3);
        m_dateTitle->setMinimumWidth(m_dateTitle->fontMetrics().horizontalAdvance(m_dateTitle->text()));
        m_datesLayout->addWidget(m_dateRule,1,0,1,4);
        m_datesLayout->addWidget(from,2,0,1,2,Qt::AlignVCenter);m_datesLayout->addWidget(to,2,2,1,2,Qt::AlignVCenter);
        from->setAccessibleName(QString::fromUtf8("Початок періоду"));to->setAccessibleName(QString::fromUtf8("Кінець періоду"));
        from->setToolTip(from->accessibleName());to->setToolTip(to->accessibleName());
        m_row->removeWidget(m_searchTile);m_row->insertWidget(1,m_searchTile,1);
        auto* dates=m_datesLayout->parentWidget();
        dates->setMinimumWidth(0);m_searchTile->setMinimumWidth(0);
        m_row->setStretchFactor(dates,0);
        m_row->setStretchFactor(m_metrics,tab==6?3:4);
        // Invoice tiles share one layout so equal stretch also means equal width.
        for(int i=0;i<4;++i){
            if(tab==2){
                m_metricRow->removeWidget(m_cards[i]);
                m_row->removeWidget(m_cards[i]);
                m_row->insertWidget(2+i,m_cards[i],1);
            }else if(m_cards[i]->parentWidget()!=m_metrics){
                m_row->removeWidget(m_cards[i]);
                m_metricRow->addWidget(m_cards[i],1);
            }
        }
        for(auto* card:m_invoiceCards)if(card)card->setVisible(tab==2);
        setStyleSheet(QString("#reportDashboard {background:transparent;} QWidget[reportTile=\"true\"] {border:1px solid black;border-radius:4px;background:transparent;} #reportDashboard QLabel {background:transparent;border:none;color:%1;} #reportDashboard QDateEdit,#reportDashboard QLineEdit,#reportDashboard QComboBox {background:white;color:black;border:1px solid #bdc7d0;border-radius:7px;padding:2px 5px;} #reportDashboard QPushButton {min-width:0px;min-height:0px;padding:2px 7px;color:%1;} #reportDashboard QPushButton:checked {background:#78c9ed;color:#111111;}").arg(light?"#111111":"#ffffff"));
        for(auto* date:{from,to}){
            date->ensurePolished();
            // Leave room for the native calendar arrow, frame and text padding.
            date->setFixedWidth(date->fontMetrics().horizontalAdvance("00.00.00")+44);
        }
        for(auto* value:m_values)value->setStyleSheet(QString("background:transparent;border:none;font-size:%1pt;font-weight:bold;color:%2;").arg(tab==2?13:15).arg(light?"#111111":"#ffffff"));
        if(tab==5)m_values[2]->setStyleSheet(QString("background:transparent;border:none;font-size:10pt;font-weight:normal;color:%1;").arg(light?"#111111":"#ffffff"));
        m_actions->setVisible(tab==1);m_metrics->setVisible(tab!=1&&tab!=2&&tab!=3);m_searchTile->setVisible(tab==1||tab==2);
        m_parts->setVisible(tab==3);
        const QColor accent=light?QColor("#177a92"):QColor(tab==2?"#86bff1":"#8bdfee");
        m_dateIcon->setPixmap(symbol(9,accent));
        for(int i=0;i<3;++i)m_actionIcons[i]->setPixmap(symbol(i==2?5:0,accent));
        if(m_expenseIcon){auto pm=symbol(5,accent);m_expenseIcon->setPixmap(pm.scaled(QSize(24,24)*pm.devicePixelRatio(),Qt::KeepAspectRatio,Qt::SmoothTransformation));}
        for(int i=0;i<4;++i)m_partsIcons[i]->setPixmap(symbol(QList<int>{13,14,10,4}[i],accent));
        for(auto* card:findChildren<QWidget*>())if(card->property("reportTile").toBool())card->setFixedHeight(tileHeight());
        for(auto* button:m_actions->findChildren<QPushButton*>()){
            button->ensurePolished();
            button->setMinimumWidth(button->fontMetrics().horizontalAdvance(button->text())+16);
        }
        if(auto* controls=findChild<QWidget*>("additionalExpensesFormScroll")){
            for(auto* button:controls->findChildren<QPushButton*>()){
                button->ensurePolished();
                button->setMinimumWidth(button->fontMetrics().horizontalAdvance(button->text())+54);
            }
        }
        QStringList titles;
        if(tab==0)titles={QString::fromUtf8("Нарядів"),QString::fromUtf8("Роботи"),QString::fromUtf8("Запчастини"),QString::fromUtf8("Разом за нарядами")};
        if(tab==2)titles={QString::fromUtf8("Накладних"),QString::fromUtf8("Сплачено"),QString::fromUtf8("Не сплачено"),QString::fromUtf8("Залишок")};
        if(tab==4)titles={QString::fromUtf8("Відправлено на ремонт"),QString::fromUtf8("Сума ремонту"),QString::fromUtf8("Маржа")};
        if(tab==5)titles={QString::fromUtf8("Витрати за період"),QString::fromUtf8("Електроенергія"),QString::fromUtf8("За цей період")};
        if(tab==6)titles={QString::fromUtf8("Сума витрат"),QString::fromUtf8("Записів витрат"),QString::fromUtf8("Нова пошта")};
        if(tab==7)titles={QString::fromUtf8("Співробітників у звіті"),QString::fromUtf8("Нараховано за період"),QString::fromUtf8("Обраний період")};
        for(int i=0;i<4;++i){
            auto* grid=m_metricLayouts[i];
            for(auto* widget:QList<QWidget*>{m_icons[i],m_rules[i],m_titles[i],m_values[i]})grid->removeWidget(widget);
            grid->setColumnStretch(1,1);grid->setColumnStretch(2,0);
            grid->addWidget(m_icons[i],0,0);grid->addWidget(m_titles[i],0,1);grid->addWidget(m_rules[i],1,0,1,2);grid->addWidget(m_values[i],2,0,1,2);
            m_rules[i]->setFrameShape(QFrame::HLine);m_rules[i]->setStyleSheet({});
            m_icons[i]->show();m_rules[i]->show();
            m_titles[i]->setMinimumHeight(0);
            m_titles[i]->setWordWrap(true);
            m_cards[i]->setMinimumWidth(tab==2?80:0);
            m_metricRow->setStretch(i,tab==2&&i==3?2:1);
            m_cards[i]->setVisible(i<titles.size());m_titles[i]->setText(titles.value(i));
            const QList<int> kinds=tab==0?QList<int>{0,4,5,1}:tab==2?QList<int>{0,2,3,1}:tab==4?QList<int>{4,1,6}:tab==5?QList<int>{1,7,0}:QList<int>{8,1,9};
            m_icons[i]->setPixmap(symbol(tab==6?QList<int>{1,0,10}.value(i):kinds.value(i),accent));
            if(tab==2 && (i==1||i==2))m_titles[i]->setText(QString::fromUtf8("<span style='color:%1'>●</span> %2").arg(i==1?"#57dc38":"#f3a000",titles[i]));
        }
        if(tab==2){
            dates->setMinimumWidth(m_datesLayout->minimumSize().width());
            m_searchTile->setMinimumWidth(110);
            m_searchTile->setMaximumWidth(170);
            for(int i=0;i<3;++i)if(m_invoiceCards[i])m_invoiceCards[i]->setMinimumWidth(i==2?95:120);
        }else{
            m_searchTile->setMaximumWidth(QWIDGETSIZE_MAX);
        }
        m_searchIcon->show();m_searchIcon->setPixmap(symbol(15,accent));m_searchRule->show();
        for(int i=0;i<3;++i)if(m_invoiceIcons[i])m_invoiceIcons[i]->setPixmap(symbol(16+i,accent));
        if(tab==6){
            int width=0;
            for(int i=0;i<3;++i){
                m_titles[i]->ensurePolished();
                width=qMax(width,m_titles[i]->fontMetrics().horizontalAdvance(m_titles[i]->text())+90);
            }
            for(int i=0;i<3;++i)m_cards[i]->setMinimumWidth(width);
        }
        // Apply the shared control height after styles have set their size constraints.
        for(auto* button:findChildren<QPushButton*>()){
            button->ensurePolished();
            button->setFixedHeight(qRound(7.0*logicalDpiY()/25.4));
        }
        unifySections();
    }
    void values(const QStringList& values){
        for(int i=0;i<4;++i){
            auto* label=m_values[i];
            label->setText(values.value(i,QString::fromUtf8("—")));
            label->ensurePolished();
            int width=0;
            for(const auto& line:label->text().split('\n'))width=qMax(width,label->fontMetrics().horizontalAdvance(line));
            m_cards[i]->setMinimumWidth(qMax(m_cards[i]->minimumWidth(),width+20));
        }
    }
    void clientReceived(const QString& value){m_clientReceived->setText(value);}
    void status(int selected){for(int i=0;i<3;++i)m_statusButtons[i]->setChecked(i==selected);}
    QDateEdit *from=nullptr,*to=nullptr;
    QLineEdit* search=nullptr;
    QHBoxLayout* controlsRow=nullptr;
    std::function<void(int,int)> action;
private:
    int tileHeight() const {return qRound(7.0*logicalDpiY()/25.4)+qRound(9.0*logicalDpiY()/25.4);}
    void unifySections() {
        const int header=qRound(7.0*logicalDpiY()/25.4);
        const int body=qRound(9.0*logicalDpiY()/25.4);
        const int inset=qRound(logicalDpiX()/25.4);
        for(auto* label:m_partsBodies)label->setFixedHeight(body);
        for(auto* icon:findChildren<QLabel*>())if(!icon->pixmap().isNull()){
            auto pm=icon->pixmap();
            const QSize size(header-4,header-4);
            icon->setFixedSize(size);
            icon->setPixmap(pm.scaled(size*pm.devicePixelRatio(),Qt::KeepAspectRatio,Qt::SmoothTransformation));
        }
        for(auto* card:findChildren<QWidget*>()){
            if(!card->property("reportTile").toBool())continue;
            card->setFixedHeight(header+body);
            if(auto* grid=qobject_cast<QGridLayout*>(card->layout())){
                grid->setContentsMargins(inset,0,inset,0);grid->setVerticalSpacing(0);
                grid->setAlignment({});
                grid->setRowMinimumHeight(0,header-1);grid->setRowMinimumHeight(1,1);grid->setRowMinimumHeight(2,body);
                for(int row=0;row<3;++row)grid->setRowStretch(row,0);
                for(int i=0;i<grid->count();++i){
                    int row,col,rs,cs;grid->getItemPosition(i,&row,&col,&rs,&cs);
                    if(auto* widget=grid->itemAt(i)->widget()){
                        if(row==1)widget->setFixedHeight(1);
                        else if(auto* label=qobject_cast<QLabel*>(widget))label->setMaximumHeight(row==0?header-1:body);
                    }
                }
            }else if(auto* column=qobject_cast<QVBoxLayout*>(card->layout())){
                if(!card->property("measuredSections").toBool()){
                    auto* head=new QWidget(card);head->setObjectName("reportTileHeading");head->setStyleSheet("#reportTileHeading {background:transparent;border:none;}");auto* hl=new QVBoxLayout(head);hl->setContentsMargins(0,0,0,0);hl->setSpacing(0);
                    auto* item=column->takeAt(0);
                    if(item->layout())hl->addLayout(item->layout());else hl->addItem(item);
                    auto* ruleItem=column->takeAt(0);
                    auto* content=new QWidget(card);content->setObjectName("reportTileContent");content->setStyleSheet("#reportTileContent {background:transparent;border:none;}");auto* bl=new QVBoxLayout(content);bl->setContentsMargins(0,0,0,0);bl->setSpacing(0);bl->setAlignment(Qt::AlignVCenter);
                    while(auto* next=column->takeAt(0)){
                        if(next->spacerItem()){delete next;continue;}
                        if(next->layout())bl->addLayout(next->layout());
                        else if(next->widget()){bl->addWidget(next->widget());delete next;}
                        else bl->addItem(next);
                    }
                    head->setFixedHeight(header-1);content->setFixedHeight(body);
                    column->addWidget(head);column->addItem(ruleItem);column->addWidget(content);
                    if(ruleItem->widget())ruleItem->widget()->setFixedHeight(1);
                    card->setProperty("measuredSections",true);
                }
                column->setContentsMargins(inset,0,inset,0);column->setSpacing(0);column->setAlignment({});
            }
        }
        if(m_tab==2){
            for(int i=0;i<4;++i){
                m_titles[i]->setWordWrap(false);
                m_titles[i]->ensurePolished();
                QString plain=m_titles[i]->text();plain.remove(QRegularExpression("<[^>]*>"));
                const int title=m_titles[i]->fontMetrics().horizontalAdvance(plain);
                m_cards[i]->setMinimumWidth(title+header+2*inset);
                m_cards[i]->setMaximumWidth(QWIDGETSIZE_MAX);
            }
        }else for(auto* card:m_cards)card->setMaximumWidth(QWIDGETSIZE_MAX);
    }
    QPixmap symbol(int kind,QColor color) const {
        const qreal dpr=devicePixelRatioF();QPixmap pm(QSize(32,40)*dpr);pm.setDevicePixelRatio(dpr);pm.fill(Qt::transparent);
        QPainter p(&pm);p.setRenderHint(QPainter::Antialiasing);p.setPen(QPen(color,1.5));p.setBrush(Qt::NoBrush);
        if(kind==13){p.drawPolygon(QPolygonF{{4,15},{19,15},{25,11},{29,12},{25,25},{10,25},{4,20}});p.drawRect(5,10,7,5);p.drawLine(8,7,8,10);p.drawLine(4,7,12,7);}
        else if(kind==14){QPainterPath path;path.moveTo(16,5);path.cubicTo(13,12,5,19,5,25);path.cubicTo(5,39,27,39,27,25);path.cubicTo(27,19,19,12,16,5);p.drawPath(path);}
        else if(kind==15){p.drawEllipse(QRectF(3,5,20,20));p.drawLine(20,23,30,34);}
        else if(kind==16){p.drawPolygon(QPolygonF{{3,7},{29,7},{19,20},{19,34},{13,30},{13,20}});}
        else if(kind==17){p.drawPolyline(QPolygonF{{3,13},{9,6},{15,13}});p.drawLine(9,6,9,33);p.drawPolyline(QPolygonF{{17,27},{23,34},{29,27}});p.drawLine(23,7,23,34);}
        else if(kind==18){p.drawArc(QRectF(4,8,24,24),35*16,280*16);p.drawPolyline(QPolygonF{{22,6},{28,12},{29,4}});}
        else if(kind==11){p.drawLine(16,8,16,32);p.drawLine(4,20,28,20);}
        else if(kind==12){p.drawRect(8,12,16,22);p.drawLine(5,10,27,10);p.drawLine(12,6,20,6);p.drawLine(13,16,13,29);p.drawLine(19,16,19,29);}
        else if(kind==1){for(int y:{11,17,23}){p.drawEllipse(QRectF(4,y,24,8));if(y<23){p.drawLine(4,y+4,4,y+10);p.drawLine(28,y+4,28,y+10);}}}
        else if(kind==2||kind==3){p.drawEllipse(QRectF(3,7,26,26));if(kind==2){p.drawLine(9,20,14,25);p.drawLine(14,25,24,14);}else{p.drawLine(9,14,23,27);p.drawLine(9,27,23,14);}}
        else if(kind==4){p.drawLine(7,32,23,16);p.drawEllipse(QRectF(3,29,5,5));p.drawArc(QRectF(17,5,12,12),-60*16,270*16);}
        else if(kind==5){p.drawEllipse(QRectF(8,12,16,16));p.drawEllipse(QRectF(12,16,8,8));for(int a=0;a<8;++a){p.save();p.translate(16,20);p.rotate(a*45);p.drawLine(0,-9,0,-14);p.restore();}}
        else if(kind==6){for(int i=0;i<3;++i)p.drawRect(4+i*9,25-i*7,5,10+i*7);}
        else if(kind==7){p.drawPolyline(QPolygonF{{20,4},{7,22},{16,22},{12,36},{27,16},{18,16},{20,4}});}
        else if(kind==8){p.drawEllipse(QRectF(9,6,12,12));p.drawArc(QRectF(3,21,26,21),0,180*16);p.drawLine(3,31,29,31);}
        else if(kind==10){p.drawPolygon(QPolygonF{{3,12},{16,5},{29,12},{29,28},{16,35},{3,28}});p.drawPolyline(QPolygonF{{3,12},{16,19},{29,12}});p.drawLine(16,19,16,35);p.drawLine(9,9,23,16);}
        else {p.drawRoundedRect(QRectF(5,5,22,30),1,1);if(kind==9){p.drawLine(5,13,27,13);p.drawLine(11,2,11,8);p.drawLine(21,2,21,8);}for(int y:{18,24,30})p.drawLine(10,y,22,y);}
        return pm;
    }
    QWidget* tile(const QString& name){auto* w=new QWidget(this);w->setObjectName(name);w->setProperty("reportTile",true);w->setAttribute(Qt::WA_StyledBackground);return w;}
    int m_tab=-1;
    QHBoxLayout *m_row=nullptr,*m_metricRow=nullptr;
    QWidget *m_metrics=nullptr,*m_actions=nullptr,*m_searchTile=nullptr;
    QWidget* m_parts=nullptr;
    QLabel *m_partsTitles[4]{},*m_partsBodies[4]{},*m_partsIcons[4]{};
    QLabel* m_dateIcon=nullptr;QFrame* m_dateRule=nullptr;
    QWidget* m_cards[4]{};QLabel* m_titles[4]{};QLabel* m_values[4]{};QLabel* m_icons[4]{};
    QFrame* m_rules[4]{};
    QGridLayout* m_metricLayouts[4]{};
    QLabel* m_searchIcon=nullptr;QLabel* m_invoiceIcons[3]{};QLabel* m_actionIcons[3]{};QLabel* m_expenseIcon=nullptr;
    QFrame* m_searchRule=nullptr;
    QWidget* m_invoiceCards[3]{};
    QGridLayout* m_datesLayout=nullptr;
    QLabel *m_dateTitle=nullptr,*m_fromLabel=nullptr,*m_toLabel=nullptr;
    QPushButton* m_statusButtons[3]{};
    QLabel* m_clientReceived=nullptr;
};
