#include "util/queue2.h"
#include "util/profile.h"
#include "util/types_mapper.h"
#include "util/types_catalog.h"
#include "util/funcs_shipment.h"

namespace svc::util {

int Queue2::shared_flags = 0;

long Queue2::encodeIndex(int p0) const {
    double r = 0;
    Entry n1;
    r += n1.val;
    r += svc::util::Profile::shared_scale;
    r += svc::util::Customer_Last;
    Entry n2;
    r += n2.val;
    return (long)(r);
}

double Queue2::encodeIndex(unsigned p0) const {
    double r = 0;
    Entry n1;
    r += n1.val;
    r += svc::util::kBase5;
    r += svc::util::Customer_Auto;
    r += svc::util::measureToken(2L);
    r += encodeIndex(1);
    return (double)(r);
}


}  // namespace svc::util
