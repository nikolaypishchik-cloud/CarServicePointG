#pragma once
#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <variant>
#include <vector>

// Version 1: UAH, at most six decimal places for new quantity/unit price.
// Legacy REAL values must use an explicit adapter; never reinterpret on read.
namespace ExactMoney {
enum class Error { InvalidDecimal, ExcessPrecision, Overflow, InvalidQuantity, NegativePrice, InvalidDiscount, RefundExceeded };
template<class T> using Result = std::variant<T, Error>;
struct Decimal { std::uint64_t coefficient; unsigned scale; bool negative; };
using Cents = std::int64_t;

inline Result<Decimal> parse(const std::string& text, unsigned maxScale=6) {
    if(text.empty()) return Error::InvalidDecimal;
    std::size_t i=0; bool negative=false;
    if(text[0]=='-' || text[0]=='+') { negative=text[0]=='-'; ++i; }
    if(i==text.size()) return Error::InvalidDecimal;
    std::uint64_t coefficient=0; unsigned scale=0; bool dot=false, digit=false;
    for(;i<text.size();++i) {
        const char c=text[i];
        if(c=='.' && !dot && digit) { dot=true; if(i+1==text.size())return Error::InvalidDecimal; continue; }
        if(c<'0'||c>'9') return Error::InvalidDecimal;
        digit=true;
        if(dot && ++scale>maxScale) return Error::ExcessPrecision;
        const auto d=static_cast<unsigned>(c-'0');
        const auto limit=static_cast<std::uint64_t>(std::numeric_limits<Cents>::max());
        if(coefficient>(limit-d)/10) return Error::Overflow;
        coefficient=coefficient*10+d;
    }
    return Decimal{coefficient,scale,negative && coefficient!=0};
}

// Decimal digit multiplication is bounded (<=38 digits), portable on MSVC.
// No binary floating point and no compiler-specific 128-bit integer required.
inline std::string product(std::uint64_t lhs,std::uint64_t rhs) {
    const auto a=std::to_string(lhs),b=std::to_string(rhs);
    std::vector<unsigned> digits(a.size()+b.size(),0);
    for(std::size_t i=0;i<a.size();++i) for(std::size_t j=0;j<b.size();++j)
        digits[i+j+1]+=static_cast<unsigned>(a[i]-'0')*static_cast<unsigned>(b[j]-'0');
    for(std::size_t i=digits.size()-1;i>0;--i) {digits[i-1]+=digits[i]/10;digits[i]%=10;}
    std::string out;for(auto d:digits)out.push_back(static_cast<char>('0'+d));
    const auto first=out.find_first_not_of('0');return first==std::string::npos?"0":out.substr(first);
}
inline Result<Cents> roundedInteger(std::string digits,unsigned fractionalDigits,bool negative=false) {
    bool up=false;
    if(fractionalDigits) {
        if(digits.size()<=fractionalDigits) digits.insert(0,fractionalDigits+1-digits.size(),'0');
        const auto split=digits.size()-fractionalDigits;up=digits[split]>='5';digits.resize(split);
    }
    Cents value=0;
    for(char c:digits) {
        const int digit=c-'0';
        if(value>(std::numeric_limits<Cents>::max()-digit)/10)return Error::Overflow;
        value=value*10+digit;
    }
    if(up) {if(value==std::numeric_limits<Cents>::max())return Error::Overflow;++value;}
    return negative?-value:value;
}
inline Result<Cents> lineAmount(const std::string& quantity,const std::string& unitPrice, unsigned maxScale=6) {
    const auto q=parse(quantity,maxScale),p=parse(unitPrice,maxScale);
    if(auto* error=std::get_if<Error>(&q))return *error;
    if(auto* error=std::get_if<Error>(&p))return *error;
    const auto qty=std::get<Decimal>(q),price=std::get<Decimal>(p);
    if(qty.negative || qty.coefficient==0)return Error::InvalidQuantity;
    if(price.negative)return Error::NegativePrice;
    auto digits=product(qty.coefficient,price.coefficient);
    const auto scale=qty.scale+price.scale;
    if(scale<2) {digits.append(2-scale,'0');return roundedInteger(digits,0);}
    return roundedInteger(digits,scale-2);
}
// Stored per-unit costs can retain more precision than manually entered prices.
inline Result<Cents> storedLineAmount(const std::string& quantity,const std::string& unitPrice) {
    return lineAmount(quantity,unitPrice,18);
}
inline Result<Cents> payment(const std::string& amount) {
    const auto parsed=parse(amount,2);
    if(auto* error=std::get_if<Error>(&parsed))return *error;
    const auto d=std::get<Decimal>(parsed);auto digits=std::to_string(d.coefficient);
    digits.append(2-d.scale,'0');return roundedInteger(digits,0,d.negative);
}
inline Result<Cents> sum(const std::vector<Cents>& values) {
    Cents total=0;
    for(const auto value:values) {
        if((value>0 && total>std::numeric_limits<Cents>::max()-value)
            || (value<0 && total<std::numeric_limits<Cents>::min()-value))return Error::Overflow;
        total+=value;
    }
    return total;
}
enum class DiscountMode { Percent, Uah };
inline Result<Cents> discounted(Cents base,DiscountMode mode,const std::string& value) {
    if(base<0)return Error::InvalidDiscount;
    Result<Cents> deduction=Error::InvalidDiscount;
    if(mode==DiscountMode::Uah) deduction=payment(value);
    else if(mode==DiscountMode::Percent) {
        const auto parsed=parse(value);
        if(auto* error=std::get_if<Error>(&parsed))return *error;
        const auto d=std::get<Decimal>(parsed);std::uint64_t maxPercent=100;
        for(unsigned i=0;i<d.scale;++i)maxPercent*=10;
        if(d.negative || d.coefficient>maxPercent)return Error::InvalidDiscount;
        deduction=roundedInteger(product(static_cast<std::uint64_t>(base),d.coefficient),d.scale+2);
    }
    if(auto* error=std::get_if<Error>(&deduction))return *error;
    const auto amount=std::get<Cents>(deduction);
    if(amount<0 || amount>base)return Error::InvalidDiscount;
    return base-amount;
}
inline Result<Cents> refundRemaining(Cents originalNet,Cents refunded,Cents credited,Cents debtReduced) {
    if(originalNet<0 || refunded<0 || credited<0 || debtReduced<0)return Error::RefundExceeded;
    const auto used=sum({refunded,credited,debtReduced});
    if(auto* error=std::get_if<Error>(&used))return *error;
    if(std::get<Cents>(used)>originalNet)return Error::RefundExceeded;
    return originalNet-std::get<Cents>(used);
}
} // namespace ExactMoney
