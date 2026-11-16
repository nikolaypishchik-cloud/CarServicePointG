#include "core/finance/ExactMoney.h"
#include "core/finance/CustomerCash.h"
#include <fstream>
#include <iostream>
#include <sstream>
using namespace ExactMoney;
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    std::ifstream input(argv[1]);if(!input)return 2;
    unsigned count=0,failures=0;std::string line;
    while(std::getline(input,line)) {
        std::istringstream row(line);std::string qty,price,expected;
        if(!(row>>qty>>price>>expected))return 2;
        const auto result=lineAmount(qty,price);++count;
        const bool good=expected=="ERROR"?std::holds_alternative<Error>(result)
            :std::holds_alternative<Cents>(result)&&std::to_string(std::get<Cents>(result))==expected;
        if(!good){++failures;std::cerr<<"Mismatch "<<qty<<" * "<<price<<" expected "<<expected<<"\n";}
    }
    auto check=[&](const Result<Cents>& result,Cents expected) {
        ++count;if(!std::holds_alternative<Cents>(result)||std::get<Cents>(result)!=expected)++failures;
    };
    auto reject=[&](const Result<Cents>& result) {++count;if(!std::holds_alternative<Error>(result))++failures;};
    check(sum({1,1}),2);
    check(discounted(100000,DiscountMode::Percent,"50"),50000);
    check(discounted(100000,DiscountMode::Uah,"50"),95000);
    check(discounted(1,DiscountMode::Percent,"50"),0);
    check(discounted(100000,DiscountMode::Percent,"10.123456"),89877);
    reject(discounted(100,DiscountMode::Percent,"100.000001"));
    reject(discounted(100,DiscountMode::Uah,"1.01"));
    reject(discounted(100,DiscountMode::Uah,"-1"));
    check(refundRemaining(10000,2000,3000,4000),1000);
    reject(refundRemaining(10000,4000,4000,4000));
    reject(sum({std::numeric_limits<Cents>::max(),1}));
    reject(sum({std::numeric_limits<Cents>::min(),-1}));
    check(payment("12.34"),1234);reject(payment("12.345"));
    using CustomerCash::Operation;
    auto checkMovement = [&](Operation operation, Cents amount, Cents received,
                             Cents paid, Cents freeAdvance) {
        for (bool reverse : {false, true}) {
            ++count;
            const auto result = CustomerCash::movement(operation, amount, reverse);
            const int sign = reverse ? -1 : 1;
            if (!result || result->received != sign * received
                || result->paidOnOrders != sign * paid || result->freeAdvance != sign * freeAdvance)
                ++failures;
        }
    };
    checkMovement(Operation::Advance, 400000, 400000, 0, 400000);
    checkMovement(Operation::Allocation, 250000, 0, 250000, -250000);
    checkMovement(Operation::Payment, 750000, 750000, 750000, 0);
    checkMovement(Operation::AdvanceRefund, 150000, -150000, 0, -150000);
    checkMovement(Operation::PaymentRefund, 100000, -100000, -100000, 0);
    for (Cents invalid : {Cents{0}, std::numeric_limits<Cents>::min()}) {
        ++count;
        if (CustomerCash::movement(Operation::Advance, invalid))
            ++failures;
    }
    using CustomerCash::Allocation;
    using CustomerCash::AllocationError;
    using CustomerCash::allocateAdvance;
    constexpr auto confirmed = CustomerCash::BalanceAuthority::Confirmed;
    const auto firstOrder = allocateAdvance("client-a", "client-a", 400000, 1000000, 250000, confirmed);
    const auto* first = std::get_if<Allocation>(&firstOrder);
    ++count;
    if (!first || first->change.received != 0 || first->change.paidOnOrders != 250000
        || first->remainingOrderDebt != 750000 || first->remainingAdvance != 150000) {
        ++failures;
    } else {
        const auto secondOrder = allocateAdvance("client-a", "client-a", first->remainingAdvance, 180000, 150000, confirmed);
        const auto* second = std::get_if<Allocation>(&secondOrder);
        ++count;
        if (!second || second->change.received != 0 || second->remainingAdvance != 0
            || second->remainingOrderDebt != 30000 || first->change.paidOnOrders + second->change.paidOnOrders != 400000)
            ++failures;
    }
    auto checkAllocationError = [&](const auto& result, AllocationError expected) {
        ++count;
        const auto* actual = std::get_if<AllocationError>(&result);
        if (!actual || *actual != expected) ++failures;
    };
    checkAllocationError(allocateAdvance("a","b",100,100,1, confirmed), AllocationError::ClientMismatch);
    checkAllocationError(allocateAdvance("","",100,100,1, confirmed), AllocationError::ClientMismatch);
    checkAllocationError(allocateAdvance("a","a",100,100,0, confirmed), AllocationError::InvalidAmount);
    checkAllocationError(allocateAdvance("a","a",100,100,-1, confirmed), AllocationError::InvalidAmount);
    checkAllocationError(allocateAdvance("a","a",-1,100,1, confirmed), AllocationError::InvalidBalance);
    checkAllocationError(allocateAdvance("a","a",100,-1,1, confirmed), AllocationError::InvalidBalance);
    checkAllocationError(allocateAdvance("a","a",99,100,100, confirmed), AllocationError::InsufficientAdvance);
    checkAllocationError(allocateAdvance("a","a",100,99,100, confirmed), AllocationError::ExceedsOrderDebt);
    using CustomerCash::Method;
    checkAllocationError(allocateAdvance("a","a",100,100,90,CustomerCash::BalanceAuthority::DisconnectedReplica),
                         AllocationError::UnconfirmedBalance);
    ++count;
    if (CustomerCash::canReceiveAdvance(Method::Cash,false)
        || !CustomerCash::canReceiveAdvance(Method::Cash,true)
        || CustomerCash::canReceiveAdvance(Method::NoMoneyMovement,true))
        ++failures;
    auto checkDrawer = [&](Operation op, Method method, bool reversal, std::optional<Cents> expected) {
        ++count;
        if (CustomerCash::drawerChange(op, 1250, method, reversal) != expected)
            ++failures;
    };
    checkDrawer(Operation::Payment, Method::Cash, false, 1250);
    checkDrawer(Operation::Advance, Method::Cash, true, -1250);
    checkDrawer(Operation::PaymentRefund, Method::Cash, false, -1250);
    checkDrawer(Operation::Payment, Method::Card, false, 0);
    checkDrawer(Operation::Advance, Method::Transfer, false, 0);
    checkDrawer(Operation::Allocation, Method::NoMoneyMovement, false, 0);
    checkDrawer(Operation::Allocation, Method::Cash, false, std::nullopt);
    checkDrawer(Operation::Payment, Method::NoMoneyMovement, false, std::nullopt);
    std::cout<<"CHECKS="<<count<<" FAILURES="<<failures<<"\n";
    return count>14 && !failures?0:1;
}
