#include "core/finance/ExactMoney.h"
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
    std::cout<<"CHECKS="<<count<<" FAILURES="<<failures<<"\n";
    return count>14 && !failures?0:1;
}
