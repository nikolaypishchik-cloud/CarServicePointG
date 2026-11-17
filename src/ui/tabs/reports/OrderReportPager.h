#pragma once

#include "ui/tabs/reports/ReportsTableSupport.h"
#include "core/util/DateFormats.h"

#include <QCollator>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableView>
#include <algorithm>
#include <QStyledItemDelegate>
#include <QPainter>
#include <QApplication>
#include <QFontInfo>

class OrderReportCellDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        QStyleOptionViewItem cell(option);
        initStyleOption(&cell,index);
        const QString text = index.data(Qt::DisplayRole).toString();
        cell.text.clear();
        const auto* widget = option.widget;
        (widget ? widget->style() : QApplication::style())->drawControl(QStyle::CE_ItemViewItem,&cell,painter,widget);
        painter->save();
        painter->setClipRect(option.rect);
        QRect bounds = option.rect.adjusted(10,6,-10,-6);
        QFont font = option.font;
        const auto lines = text.split('\n');
        if (index.column()==6) {
            const bool paid = text == tr("Сплачено");
            const bool unpaid = text == tr("Не сплачено повністю");
            const QColor accent(paid ? "#16952b" : unpaid ? "#ed8b00" : "#64717d");
            painter->setRenderHint(QPainter::Antialiasing);
            painter->setBrush(QColor(paid ? "#dff4d6" : unpaid ? "#fff0dc" : "#edf1f4"));
            painter->setPen(QPen(accent.lighter(155),1));
            bounds.setHeight(qMin(bounds.height(),QFontMetrics(font).height()+24));
            bounds.moveCenter(option.rect.center());
            painter->drawRoundedRect(bounds,6,6);
            painter->setPen(QPen(accent,2));
            const QPoint center(bounds.left()+18,bounds.center().y());
            if (paid) {
                painter->drawLine(center+QPoint(-6,0),center+QPoint(-1,5));
                painter->drawLine(center+QPoint(-1,5),center+QPoint(8,-6));
            } else {
                painter->setBrush(Qt::NoBrush);
                painter->drawEllipse(center,7,7);
                if (unpaid) painter->fillRect(QRect(bounds.left(),bounds.top()+3,4,bounds.height()-6),accent);
            }
            font.setWeight(QFont::DemiBold);
            font.setUnderline(true);
            painter->setFont(font);
            painter->setPen(QColor("#142019"));
            painter->drawText(bounds.adjusted(36,0,-8,0),Qt::AlignVCenter|Qt::AlignLeft,text);
        } else {
            int height=0;
            for (int line=0;line<lines.size();++line) height+=QFontMetrics(lineFont(font,index.column(),line)).height();
            int y=option.rect.center().y()-height/2;
            for (int line=0;line<lines.size();++line) {
                const QFont current=lineFont(font,index.column(),line);
                painter->setFont(current);
                painter->setPen(QColor("#14212c"));
                const int h=QFontMetrics(current).height();
                painter->drawText(QRect(bounds.left(),y,bounds.width(),h),
                    Qt::AlignVCenter|(index.column()==4?Qt::AlignRight:Qt::AlignLeft),lines[line]);
                y+=h;
            }
        }
        painter->restore();
    }
    QSize sizeHint(const QStyleOptionViewItem& option,const QModelIndex& index) const override {
        const auto lines=index.data().toString().split('\n');
        int width=0,height=0;
        for(int line=0;line<lines.size();++line) {
            const QFontMetrics metrics(lineFont(option.font,index.column(),line));
            width=qMax(width,metrics.horizontalAdvance(lines[line]));
            height+=metrics.height();
        }
        if(index.column()==6) {
            QFont font=option.font; font.setWeight(QFont::DemiBold);
            return {QFontMetrics(font).horizontalAdvance(index.data().toString())+64,QFontMetrics(font).height()+36};
        }
        return {width+20,height+16};
    }
private:
    static QFont lineFont(QFont font,int column,int line) {
        const bool larger=(column==1 && line==0) || (column==2 && line>0) || column==4;
        if(larger) font.setPointSizeF(QFontInfo(font).pointSizeF()+2);
        if(column==1 && line==0) font.setBold(true);
        if((column==2 && line>0) || column==4) font.setWeight(QFont::DemiBold);
        return font;
    }
};

