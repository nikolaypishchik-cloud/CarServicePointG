#pragma once

#include <QDate>
#include <QList>
#include <QString>
#include <QStringList>

struct StatisticsSeriesPoint {
    QString label;
    double value = 0.0;
};

struct StatisticsPairPoint {
    QString label;
    double a = 0.0;
    double b = 0.0;
};

struct StatisticsRowItem {
    QString a;
    QString b;
    QString c;
    QString d;
    QString e;
};

struct StatisticsSnapshot {
    QString error;
    QDate from;
    QDate to;
    double revenueLabor = 0.0;
    double revenueParts = 0.0;
    double purchaseParts = 0.0;
    double markupParts = 0.0;
    double payroll = 0.0;
    double expenses = 0.0;
    double grossProfit = 0.0;
    double netProfit = 0.0;
    int closedOrders = 0;
    int openOrders = 0;
    int unpaidOrders = 0;
    double avgCheck = 0.0;
    double avgRepairDays = 0.0;
    double avgJobsPerOrder = 0.0;
    int carsInWork = 0;
    double outsourceRevenue = 0.0;
    double outsourceCost = 0.0;
    double outsourceMargin = 0.0;
    int repeatedClients = 0;
    int uniqueClients = 0;
    QList<StatisticsSeriesPoint> dailyRevenue;
    QList<StatisticsSeriesPoint> incomeStructure;
    QList<StatisticsSeriesPoint> expenseStructure;
    QList<StatisticsSeriesPoint> repairStages;
    QList<StatisticsSeriesPoint> supplierSpend;
    QList<StatisticsSeriesPoint> partUsage;
    QList<StatisticsSeriesPoint> masterRevenue;
    QList<StatisticsSeriesPoint> masterJobs;
    QList<StatisticsSeriesPoint> clientRevenue;
    QList<StatisticsRowItem> masterRows;
    QList<StatisticsRowItem> partRows;
    QList<StatisticsRowItem> clientRows;
    QStringList problems;
};
