#pragma once
#include <QWidget>
#include <QDate>
#include <QList>
#include "core/services/orders/CashOperations.h"
class QLineEdit;class QComboBox;class QTableWidget;class QPushButton;class QLabel;
QString writeCashJournalPdf(const QString& path,QDate first,QDate last,const QString& search,const QString& kind,const QString& method);
class CashOperationsWidget : public QWidget {
public:
    explicit CashOperationsWidget(QWidget* parent);
    void refresh(QDate first,QDate last);
private:
    void loadPage();
    QDate start,end;
    int offset=0;
    QLineEdit* search;
    QComboBox *kind,*method;
    QTableWidget* table;
    QPushButton *previous,*next;
    QLabel* status;
    QList<CashOperations::Row> rows;
};
