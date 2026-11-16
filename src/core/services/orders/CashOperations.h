#pragma once
#include <QDate>
#include <QList>
#include <QString>
namespace CashOperations {
struct Row {QString day,uid,kind,name,vehicle,order,number,method,reason,source,actor; qint64 amount,net;};
struct Page {QList<Row> rows;bool more=false;QString error;};
Page read(QDate first,QDate last,const QString& search,const QString& operation,const QString& method,int offset);
Page exportSelection(QDate first,QDate last,const QString& search,const QString& operation,const QString& method);
QString title(const QString& kind);
QString methodTitle(const QString& method);
}