class OrderReportPager final : public QWidget {
public:
    OrderReportPager(QTableView* table, ReportsTableSupport::ReportsSnapshotTableModel* model, QWidget* parent)
        : QWidget(parent), m_table(table), m_model(model) {
        for (int column : {1,2,4,6})
            table->setItemDelegateForColumn(column,new OrderReportCellDelegate(table));
        table->horizontalHeader()->setStyleSheet(
            "QHeaderView::section {color:#fff;font-weight:bold;padding:9px 10px;border:1px solid #bccbd6;"
            "background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #708797,stop:1 #4d606e);}");
        auto* bar = new QHBoxLayout(this);
        bar->setContentsMargins(8, 2, 8, 2);
        m_status = new QLabel(this);
        m_status->setStyleSheet("color:#172a3a;");
        bar->addWidget(m_status);
        bar->addStretch();
        m_size = new QComboBox(this);
        m_size->setObjectName("reportOrdersPageSize");
        m_size->setAccessibleName(tr("Нарядів на сторінці"));
        for (int size : {10, 25, 50, 100}) m_size->addItem(QString::number(size), size);
        m_size->setCurrentIndex(2);
        m_previous = new QPushButton(tr("Назад"), this);
        m_next = new QPushButton(tr("Далі"), this);
        m_next->setObjectName("reportOrdersNextPage");
        bar->addWidget(m_size);
        bar->addWidget(m_previous);
        bar->addWidget(m_next);
        setObjectName("orderReportPager");
        setAttribute(Qt::WA_StyledBackground);
        setStyleSheet("#orderReportPager { background:#eef3f7; }");
        m_status->setStyleSheet("background:#eef3f7;color:#172a3a;padding:3px;");
        connect(m_size, &QComboBox::currentIndexChanged, this, [this] { m_page = 0; display(); });
        connect(m_previous, &QPushButton::clicked, this, [this] { --m_page; display(); });
        connect(m_next, &QPushButton::clicked, this, [this] { ++m_page; display(); });
        auto* header = m_table->horizontalHeader();
        header->setSectionsClickable(true);
        connect(header, &QHeaderView::sectionClicked, this, [this, header](int column) {
            m_descending = m_sortColumn == column && !m_descending;
            m_sortColumn = column;
            header->setSortIndicator(column, m_descending ? Qt::DescendingOrder : Qt::AscendingOrder);
            header->setSortIndicatorShown(true);
            sort();
            m_page = 0;
            display();
        });
        m_collator.setNumericMode(true);
        connect(model, &QAbstractItemModel::modelReset, this, [this] {
            if (m_rendering) return;
            m_original = {};
            m_snapshot = {};
            m_page = 0;
            setEnabled(false);
            m_status->clear();
        });
    }

    void setPaymentGrouping(int index) {
        m_paymentGrouping = index != 0;
        if (isEnabled()) setSnapshot(m_original);
    }

    void setSnapshot(const OrdersSnapshot& filtered) {
        setEnabled(true);
        m_original = filtered;
        m_snapshot = filtered;
        m_page = 0;
        sort();
        display();
    }

private:
    static QString sortText(const OrdersSnapshotRow& row, int column) {
        switch (column) {
        case 0: return DateFormats::parseDate(row.date).toString(Qt::ISODate);
        case 1: return row.orderNumber;
        case 2: return row.car;
        case 3: return row.invoiceNumbers.join(' ');
        case 5: return row.paymentDue;
        default: return row.paymentStatus;
        }
    }

    void sort() {

        const auto money = [](QString value) {
            return value.simplified().remove(' ').replace(',', '.').toDouble();
        };
        std::stable_sort(m_snapshot.rows.begin(), m_snapshot.rows.end(), [&](const auto& a, const auto& b) {
            if (m_paymentGrouping && a.paymentStatus != b.paymentStatus)
                return m_collator.compare(a.paymentStatus, b.paymentStatus) < 0;
            if (m_sortColumn < 0) return false;
            if (m_sortColumn == 4) return m_descending ? money(b.invoiceTotal) < money(a.invoiceTotal)
                                                       : money(a.invoiceTotal) < money(b.invoiceTotal);
            const int cmp = m_collator.compare(sortText(a, m_sortColumn), sortText(b, m_sortColumn));
            return m_descending ? cmp > 0 : cmp < 0;
        });
    }

    void display() {
        const int size = m_size->currentData().toInt();
        const int count = m_snapshot.rows.size();
        const int pages = qMax(1, (count + size - 1) / size);
        m_page = qBound(0, m_page, pages - 1);
        OrdersSnapshot slice;
        slice.rows = m_snapshot.rows.mid(m_page * size, size);
        m_rendering = true;
        ReportsTableSupport::applyOrdersSnapshot(m_table, m_model, slice, m_paymentGrouping);
        m_rendering = false;
        m_previous->setEnabled(m_page > 0);
        m_next->setEnabled(m_page + 1 < pages);
        m_status->setText(tr("Показано %1–%2 із %3 · Сторінка %4 із %5")
            .arg(count ? m_page * size + 1 : 0).arg(qMin(count, (m_page + 1) * size)).arg(count).arg(m_page + 1).arg(pages));
    }

    QTableView* m_table;
    ReportsTableSupport::ReportsSnapshotTableModel* m_model;
    QLabel* m_status;
    QComboBox* m_size;
    QPushButton* m_previous;
    QPushButton* m_next;
    OrdersSnapshot m_original;
    OrdersSnapshot m_snapshot;
    bool m_paymentGrouping = false;
    QCollator m_collator;
    int m_page = 0;
    int m_sortColumn = -1;
    bool m_descending = false;
    bool m_rendering = false;
};
