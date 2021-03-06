


#include "unicode/dtintrv.h"


U_NAMESPACE_BEGIN

UOBJECT_DEFINE_RTTI_IMPLEMENTATION(DateInterval)

//DateInterval::DateInterval(){}


DateInterval::DateInterval(UDate from, UDate to)
:   fromDate(from),
    toDate(to)
{}


DateInterval::~DateInterval(){}


DateInterval::DateInterval(const DateInterval& other)
: UObject(other) {
    *this = other;
}   


DateInterval&
DateInterval::operator=(const DateInterval& other) {
    if ( this != &other ) {
        fromDate = other.fromDate;
        toDate = other.toDate;
    }
    return *this;
}


DateInterval* 
DateInterval::clone() const {
    return new DateInterval(*this);
}


UBool 
DateInterval::operator==(const DateInterval& other) const { 
    return ( fromDate == other.fromDate && toDate == other.toDate );
}


U_NAMESPACE_END

