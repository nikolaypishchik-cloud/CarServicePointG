#pragma once

#include "core/services/ReportsDataLoader.h"
#include <QHeaderView>
#include <QTreeWidget>

class PeriodGroupsView final : public QTreeWidget {
public:
    explicit PeriodGroupsView(QWidget* parent) : QTreeWidget(parent) {
        setObjectName("reportPeriodGroups");
        setHeaderLabels({tr("Період / наряд"), tr("Нарядів"), tr("Автомобіль"), tr("Майстер"),
                         tr("Роботи, грн"), tr("Запчастини, грн"), tr("Разом, грн")});
        setStyleSheet(QStringLiteral("QTreeWidget { background:#fafbfd; alternate-background-color:#edf2f6;"
                                     "color:#182632; selection-background-color:#226678; selection-color:white; }"));
        setUniformRowHeights(true);
        setAlternatingRowColors(true);
        header()->setStretchLastSection(false);
        header()->setSectionResizeMode(QHeaderView::ResizeToContents);
        header()->setSectionResizeMode(2,QHeaderView::Stretch);
        hide();
    }

    void showSnapshot(const PeriodSnapshot& snapshot) {
        setUpdatesEnabled(false);
        clear();
        QDate weekStart, day;
        QTreeWidgetItem* weekItem = nullptr;
        QTreeWidgetItem* dayItem = nullptr;
        Totals weekTotals, dayTotals;
        QMap<QPair<QString,QString>,Totals> byMaster;
        for (int i=0; i<snapshot.rows.size(); ++i) {
            const auto& row = snapshot.rows[i];
            const auto monday = row.day.addDays(1-row.day.dayOfWeek());
            if (!weekItem || monday!=weekStart) {
                showTotals(weekItem,weekTotals);
                weekStart=monday;
                weekTotals={};
                weekItem=new QTreeWidgetItem(this,{monday.toString("dd.MM.yyyy") + " – " + monday.addDays(6).toString("dd.MM.yyyy")});
                weekItem->setExpanded(true);
            }
            if (!dayItem || row.day!=day) {
                showTotals(dayItem,dayTotals);
                day=row.day;
                dayTotals={};
                dayItem=new QTreeWidgetItem(weekItem,{day.toString("dd.MM.yyyy")});
                dayItem->setExpanded(true);
            }
            auto* order=new QTreeWidgetItem(dayItem,{tr("Наряд №%1").arg(row.orderNumber),{},row.car,row.master,
                money(row.worksCents),money(row.partsCents),money(row.worksCents+row.partsCents)});
            order->setData(0,Qt::UserRole,i);
            byMaster[{row.master,row.masterId}].add(row);
            weekTotals.add(row);
            dayTotals.add(row);
        }
        showTotals(dayItem,dayTotals);
        showTotals(weekItem,weekTotals);
        if (!byMaster.isEmpty()) {
            auto* summary=new QTreeWidgetItem(this,{tr("Підсумки за майстрами")});
            for (auto it=byMaster.cbegin();it!=byMaster.cend();++it) {
                const auto& key=it.key();
                QString name=key.first.isEmpty()?tr("Майстра не вказано"):key.first;
                if (!key.second.isEmpty()) name+=tr(" (№%1)").arg(key.second);
                else if (!key.first.isEmpty()) name+=tr(" (за іменем)");
                showTotals(new QTreeWidgetItem(summary,{name}),it.value());
            }
            summary->setExpanded(true);
        }
        setUpdatesEnabled(true);
        setVisible(!snapshot.rows.isEmpty());
    }

private:
    struct Totals {
        int count=0;
        qint64 works=0, parts=0;
        void add(const PeriodSnapshotRow& row) {
            ++count;
            works+=row.worksCents;
            parts+=row.partsCents;
        }
    };
    QString money(qint64 cents) const { return locale().toString(cents / 100.0,'f',2); }
    void showTotals(QTreeWidgetItem* item,const Totals& totals) {
        if (!item) return;
        item->setText(1,QString::number(totals.count));
        item->setText(4,money(totals.works));
        item->setText(5,money(totals.parts));
        item->setText(6,money(totals.works+totals.parts));
    }
};
