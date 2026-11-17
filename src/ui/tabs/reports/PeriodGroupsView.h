#pragma once

#include "core/services/ReportsDataLoader.h"
#include <QHeaderView>
#include "core/services/settings/UiSettingsService.h"
#include <QSettings>
#include <QTreeWidget>

namespace PeriodReportPresentation {
inline void configure(QAbstractItemView* view, QHeaderView* header, const QString& key)
{
    const auto fonts = UiSettingsService::loadFontSettings();
    view->setStyleSheet(view->styleSheet() + QStringLiteral(
        "QTreeWidget,QTableView {font-size:%1pt;}"
        "QHeaderView::section {font-size:%2pt;}"
        "QAbstractItemView::item:selected {background:#c9dfe6;color:#142631;}")
        .arg(fonts.tableFontSize + 3).arg(fonts.headingFontSize + 3));
    header->setSectionsMovable(true);
    header->setFirstSectionMovable(true);
    header->restoreState(QSettings().value(key).toByteArray());
    QObject::connect(header, &QHeaderView::sectionMoved, header, [header,key] {
        QSettings().setValue(key, header->saveState());
    });
}
}

class PeriodGroupsView final : public QTreeWidget {
public:
    explicit PeriodGroupsView(QWidget* parent) : QTreeWidget(parent) {
        setObjectName("reportPeriodGroups");
        setHeaderLabels({tr("Період / наряд"), tr("Нарядів"), tr("Автомобіль"), tr("Майстер"),
                         tr("Роботи, грн"), tr("Запчастини, грн"), tr("Разом, грн")});
        setStyleSheet(QStringLiteral("QTreeWidget { background:#fafbfd; alternate-background-color:#edf2f6;"
                                     "color:#182632; selection-background-color:#c9dfe6; selection-color:#142631; }"));
        setUniformRowHeights(true);
        setAlternatingRowColors(true);
        header()->setStretchLastSection(false);
        header()->setSectionResizeMode(QHeaderView::ResizeToContents);
        header()->setSectionResizeMode(2,QHeaderView::Stretch);
        PeriodReportPresentation::configure(this, header(), QStringLiteral("reports/period/groupedColumns"));
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
                auto* master = new QTreeWidgetItem(summary,{name});
                master->setData(0, Qt::UserRole + 1, key.second);
                showTotals(master,it.value());
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
