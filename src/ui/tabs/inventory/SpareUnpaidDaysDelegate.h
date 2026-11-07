#pragma once
#include <QStyledItemDelegate>
#include <QPainter>

class SpareUnpaidDaysDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* painter,const QStyleOptionViewItem& option,const QModelIndex& index) const override {
        bool valid=false;
        const int days=index.siblingAtColumn(9).data().toString().toInt(&valid);
        if(!valid || days<3) {QStyledItemDelegate::paint(painter,option,index);return;}
        QStyleOptionViewItem opt(option);initStyleOption(&opt,index);
        painter->save();
        painter->fillRect(opt.rect,days>=5?QColor(255,160,160):QColor(255,235,120));
        painter->setFont(opt.font);painter->setPen(Qt::black);
        const auto rect=opt.rect.adjusted(6,0,-6,0);
        painter->drawText(rect,Qt::AlignLeft|Qt::AlignVCenter,opt.fontMetrics.elidedText(opt.text,Qt::ElideRight,rect.width()));
        if(opt.state & QStyle::State_Selected) {
            painter->setPen(QColor("#52708f"));
            painter->drawLine(opt.rect.topLeft(),opt.rect.topRight());
            painter->drawLine(opt.rect.bottomLeft(),opt.rect.bottomRight());
        }
        painter->restore();
    }
};
