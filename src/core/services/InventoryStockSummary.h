#pragma once
#include "WorkshopInventoryService.h"
#include <QMap>
#include <QRegularExpression>
#include <array>

namespace InventoryStockSummary {
struct Totals {
    std::array<QMap<QString,double>,3> quantities;
    std::array<QStringList,3> details;
    double value=0;
};
inline int group(const QString& name) {
    const auto n=name.toCaseFolded();
    if(n.contains(QString::fromUtf8("антифриз")) || n.contains(QString::fromUtf8("тосол")) || n.contains("coolant"))return 1;
    static const QRegularExpression oil(QString::fromUtf8("олив|масло|масла|мастил|\\boil\\b|\\d{1,2}\\s*[wв]\\s*[-–]?\\s*\\d{2}"));
    return oil.match(n).hasMatch()?0:2;
}
inline Totals calculate(const QVector<WorkshopInventoryBalanceRow>& rows) {
    Totals out;
    for(const auto& row:rows) {
        out.value+=row.stockValue;
        if(qAbs(row.balanceQuantity)<0.0000001)continue;
        QString unit=row.unit.trimmed().toCaseFolded();double quantity=row.balanceQuantity;
        if(unit==QString::fromUtf8("мл") || unit=="ml") {quantity/=1000;unit=QString::fromUtf8("л");}
        else if(unit=="l" || unit==QString::fromUtf8("літр") || unit==QString::fromUtf8("литр"))unit=QString::fromUtf8("л");
        else if(unit==QString::fromUtf8("г") || unit=="g") {quantity/=1000;unit=QString::fromUtf8("кг");}
        else if(unit=="kg")unit=QString::fromUtf8("кг");
        else if(unit==QString::fromUtf8("шт") || unit=="pcs")unit=QString::fromUtf8("шт.");
        if(unit.isEmpty())unit=QString::fromUtf8("од.");
        const int index=group(row.materialName);
        out.quantities[index][unit]+=quantity;
        out.details[index].append(QString("%1: %2 %3").arg(row.materialName,QString::number(quantity,'g',12),unit));
    }
    return out;
}
}
