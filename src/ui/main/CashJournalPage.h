#pragma once
#include <QWidget>
class QDateEdit;
class QLabel;
class QTableWidget;
class CashJournalPage : public QWidget {
public:
    explicit CashJournalPage(QWidget* parent);
    void refresh();
private:
    QDateEdit* firstDay;
    QDateEdit* lastDay;
    QTableWidget* table;
    QTableWidget* outflowTable;
    QTableWidget* advanceTable;
    QLabel* advanceStatus;
    QList<QLabel*> metricValues;
    QLabel* summary;
};
