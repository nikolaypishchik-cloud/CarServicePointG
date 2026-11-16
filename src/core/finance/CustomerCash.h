#pragma once

#include "ExactMoney.h"
#include <optional>

namespace CustomerCash {
using ExactMoney::Cents;

enum class Operation { Payment, Advance, Allocation, PaymentRefund, AdvanceRefund };
struct Movement {
    Cents received;
    Cents paidOnOrders;
    Cents freeAdvance;
};

// Projects a posted operation. A reversal uses the original operation type and amount.
// Drawer transfers and supplier expenses do not belong to the customer ledger.
inline std::optional<Movement> movement(Operation operation, Cents amount, bool reversal = false)
{
    if (amount <= 0)
        return std::nullopt;
    const Cents delta = reversal ? -amount : amount;
    switch (operation) {
    case Operation::Payment:       return Movement{delta, delta, 0};
    case Operation::Advance:       return Movement{delta, 0, delta};
    case Operation::Allocation:    return Movement{0, delta, -delta};
    case Operation::PaymentRefund: return Movement{-delta, -delta, 0};
    case Operation::AdvanceRefund: return Movement{-delta, 0, -delta};
    }
    return std::nullopt;
}

enum class Method { Cash, Card, Transfer, NoMoneyMovement };

// A non-customer expense, opening count or transfer is recorded separately.
inline std::optional<Cents> drawerChange(Operation operation, Cents amount, Method method,
                                         bool reversal = false)
{
    if ((operation == Operation::Allocation) != (method == Method::NoMoneyMovement))
        return std::nullopt;
    const auto effects = movement(operation, amount, reversal);
    if (!effects)
        return std::nullopt;
    switch (method) {
    case Method::Cash: return effects->received;
    case Method::Card:
    case Method::Transfer:
    case Method::NoMoneyMovement: return Cents{0};
    }
    return std::nullopt;
}
}
