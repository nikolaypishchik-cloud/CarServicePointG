#pragma once

#include "core/services/catalog/CatalogTypes.h"

class QWidget;

class CatalogController {
public:
    static bool saveRow(QWidget* parent, const CatalogRowData& row);
    static bool deleteRowWithConfirmation(QWidget* parent, int id);
    static bool importFromOrders(QWidget* parent);
};
