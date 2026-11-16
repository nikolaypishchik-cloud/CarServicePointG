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
    QLabel* summary;
};
