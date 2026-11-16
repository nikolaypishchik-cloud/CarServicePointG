#pragma once

#include "ExactMoney.h"
#include <optional>
#include <string_view>

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

enum class BalanceAuthority { Confirmed, DisconnectedReplica };
enum class AllocationError { UnconfirmedBalance, ClientMismatch, InvalidAmount, InvalidBalance, InsufficientAdvance, ExceedsOrderDebt };
struct Allocation {
    Movement change;
    Cents remainingAdvance;
    Cents remainingOrderDebt;
};

// Both balances must come from the posting transaction, after prior allocations and returns.
inline std::variant<Allocation, AllocationError> allocateAdvance(
    std::string_view advanceClientUid, std::string_view orderClientUid,
    Cents freeAdvance, Cents orderDebt, Cents amount, BalanceAuthority authority)
{
    if (authority != BalanceAuthority::Confirmed)
        return AllocationError::UnconfirmedBalance;
    if (advanceClientUid.empty() || advanceClientUid != orderClientUid)
        return AllocationError::ClientMismatch;
    if (amount <= 0)
        return AllocationError::InvalidAmount;
    if (freeAdvance < 0 || orderDebt < 0)
        return AllocationError::InvalidBalance;
    if (amount > freeAdvance)
        return AllocationError::InsufficientAdvance;
    if (amount > orderDebt)
        return AllocationError::ExceedsOrderDebt;
    return Allocation{*movement(Operation::Allocation, amount), freeAdvance - amount, orderDebt - amount};
}

enum class Method { Cash, Card, Transfer, NoMoneyMovement };

inline bool canReceiveAdvance(Method method, bool shiftOpen)
{
    switch (method) {
    case Method::Cash: return shiftOpen;
    case Method::Card:
    case Method::Transfer: return true;
    case Method::NoMoneyMovement: return false;
    }
    return false;
}

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
