#pragma once
#include <QWidget>
class CashOperationsWidget;
class QTabWidget;
class QScrollArea;
class QComboBox;
class QGridLayout;
class QResizeEvent;
class QDateEdit;
class QLabel;
class QTableWidget;
class CashJournalPage : public QWidget {
public:
    explicit CashJournalPage(QWidget* parent);
    void refresh();
private:
    void resizeEvent(QResizeEvent* event) override;
    QGridLayout* body;
    QWidget* metricsPanel;
    QTabWidget* tabs;
    QGridLayout* metrics;
    QDateEdit* firstDay;
    QDateEdit* lastDay;
    QTableWidget* table;
    QTableWidget* outflowTable;
    QTableWidget* advanceTable;
    QLabel* advanceStatus;
    CashOperationsWidget* operations;
    QList<QLabel*> metricValues;
    QLabel* summary;
    QComboBox* selectedAccount;
    QScrollArea* balanceArea;
};
