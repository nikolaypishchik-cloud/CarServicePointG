#pragma once

#include <QDateEdit>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QPainter>
#include <QResizeEvent>
#include <QVBoxLayout>
#include "core/util/FormatUtils.h"
#include "ui/theme/ThemeManager.h"

// Presentation only: supplier balances remain owned by the existing ledger.
class PartsSupplierDashboard final : public QWidget {
public:
    explicit PartsSupplierDashboard(QDateEdit* from, QDateEdit* to, QLineEdit* search, QWidget* parent=nullptr)
        : QWidget(parent) {
        setObjectName("partsToolbarHost");
        setStyleSheet(QString("#partsToolbarHost {background:transparent;border:none;} QWidget[partsSupplierTile=\"true\"] {background:transparent;border:1px solid black;border-radius:4px;} #partsToolbarHost QLabel {background:transparent;border:none;color:%1;}").arg(ThemeManager::currentTheme()=="light"?"#111111":"#ffffff"));
        setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);
        setFixedHeight(qRound(15.0*logicalDpiY()/25.4));
        auto* outer=new QVBoxLayout(this);outer->setContentsMargins(0,0,0,0);outer->setSpacing(0);
        m_grid=new QGridLayout; m_grid->setContentsMargins(0,0,0,0);m_grid->setSpacing(qRound(logicalDpiX()/25.4));outer->addLayout(m_grid);
        auto* period=card("partsFilterFrame",QString::fromUtf8("Період"),0);
        auto* dates=new QHBoxLayout;dates->setSpacing(4);
        for(auto* edit:{from,to}) {
            edit->ensurePolished();
            edit->setMinimumWidth(edit->fontMetrics().horizontalAdvance("00.00.00")+52);
            edit->setFixedHeight(qRound(7.0*logicalDpiY()/25.4));
            dates->addWidget(edit,1);
        }
        static_cast<QVBoxLayout*>(period->layout())->addLayout(dates);
        auto* find=card("partsSearchTile",QString::fromUtf8("Пошук"),1);
        search->setMinimumWidth(0);search->setFixedHeight(qRound(7.0*logicalDpiY()/25.4));
        static_cast<QVBoxLayout*>(find->layout())->addWidget(search);
        m_fixed={period,find};
        error=new QLabel(this);error->setWordWrap(true);error->hide();
        setBalances({});
    }
    void setBalances(const QMap<QString,qint64>& balances) {
        if(m_initialized && balances==m_balances) {error->clear();error->hide();return;}
        m_initialized=true;m_balances=balances;
        for(auto* item:m_dynamic){m_grid->removeWidget(item);delete item;}m_dynamic.clear();
        qint64 total=0;
        for(auto it=balances.cbegin();it!=balances.cend();++it) {
            total+=it.value();
            m_dynamic.append(balanceCard("partsSupplierTile",it.key(),it.value(),false));
        }
        if(balances.size()<=3)m_dynamic.append(balanceCard("partsTotalTile",QString::fromUtf8("Загальний борг"),total,true));
        error->clear();error->hide();arrange();
    }
    void showError(const QString& message) {error->setText(message);error->setGeometry(rect());error->setVisible(!message.isEmpty());error->raise();}
    QLabel* error=nullptr;
protected:
    void resizeEvent(QResizeEvent* event) override {QWidget::resizeEvent(event);arrange();}
private:
    QPixmap symbol(int kind,const QColor& color) const {
        QPixmap pm(QSize(32,36)*devicePixelRatioF());pm.setDevicePixelRatio(devicePixelRatioF());pm.fill(Qt::transparent);
        QPainter p(&pm);p.setRenderHint(QPainter::Antialiasing);p.setPen(QPen(color,1.7));
        if(kind==0){p.drawRoundedRect(3,6,26,27,2,2);p.drawLine(3,14,29,14);p.drawLine(10,2,10,10);p.drawLine(22,2,22,10);for(int y:{19,25})for(int x:{9,16,23})p.drawPoint(x,y);}
        else if(kind==1){p.drawEllipse(QRectF(3,3,21,21));p.drawLine(21,22,30,32);}
        else if(kind==2){p.drawRect(5,14,22,18);p.drawRect(17,22,6,10);p.drawRect(8,20,5,5);p.drawPolygon(QPolygonF{{3,14},{6,5},{26,5},{29,14}});for(int x:{3,10,17,24})p.drawArc(QRectF(x,10,7,8),180*16,180*16);}
        else {for(int y:{7,14,21}){p.drawEllipse(QRectF(5,y,22,8));p.drawLine(5,y+4,5,y+10);p.drawLine(27,y+4,27,y+10);}p.drawArc(QRectF(5,27,22,8),180*16,180*16);}
        return pm;
    }
    QWidget* card(const QString& name,const QString& title,int kind) {
        auto* w=new QWidget(this);w->setObjectName(name);w->setProperty("partsSupplierTile",true);w->setAttribute(Qt::WA_StyledBackground);
        w->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Fixed);
        auto* layout=new QVBoxLayout(w);layout->setContentsMargins(4,1,4,1);layout->setSpacing(qRound(logicalDpiY()/25.4));
        auto* heading=new QHBoxLayout;auto* icon=new QLabel(w);icon->setPixmap(symbol(kind,QColor("#8bdfee")));icon->setScaledContents(true);icon->setFixedSize(18,20);heading->addWidget(icon);
        auto* label=new QLabel(title,w);label->setToolTip(title);label->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);auto f=label->font();f.setBold(true);label->setFont(f);heading->addWidget(label,1);layout->addLayout(heading);
        return w;
    }
    QWidget* balanceCard(const QString& name,const QString& title,qint64 cents,bool total) {
        auto* w=card(name,title,total?3:2);auto* line=new QHBoxLayout;auto* icon=new QLabel(w);icon->setPixmap(symbol(3,QColor("#e7a52d")));icon->setScaledContents(true);icon->setFixedSize(18,20);line->addWidget(icon);
        auto* amount=new QLabel(FormatUtils::money(cents/100.0,2)+QString::fromUtf8(" грн"),w);amount->setObjectName("partsSupplierAmount");amount->setWordWrap(true);amount->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);auto f=amount->font();f.setPointSize(12);f.setBold(true);amount->setFont(f);line->addWidget(amount,1);
        amount->setAlignment(Qt::AlignCenter);
        // Balance the decorative icon so the amount is centered in the whole tile.
        line->addSpacing(icon->width());
        static_cast<QVBoxLayout*>(w->layout())->addLayout(line);return w;
    }
    void arrange() {
        const auto cards=m_fixed+m_dynamic;
        // Reserve readable dates; share all remaining width without adding rows.
        const int periodWidth=m_fixed.first()->minimumSizeHint().width();
        while(auto* item=m_grid->takeAt(0))delete item;
        for(int col=0;col<m_grid->columnCount();++col){m_grid->setColumnMinimumWidth(col,0);m_grid->setColumnStretch(col,0);}
        m_grid->setColumnMinimumWidth(0,periodWidth);
        for(int i=0;i<cards.size();++i){cards[i]->setFixedHeight(height());m_grid->addWidget(cards[i],0,i);m_grid->setColumnStretch(i,i==0?0:1);}
        updateGeometry();
    }
    QGridLayout* m_grid=nullptr;
    QList<QWidget*> m_fixed,m_dynamic;
    QMap<QString,qint64> m_balances;
    bool m_initialized=false;
};
