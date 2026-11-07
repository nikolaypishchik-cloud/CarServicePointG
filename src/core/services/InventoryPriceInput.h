#pragma once
#include <cmath>
namespace InventoryPriceInput {
inline double unitPrice(double entered,double quantity,bool wholeQuantity) {
    if(!std::isfinite(entered) || !std::isfinite(quantity) || quantity<=0 || entered<0)return -1;
    // Keep fractional kopecks per ml; round the monetary total, not this rate.
    return wholeQuantity ? entered/quantity : entered;
}
}
