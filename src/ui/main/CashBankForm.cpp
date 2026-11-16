#include "CashBankForm.h"
#include "core/services/orders/CashBankService.h"
#include "core/services/orders/ClientReceiptReader.h"
#include "core/services/AccessControlService.h"
#include "core/finance/ExactMoney.h"
#include <QComboBox>
#include <QDateEdit>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QHeaderView>
#include <QUuid>

QWidget* cashBankForm(QWidget* parent,bool configure)
{
    using namespace CashBankService;
    auto* page=new QWidget(parent);
    auto* layout=new QFormLayout(page);
    auto* account=new QComboBox(page);
    auto* action=new QComboBox(page);
    if(configure){
        action->addItem(QObject::tr("Додати банківський рахунок"),"bank");
        action->addItem(QObject::tr("Додати сейф"),"safe");
        action->addItem(QObject::tr("Додати канал еквайрингу"),"channel");
        action->addItem(QObject::tr("Перейменувати"),"rename");
        action->addItem(QObject::tr("Блокувати нові операції"),"block");
        action->addItem(QObject::tr("Активувати"),"activate");
        action->addItem(QObject::tr("Архівувати"),"archive");
    }else{
        layout->addRow(new QLabel(QObject::tr("Ручна звірка за банківською випискою. Фіскальна інтеграція відсутня."),page));
        action->addItem(QObject::tr("Записати зарахування банку"),"credit");
        action->addItem(QObject::tr("Звірити з картковою оплатою"),"match");
    }
    layout->addRow(QObject::tr("Дія"),action);
    layout->addRow(QObject::tr("Рахунок"),account);
    auto* channel=new QComboBox(page);
    layout->addRow(QObject::tr("Канал еквайрингу"),channel);
    auto* terms=new QLineEdit(page);terms->setMaxLength(300);
    layout->addRow(QObject::tr("Умови комісії за договором"),terms);
    auto selectChannels=[=]{
        QString error;
        const auto configuredChannels=channels(error);
        channel->setToolTip(error);
        channel->clear();channel->addItem(QObject::tr("Канал не вказано"),QString());
        for(const auto& item:configuredChannels)if(item.account==account->currentData().toString()){
            channel->addItem(item.name,item.uid);
            channel->setItemData(channel->count()-1,item.terms,Qt::ToolTipRole);
        }
    };
    auto* reference=new QLineEdit(page);
    layout->addRow(configure?QObject::tr("Назва"):QObject::tr("Ідентифікатор рядка виписки"),reference);
    auto* credit=new QComboBox(page);layout->addRow(QObject::tr("Банківський рядок"),credit);
    auto* source=new QComboBox(page);layout->addRow(QObject::tr("Оплата до зарахування"),source);
    auto* date=new QDateEdit(QDate::currentDate(),page);
    date->setCalendarPopup(true);date->setMaximumDate(QDate::currentDate());
    layout->addRow(QObject::tr("Дата виписки"),date);
    auto* net=new QLineEdit(page);layout->addRow(QObject::tr("Зараховано банком, грн"),net);
    auto* fee=new QLineEdit("0",page);layout->addRow(QObject::tr("Комісія, грн"),fee);
    auto* matched=new QLineEdit(page);layout->addRow(QObject::tr("Сума звірки до комісії, грн"),matched);
    auto* reason=new QLineEdit(page);reason->setMaxLength(300);layout->addRow(QObject::tr("Підстава"),reason);
    auto* post=new QPushButton(QObject::tr("Підтвердити"),page);layout->addRow(post);
    auto* status=new QLabel(page);status->setTextFormat(Qt::PlainText);status->setWordWrap(true);layout->addRow(status);
    auto* table=new QTableWidget(0,7,page);
    table->setHorizontalHeaderLabels({QObject::tr("Дата"),QObject::tr("Рахунок"),QObject::tr("Рядок виписки"),QObject::tr("Зараховано"),QObject::tr("Комісія"),QObject::tr("До звірки"),QObject::tr("Канал еквайрингу")});
    table->horizontalHeader()->setStretchLastSection(true);table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    if(configure)table->hide();else layout->addRow(table);
    auto reload=[=]{
        QString error;
        account->clear();credit->clear();source->clear();
        for(const auto& item:accounts(error,configure))if(configure || item.type=="bank")account->addItem(item.name+(item.state=="block"?QObject::tr(" · заблоковано"):item.state=="archive"?QObject::tr(" · архів"):QString()),item.uid);
        if(!configure && error.isEmpty()){
            const auto rows=credits(error);
            table->setRowCount(rows.size());
            for(int i=0;i<rows.size();++i){
                const auto& item=rows[i];
                const QStringList cols{item.day,item.account,item.reference,ClientReceiptReader::money(item.net),ClientReceiptReader::money(item.fee),ClientReceiptReader::money(item.unmatched),item.channelName.isEmpty()?QObject::tr("Не вказано"):item.channelName};
                for(int j=0;j<cols.size();++j)table->setItem(i,j,new QTableWidgetItem(cols[j]));
                if(item.unmatched>0){credit->addItem(item.account+" · "+item.reference+" · "+ClientReceiptReader::money(item.unmatched),item.reference);
                    credit->setItemData(credit->count()-1,item.accountUid,Qt::UserRole+1);}
            }
            if(error.isEmpty())for(const auto& item:pending(error)){source->addItem(item.day+" · "+ClientReceiptReader::money(item.cents)+" · "+item.uid,item.uid);
                source->setItemData(source->count()-1,item.type,Qt::UserRole+1);}
        }
        selectChannels();
        return error;
    };
    auto update=[=]{
        const auto kind=action->currentData().toString();
        const bool lifecycle=QStringList{"block","activate","archive"}.contains(kind);
        layout->setRowVisible(account,kind=="rename" || kind=="credit" || kind=="channel" || lifecycle);
        layout->setRowVisible(reference,kind!="match" && !lifecycle);
        layout->setRowVisible(channel,kind=="credit");layout->setRowVisible(terms,kind=="channel");
        layout->setRowVisible(credit,kind=="match");layout->setRowVisible(source,kind=="match");
        layout->setRowVisible(net,kind=="credit");layout->setRowVisible(fee,kind=="credit");
        layout->setRowVisible(date,kind=="credit");layout->setRowVisible(matched,kind=="match");
        post->setEnabled(AccessControlService::currentUserHasPermission(configure?"cash.configure":"cash.bank"));
    };
    QObject::connect(account,&QComboBox::currentIndexChanged,page,selectChannels);
    status->setText(reload());update();
    QObject::connect(action,&QComboBox::currentIndexChanged,page,[=]{update();});
    auto* refresh=new QPushButton(QObject::tr("Оновити дані"),page);layout->addRow(refresh);
    QObject::connect(refresh,&QPushButton::clicked,page,[=]{status->setText(reload());update();});
    auto newUid=[] {return QUuid::createUuid().toString(QUuid::WithoutBraces);};
    QObject::connect(post,&QPushButton::clicked,page,[=,request=newUid(),accountUid=newUid()]()mutable{
        const auto kind=action->currentData().toString();
        Command c{request,kind,account->currentData().toString(),reference->text(),{},{},reason->text(),QDate::currentDate()};
        auto amount=[](QLineEdit* field){const auto parsed=ExactMoney::payment(field->text().trimmed().replace(',', '.').toStdString());
            const auto* cents=std::get_if<ExactMoney::Cents>(&parsed);return cents?*cents:qint64(-1);};
        if(kind=="bank" || kind=="safe"){c.action="account";c.sourceType=kind;c.account=accountUid;}
        if(QStringList{"block","activate","archive"}.contains(kind))c.reference=c.account;
        if(kind=="channel")c.source=terms->text().trimmed();
        if(kind=="credit" && !channel->currentData().toString().isEmpty()){c.sourceType="channel";c.source=channel->currentData().toString();}
        if(kind=="credit"){c.net=amount(net);c.fee=amount(fee);c.date=date->date();}
        if(kind=="match"){c.account=credit->currentData(Qt::UserRole+1).toString();c.reference=credit->currentData().toString();
            c.sourceType=source->currentData(Qt::UserRole+1).toString();c.source=source->currentData().toString();c.matched=amount(matched);}
        const auto result=CashBankService::post(c);
        if(!result.error.isEmpty()){status->setText(result.error);return;}
        reference->clear();terms->clear();reason->clear();net->clear();fee->setText("0");matched->clear();
        request=newUid();accountUid=newUid();
        const auto error=reload();status->setText(error.isEmpty()?QObject::tr("Запис підтверджено."):error);
    });
    return page;
}
