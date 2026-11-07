#pragma once

#include <QString>
#include <QStringList>
#include <QList>

struct CatalogRowData {
    int id = 0;
    QString brand;
    QString model;
    int yearFrom = 0;
    int yearTo = 0;
    QString fuel;
    QString group;
    QString oem;
    QString name;
    QString brandName;
    QString supplier;
    double price = 0.0;
    QString note;

    QString effectiveBrand;
    QString effectiveModel;
    QString brandKey;
    QString modelKey;
    QString fuelKey;
    QString groupKey;
    QString searchHaystack;
    QString oemSearchKey;
    int effectiveYearFrom = 0;
    int effectiveYearTo = 0;
    bool brandRecognized = false;
};

struct CatalogFilterState {
    QString brand;
    QString model;
    QString fuel;
    QString group;
    QString search;
    int yearFrom = 0;
    int yearTo = 0;
};

struct CatalogFilterOptions {
    QStringList brands;
    QStringList models;
    QStringList fuels;
    QStringList groups;
    QList<int> yearsFrom;
    QList<int> yearsTo;
    QString errorText;
};
