#pragma once
#include "ui/tabs/reports/ReportsTableSupport.h"
#include <QHeaderView>
#include <QStyledItemDelegate>
#include <QPainter>
#include <QFontInfo>
#include <QTableView>

class PartsWrappedHeader final : public QHeaderView {
public:
    explicit PartsWrappedHeader(QWidget* parent):QHeaderView(Qt::Horizontal,parent) {}
    QSize sizeHint() const override {
        auto size=QHeaderView::sizeHint();
        size.setHeight(qMax(size.height(),fontMetrics().height()*3+12));
        return size;
    }
protected:
    void paintSection(QPainter* painter,const QRect& rect,int logical) const override {
        if(!rect.isValid())return;
        painter->save();painter->setClipRect(rect);
        painter->fillRect(rect,QColor("#d7e7ee"));
        painter->setPen(QColor("#c2d3df"));painter->drawRect(rect.adjusted(0,0,-1,-1));
        QFont font=this->font();font.setPointSizeF(qMax(8.0,QFontInfo(font).pointSizeF()-2));font.setBold(true);
        painter->setFont(font);painter->setPen(QColor("#19313e"));
        QString title=model()->headerData(logical,Qt::Horizontal).toString();
        if(model()->columnCount()==12) {
            if(logical==0)title=tr("Дата / Встановлено");
            if(logical==2)title=tr("Наряд / Авто");
        }
        painter->drawText(rect.adjusted(5,4,-5,-4),Qt::AlignCenter|Qt::TextWordWrap,title);
        painter->restore();
    }
};

class PartsRegisterDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* p,const QStyleOptionViewItem& option,const QModelIndex& index) const override {
        QStyleOptionViewItem opt(option);initStyleOption(&opt,index);
        auto value=[&](int c){return index.siblingAtColumn(c).data().toString();};
        p->save();p->setClipRect(opt.rect);
        p->fillRect(opt.rect,opt.state&QStyle::State_Selected?QColor("#e0f4f5"):QColor("#ffffff"));
        p->setPen(QColor("#d7e0e5"));p->drawLine(opt.rect.bottomLeft(),opt.rect.bottomRight());
        QRect bounds=opt.rect.adjusted(10,6,-10,-6);
        const QString status=value(11);
        if(index.column()==0 || index.column()==5) {
            const auto* view=qobject_cast<const QTableView*>(option.widget);
            if(status==tr("Не сплачено") && (index.column()==0 || (view && view->isColumnHidden(0)))) {
                p->fillRect(QRect(opt.rect.left()+3,opt.rect.top()+6,4,opt.rect.height()-12),QColor("#ee7777"));
            }
        }
        QFont font=option.font;p->setFont(font);p->setPen(QColor("#203442"));
        if(index.column()==11) {
            const bool paid=status==tr("Сплачено"),unpaid=status==tr("Не сплачено");
            font.setBold(true);p->setFont(font);
            p->setRenderHint(QPainter::Antialiasing);
            p->setBrush(QColor(paid?"#e7f5df":unpaid?"#ffe9e7":"#edf1f5"));
            p->setPen(QColor(paid?"#9cc48d":unpaid?"#e9a29d":"#a6b5bf"));
            QRect badge=bounds;badge.setHeight(qMin(badge.height(),QFontMetrics(font).height()+12));badge.moveCenter(bounds.center());
            p->drawRoundedRect(badge,12,12);
            p->setPen(QColor(paid?"#276632":unpaid?"#a62d2d":"#334955"));
            p->drawText(badge.adjusted(5,0,-5,0),Qt::AlignCenter,QFontMetrics(font).elidedText(status,Qt::ElideRight,badge.width()-10));
        } else if(index.column()==0 || index.column()==2 || index.column()==5) {
            QString first=value(index.column()),second;
            if(index.column()==0){first=tr("Дата: %1").arg(first);second=tr("Вст.: %1").arg(value(1));}
            if(index.column()==2){first=tr("№ %1").arg(first);second=value(3);}
            if(index.column()==5)second=tr("OEM %1 · %2").arg(value(6),value(7));
            font.setBold(index.column()!=0);font.setUnderline(index.column()==2);
            p->setFont(font);p->setPen(index.column()==2?QColor("#1764a4"):QColor("#203442"));
            const int h=QFontMetrics(font).height();
            p->drawText(bounds.left(),bounds.top()+h,QFontMetrics(font).elidedText(first,Qt::ElideRight,bounds.width()));
            font.setBold(false);font.setUnderline(false);font.setPointSizeF(qMax(8.0,QFontInfo(font).pointSizeF()-1));
            p->setFont(font);p->setPen(QColor("#4d6370"));
            p->drawText(bounds.adjusted(0,h+2,0,0),Qt::AlignLeft|Qt::AlignTop,QFontMetrics(font).elidedText(second,Qt::ElideRight,bounds.width()));
        } else {
            font.setBold(index.column()==10);p->setFont(font);
            p->drawText(bounds,Qt::AlignVCenter|(index.column()>=8?Qt::AlignRight:Qt::AlignLeft),
                QFontMetrics(font).elidedText(value(index.column()),Qt::ElideRight,bounds.width()));
        }
        p->restore();
    }
    QSize sizeHint(const QStyleOptionViewItem& option,const QModelIndex&) const override {
        return QSize(120,QFontMetrics(option.font).height()*2+22);
    }
};


class PartsStoreGroupDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* p,const QStyleOptionViewItem& option,const QModelIndex& index) const override {
        const auto info=index.data(Qt::UserRole+100).toStringList();
        if(info.size()!=3){QStyledItemDelegate::paint(p,option,index);return;}
        p->save();p->setClipRect(option.rect);
        QLinearGradient gradient(option.rect.topLeft(),option.rect.topRight());
        gradient.setColorAt(0,QColor("#d6e9ef"));gradient.setColorAt(1,QColor("#9fbfd0"));
        p->fillRect(option.rect,gradient);
        p->fillRect(QRect(option.rect.left()+2,option.rect.top()+3,4,option.rect.height()-6),QColor("#22c6ca"));
        QFont font=option.font;font.setBold(true);p->setFont(font);p->setPen(QColor("#19333f"));
        const int width=option.rect.width();
        const int badgeX=option.rect.left()+width/3;
        const int h=QFontMetrics(font).height();
        const int y=option.rect.center().y();
        p->drawText(QRect(option.rect.left()+15,option.rect.top(),width/3-25,option.rect.height()),Qt::AlignVCenter,info[0]);
        QRect badge(badgeX,y-h/2-3,qMax(100,width/10),h+6);
        p->setRenderHint(QPainter::Antialiasing);p->setBrush(QColor("#6f98ac"));p->setPen(Qt::NoPen);p->drawRoundedRect(badge,10,10);
        p->setPen(Qt::white);p->drawText(badge,Qt::AlignCenter,tr("Позицій: %1").arg(info[1]));
        p->setPen(QColor("#19333f"));
        p->drawText(option.rect.adjusted(width/2,0,-12,0),Qt::AlignVCenter|Qt::AlignRight,tr("Закупівля: %1 грн").arg(info[2]));
        p->restore();
    }
    QSize sizeHint(const QStyleOptionViewItem& option,const QModelIndex& index) const override {
        auto size=QStyledItemDelegate::sizeHint(option,index);
        if(index.data(Qt::UserRole+100).isValid())size.setHeight(qMax(size.height(),QFontMetrics(option.font).height()+22));
        return size;
    }
};
